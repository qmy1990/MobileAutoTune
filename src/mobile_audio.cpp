#include "mat/mobile_audio.h"
#include "pitch.hpp"
#include "effects.hpp"
#include "live_pitch.hpp"
#include "signalsmith-stretch.h"
#include "rnnoise.h"
#include <atomic>
#include <memory>
#include <new>
#include <mutex>

extern "C" {
DenoiseState *mat_legacy_create(RNNModel *);
void mat_legacy_destroy(DenoiseState *);
int mat_legacy_init(DenoiseState *,RNNModel *);
float mat_legacy_process_frame(DenoiseState *,float *,const float *);
}
static_assert(std::atomic<float>::is_always_lock_free,"Float atomics must be lock free");
static_assert(std::atomic<uint64_t>::is_always_lock_free,"64-bit atomics must be lock free");
namespace {
std::once_flag legacyTables;
float clamp(float x,float lo,float hi){return std::max(lo,std::min(hi,x));}
bool valid(const mat_parameters &p){
    return p.tonic>=0 && p.tonic<12 && p.scale>=0 && p.scale<=2 &&
    std::isfinite(p.tuning) && p.tuning>=0 && p.tuning<=1.2f &&
    std::isfinite(p.reverb) && p.reverb>=0 && p.reverb<=1 &&
    std::isfinite(p.echo) && p.echo>=0 && p.echo<=1 &&
    std::isfinite(p.denoise) && p.denoise>=0 && p.denoise<=1 &&
    std::isfinite(p.echo_ms) && p.echo_ms>=40 && p.echo_ms<=800 &&
    std::isfinite(p.concert_a_hz) && p.concert_a_hz>=400 && p.concert_a_hz<=480 &&
    std::isfinite(p.pitch_shift_semitones) && p.pitch_shift_semitones>=-12 && p.pitch_shift_semitones<=12;
}
// One control writer. Atomic payload + generation allow bounded, coherent reads.
struct Mailbox {
    std::atomic<uint64_t> seq{0};
    std::atomic<int> tonic{3},scale{0};
    std::atomic<float> tuning{0.75f},reverb{0.18f},echo{0.08f},denoise{0.35f},echoMs{280},concertA{440},pitchShift{0};
    void set(const mat_parameters &p){
        seq.fetch_add(1);tonic.store(p.tonic);scale.store(p.scale);tuning.store(p.tuning);
        reverb.store(p.reverb);echo.store(p.echo);denoise.store(p.denoise);
        echoMs.store(p.echo_ms);concertA.store(p.concert_a_hz);pitchShift.store(p.pitch_shift_semitones);seq.fetch_add(1);
    }
    void read(mat_parameters &p){
        auto before=seq.load();if(before&1)return;
        mat_parameters next{tonic.load(),scale.load(),tuning.load(),reverb.load(),echo.load(),denoise.load(),echoMs.load(),concertA.load(),pitchShift.load()};
        if(seq.load()==before)p=next;
    }
};
}
struct mat_engine {
    mat_mode mode;
    Mailbox mailbox;
    mat_parameters params=mat_default_parameters();
    DenoiseState *noise=nullptr;
    PitchTracker pitch;
    signalsmith::stretch::SignalsmithStretch<float> shifter{42};
    LivePitchShifter liveShifter;
    std::unique_ptr<Delay> pitchDry,noiseDry;
    Effects effects;
    std::array<float,480> streamInput{},streamOutput{};
    int streamPos=0, unvoiced=0, latency=0;
    float shift=0,lastTarget=-999,smDenoise=0,smReverb=0,smEcho=0,smTune=0,smVoice=0,smPitchShift=0;
    std::array<float,5> midiHistory{};
    int historyPos=0,historyCount=0;
    std::atomic<float> hz{0},target{0},correction{0},confidence{0};
    std::atomic<uint64_t> frames{0};
    explicit mat_engine(mat_mode m):mode(m),pitch(m==MAT_OFFLINE){
        noise=m==MAT_REALTIME?mat_legacy_create(nullptr):rnnoise_create(nullptr);if(!noise)throw std::bad_alloc();
        std::unique_ptr<DenoiseState,void(*)(DenoiseState*)> guard(noise,m==MAT_REALTIME?mat_legacy_destroy:rnnoise_destroy);
        // Upstream v0.1 allocates FFT tables on its first process call. Warm
        // under call_once during construction so callbacks neither allocate
        // nor race when multiple independent handles are created.
        if(m==MAT_REALTIME){
            std::call_once(legacyTables,[this]{std::array<float,480> a{},b{};mat_legacy_process_frame(noise,b.data(),a.data());});
            mat_legacy_init(noise,nullptr);
        }
        // 16ms spectral window in live mode; 80ms window for offline quality.
        shifter.configure(1,m==MAT_REALTIME?768:3840,m==MAT_REALTIME?192:480);
        shifter.setFormantFactor(1,true);
        int shiftLatency=m==MAT_REALTIME?LivePitchShifter::latency:shifter.inputLatency()+shifter.outputLatency();
        int noiseLatency=m==MAT_REALTIME?480:960;
        latency=noiseLatency+shiftLatency;
        noiseDry=std::make_unique<Delay>(noiseLatency);
        pitchDry=std::make_unique<Delay>(shiftLatency);
        smDenoise=params.denoise;smReverb=params.reverb;smEcho=params.echo;smTune=params.tuning;smPitchShift=params.pitch_shift_semitones;
        guard.release();
    }
    ~mat_engine(){if(mode==MAT_REALTIME)mat_legacy_destroy(noise);else rnnoise_destroy(noise);}
    void reset(){
        if(mode==MAT_REALTIME)mat_legacy_init(noise,nullptr);else rnnoise_init(noise,nullptr);
        shifter.reset();liveShifter.reset();pitch.reset();pitchDry->reset();noiseDry->reset();effects.reset();
        streamInput.fill(0);streamOutput.fill(0);streamPos=unvoiced=0;
        shift=0;smVoice=0;lastTarget=-999;historyPos=historyCount=0;midiHistory.fill(0);
        mailbox.read(params);smDenoise=params.denoise;smReverb=params.reverb;smEcho=params.echo;smTune=params.tuning;smPitchShift=params.pitch_shift_semitones;
        hz=target=correction=confidence=0;frames=0;
    }
    void process(const float *in,float *out){
        mailbox.read(params);
        std::array<float,480> raw{},noisy{},clean{},mixed{},shifted{};
        for(int i=0;i<480;++i){raw[i]=std::isfinite(in[i])?clamp(in[i],-1,1):0;noisy[i]=raw[i]*32768.f;}
        if(mode==MAT_REALTIME)mat_legacy_process_frame(noise,clean.data(),noisy.data());
        else rnnoise_process_frame(noise,clean.data(),noisy.data());
        for(int i=0;i<480;++i){
            smDenoise+=0.002f*(params.denoise-smDenoise);
            mixed[i]=noiseDry->tick(raw[i])*(1-smDenoise)+clean[i]/32768.f*smDenoise;
        }
        // Compute the shifted pitch analytically, then quantize in the selected
        // scale. One tracker and one shifter suffice; no second audio pass.
        smPitchShift+=(1-std::exp(-0.5f))*(params.pitch_shift_semitones-smPitchShift);
        pitch.feed(mixed.data(),480);pitch.analyze();
        float desired=0,targetHz=0;
        bool voiced=pitch.hz>0 && pitch.confidence>0.82f;
        if(voiced){
            unvoiced=0;
            float midi=69+12*std::log2(pitch.hz/params.concert_a_hz);
            midiHistory[historyPos]=midi;historyPos=(historyPos+1)%5;historyCount=std::min(historyCount+1,5);
            if(mode==MAT_OFFLINE && historyCount>=3){
                auto history=midiHistory;std::sort(history.begin(),history.begin()+historyCount);midi=history[historyCount/2];
            }
            midi+=smPitchShift;
            float nearest=nearestScaleNote(midi,params.tonic,params.scale);
            // Hold near a scale boundary only if the held note is in the new scale.
            if(lastTarget>-900 && nearestScaleNote(lastTarget,params.tonic,params.scale)==lastTarget &&
               std::abs(midi-lastTarget)<std::abs(midi-nearest)+0.12f)nearest=lastTarget;
            lastTarget=nearest;targetHz=params.concert_a_hz*std::exp2((nearest-69)/12.f);
            desired=clamp(nearest-midi,-2,2)*std::min(params.tuning,1.f);
        }else if(++unvoiced>=3){lastTarget=-999;historyCount=0;historyPos=0;}
        // Above100%, speed and note-transition hardness increase, never overshoot
        // the scale note. The original0..100% response is preserved.
        float retuneMs=2/std::max(1.f,params.tuning)+140*std::pow(1-std::min(params.tuning,1.f),2);
        float alpha=1-std::exp(-10.f/retuneMs);
        shift+=alpha*(desired-shift);
        float totalShift=shift+smPitchShift;
        const float *src[]{mixed.data()};float *dst[]{shifted.data()};
        if(mode==MAT_REALTIME)liveShifter.process(mixed.data(),shifted.data(),480,totalShift,pitch.hz);
        else {shifter.setFormantFactor(std::exp2(smPitchShift/12.f),true);shifter.setTransposeSemitones(totalShift,8000.f/48000);shifter.setFormantBase(pitch.hz/48000.f);shifter.process(src,480,dst,480);}
        for(int i=0;i<480;++i){
            float dry=pitchDry->tick(mixed[i]);
            smTune+=0.002f*(params.tuning-smTune);
            // Preserve clean unvoiced consonants: stopped variable read heads can
            // otherwise comb-filter unvoiced audio even at a zero shift.
            smVoice+=0.002f*((voiced?1.f:0.f)-smVoice);
            float mechanical=clamp(smTune-1,0,0.2f);
            float voice=smVoice+mechanical*((voiced?1.f:0.f)-smVoice);
            float tuneActive=clamp(smTune*100,0,1)*(mode==MAT_REALTIME?voice:1.f);
            // Independent transposition also shifts unvoiced consonants and
            // works with autotune disabled or unavailable pitch detection.
            float pitchActive=clamp(std::abs(smPitchShift)*100,0,1);
            float active=pitchActive+(1-pitchActive)*tuneActive;
            float x=dry*(1-active)+shifted[i]*active;
            smReverb+=0.002f*(params.reverb-smReverb);smEcho+=0.002f*(params.echo-smEcho);
            out[i]=effects.tick(x,smReverb,smEcho,params.echo_ms);
        }
        if(targetHz>0 && smPitchShift!=0 && params.tuning==0)targetHz=pitch.hz*std::exp2(smPitchShift/12.f);
        hz.store(pitch.hz);target.store(targetHz);correction.store(totalShift);confidence.store(pitch.confidence);frames.fetch_add(480);
    }
};
extern "C" {
mat_parameters mat_default_parameters(){return {3,MAT_MAJOR,0.75f,0.18f,0.08f,0.35f,280,440,0};}
mat_engine *mat_create(int sr,mat_mode mode){
    if(sr!=48000 || (mode!=MAT_REALTIME && mode!=MAT_OFFLINE))return nullptr;
    try{return new mat_engine(mode);}catch(...){return nullptr;}
}
void mat_destroy(mat_engine *e){delete e;}
int mat_set_parameters(mat_engine *e,const mat_parameters *p){if(!e || !p || !valid(*p))return -1;e->mailbox.set(*p);return 0;}
int mat_process_frame(mat_engine *e,const float *in,float *out){if(!e || !in || !out)return -1;e->process(in,out);return 0;}
int mat_process(mat_engine *e,const float *in,float *out,int n){
    if(!e || !in || !out || n<0)return -1;
    for(int i=0;i<n;++i){
        e->streamInput[e->streamPos]=in[i];
        out[i]=e->streamOutput[e->streamPos];
        if(e->streamPos==479)e->process(e->streamInput.data(),e->streamOutput.data());
        if(++e->streamPos==480)e->streamPos=0;
    }return 0;
}
void mat_reset(mat_engine *e){if(e)e->reset();}
int mat_latency_samples(const mat_engine *e){return e?e->latency:0;}
int mat_stream_latency_samples(const mat_engine *e){return e?e->latency+480:0;}
int mat_max_history_samples(const mat_engine *e){return e?(e->mode==MAT_REALTIME?480+LivePitchShifter::maximumDelay:e->latency):0;}
int mat_stream_max_history_samples(const mat_engine *e){return e?mat_max_history_samples(e)+480:0;}
int mat_tail_samples(const mat_engine *e){return e?48000*4:0;}
mat_telemetry mat_get_telemetry(const mat_engine *e){if(!e)return {};return {e->hz.load(),e->target.load(),e->correction.load(),e->confidence.load(),e->frames.load()};}
const char *mat_version(){return "0.3.0";}
}
