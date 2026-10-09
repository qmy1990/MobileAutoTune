#include "mat/mobile_audio.h"
#include "../src/pitch.hpp"
#include "../src/live_input.hpp"
#include <cstdio>
#include <cstdlib>
#include <cmath>
#include <vector>
#include <atomic>
#include <thread>
#include <new>
static std::atomic<unsigned long> allocations{0};
void *operator new(std::size_t n){allocations.fetch_add(1);if(auto p=std::malloc(n?n:1))return p;throw std::bad_alloc();}
void *operator new[](std::size_t n){return ::operator new(n);}
void operator delete(void *p) noexcept {std::free(p);}
void operator delete[](void *p) noexcept {std::free(p);}
void operator delete(void *p,std::size_t) noexcept {std::free(p);}
void operator delete[](void *p,std::size_t) noexcept {std::free(p);}
#define CHECK(v) do{if(!(v)){std::fprintf(stderr,"FAIL line %d: %s\n",__LINE__,#v);std::exit(1);}}while(0)
int main(){
    {
        LiveInput queue(5);float a[]{1,2,3,4,5,6,7},b[7]{};
        CHECK(queue.push(a,3)==0);CHECK(queue.pop(b,2)==2);CHECK(b[0]==1&&b[1]==2);
        CHECK(queue.push(a+3,4)==0);CHECK(queue.discard(1)==1);CHECK(queue.pop(b,7)==4);
        CHECK(b[0]==4&&b[3]==7);CHECK(queue.push(a,7)==2);CHECK(queue.pop(b,7)==5);CHECK(b[0]==3&&b[4]==7);
        CHECK(queue.push(a,4)==0);CHECK(queue.push(a+4,3)==2);CHECK(queue.pop(b,5)==5);CHECK(b[0]==3&&b[4]==7);
        LiveInput clocked(4096);std::vector<float> ramp(2000),resampled(1000);
        for(int i=0;i<2000;++i)ramp[i]=float(i);
        CHECK(clocked.push(ramp.data(),2000)==0);
        CHECK(clocked.popClocked(resampled.data(),1000,1.0002)==1000);
        for(int i=0;i<1000;++i)CHECK(std::abs(resampled[i]-i*1.0002)<.0001);
        CHECK(clocked.popClocked(resampled.data(),998,1.0002)==998);
        for(int i=0;i<998;++i)CHECK(std::abs(resampled[i]-(i+1000)*1.0002)<.00015);
        CHECK(clocked.size()==2);
        LiveInput slow(4096);slow.push(ramp.data(),2000);
        CHECK(slow.popClocked(resampled.data(),1000,.9998)==1000);
        for(int i=0;i<1000;++i)CHECK(std::abs(resampled[i]-i*.9998)<.0001);
    }
    CHECK(!mat_create(44100,MAT_REALTIME));
    for(int mode=0;mode<2;++mode){
        auto *e=mat_create(48000,mat_mode(mode));CHECK(e);
        auto p=mat_default_parameters();p.tuning=p.reverb=p.echo=p.denoise=0;
        CHECK(mat_set_parameters(e,&p)==0);mat_reset(e);
        p.denoise=NAN;CHECK(mat_set_parameters(e,&p)==-1);p.denoise=0;
        CHECK(mode || mat_stream_max_history_samples(e)==1920);
        int delay=mat_latency_samples(e);
        std::printf("mode %d frame latency %.3f ms, stream %.3f ms\n",mode,delay/48.f,mat_stream_latency_samples(e)/48.f);
        if(!mode)CHECK(mat_stream_latency_samples(e)<=1920);
        std::vector<float> input(48000),output(48000);
        for(int i=0;i<48000;++i)input[i]=0.15f*std::sin(2*3.141592653589793*225*i/48000);
        auto beforeAlloc=allocations.load();
        for(int i=0;i<48000;i+=480)CHECK(!mat_process_frame(e,input.data()+i,output.data()+i));
        CHECK(allocations.load()==beforeAlloc);
        for(int i=delay;i<48000;++i)CHECK(std::abs(output[i]-input[i-delay])<1e-6f);
        auto t=mat_get_telemetry(e);CHECK(std::abs(t.input_pitch_hz-225)<2);CHECK(t.processed_frames==48000);
        mat_reset(e);
        std::fill(output.begin(),output.end(),0);
        for(int i=0;i<48000;){int n=std::min(137,48000-i);CHECK(!mat_process(e,input.data()+i,output.data()+i,n));i+=n;}
        delay=mat_stream_latency_samples(e);
        for(int i=delay;i<48000;++i)CHECK(std::abs(output[i]-input[i-delay])<1e-6f);
        p.tuning=1;p.tonic=0;p.scale=MAT_MAJOR;CHECK(!mat_set_parameters(e,&p));mat_reset(e);
        for(int i=0;i<48000;i+=480)mat_process_frame(e,input.data()+i,output.data()+i);
        t=mat_get_telemetry(e);CHECK(std::abs(t.target_pitch_hz-220)<0.1f);CHECK(t.correction_semitones<0);
        PitchTracker analyzer(true);for(int i=24000;i<48000;i+=480){analyzer.feed(output.data()+i,480);analyzer.analyze();}
        std::printf("225 Hz -> %.3f Hz, target %.3f, correction %.3f\n",analyzer.hz,t.target_pitch_hz,t.correction_semitones);
        CHECK(std::abs(analyzer.hz-220)<3);
        for(auto value:output)CHECK(std::isfinite(value) && std::abs(value)<=1);
        // New independent transposition, including both octave endpoints, must
        // work with tuning disabled. Enhanced tuning stays on the scale note.
        auto nominalLatency=mat_latency_samples(e);
        for(float amount:{-12.f,-5.f,5.f,12.f}){
            p.tuning=0;p.pitch_shift_semitones=amount;CHECK(!mat_set_parameters(e,&p));mat_reset(e);
            auto allocationStart=allocations.load();
            for(int i=0;i<48000;i+=480)CHECK(!mat_process_frame(e,input.data()+i,output.data()+i));
            CHECK(allocations.load()==allocationStart);CHECK(mat_latency_samples(e)==nominalLatency);
            PitchTracker shiftedPitch(true);for(int i=24000;i<48000;i+=480){shiftedPitch.feed(output.data()+i,480);shiftedPitch.analyze();}
            float expected=225*std::exp2(amount/12.f);
            std::printf("mode%d transpose%+.1f: %.3fHz expected%.3fHz\n",mode,amount,shiftedPitch.hz,expected);
            CHECK(std::abs(shiftedPitch.hz-expected)<std::max(3.f,expected*.02f));
            CHECK(std::abs(mat_get_telemetry(e).correction_semitones-amount)<.01f);
            for(auto value:output)CHECK(std::isfinite(value)&&std::abs(value)<=1);
        }
        // These offsets distinguish post-transposition scale quantization from
        // tuning the original first: old+1 would giveA# (outsideCmajor), nowB.
        for(float amount:{-6.f,-1.f,1.f,6.f}){
            p.tuning=1.2f;p.pitch_shift_semitones=amount;CHECK(!mat_set_parameters(e,&p));mat_reset(e);
            auto allocationStart=allocations.load();
            for(int i=0;i<48000;i+=480)CHECK(!mat_process_frame(e,input.data()+i,output.data()+i));
            CHECK(allocations.load()==allocationStart);
            float midi=69+12*std::log2(225/440.f)+amount;
            float expected=440*std::exp2((nearestScaleNote(midi,0,MAT_MAJOR)-69)/12);
            PitchTracker enhancedPitch(true);for(int i=24000;i<48000;i+=480){enhancedPitch.feed(output.data()+i,480);enhancedPitch.analyze();}
            auto shiftedTelemetry=mat_get_telemetry(e);
            std::printf("mode%d transpose%+.1f thenCmajor: %.3fHz target%.3fHz\n",mode,amount,enhancedPitch.hz,shiftedTelemetry.target_pitch_hz);
            CHECK(std::abs(enhancedPitch.hz-expected)<3);
            CHECK(std::abs(shiftedTelemetry.target_pitch_hz-expected)<.1f);
            CHECK(std::abs(225*std::exp2(shiftedTelemetry.correction_semitones/12)-expected)<.2f);
        }
        p.pitch_shift_semitones=12.01f;CHECK(mat_set_parameters(e,&p)==-1);
        p.pitch_shift_semitones=-12.01f;CHECK(mat_set_parameters(e,&p)==-1);
        p.pitch_shift_semitones=NAN;CHECK(mat_set_parameters(e,&p)==-1);
        p.pitch_shift_semitones=INFINITY;CHECK(mat_set_parameters(e,&p)==-1);
        p.pitch_shift_semitones=0;p.tuning=1.201f;CHECK(mat_set_parameters(e,&p)==-1);
        p.tuning=1.2f;CHECK(!mat_set_parameters(e,&p));mat_reset(e);
        for(int i=0;i<48000;i+=480)CHECK(!mat_process_frame(e,input.data()+i,output.data()+i));
        PitchTracker hardPitch(true);for(int i=24000;i<48000;i+=480){hardPitch.feed(output.data()+i,480);hardPitch.analyze();}
        CHECK(std::abs(hardPitch.hz-220)<3); // No120% pitch overshoot.

        // Silence, nonfinite input and in-place processing remain finite.
        float block[480]{};block[2]=INFINITY;block[3]=NAN;mat_process_frame(e,block,block);
        for(float value:block)CHECK(std::isfinite(value));
        // All controls can change concurrently without a processing lock.
        std::atomic<bool> done{false};
        std::thread control([&]{auto knobs=p;int k=0;while(!done.load()){knobs.reverb=(++k&1)?1:0;knobs.echo=knobs.reverb;knobs.denoise=knobs.reverb;knobs.pitch_shift_semitones=(k&1)?-5.f:5.f;CHECK(!mat_set_parameters(e,&knobs));}});
        for(int f=0;f<100;++f){for(int i=0;i<480;++i)block[i]=0.01f*std::sin(i*0.02f);mat_process_frame(e,block,block);for(float v:block)CHECK(std::isfinite(v)&&std::abs(v)<=1);}
        done=true;control.join();
        mat_destroy(e);
    }
    {
        // Changing live buffering must preserve all four effects exactly after
        // removing the stream adapter's one-frame delay.
        auto *framed=mat_create(48000,MAT_REALTIME),*streamed=mat_create(48000,MAT_REALTIME);
        CHECK(framed&&streamed);auto p=mat_default_parameters();p.tuning=1.2f;p.reverb=p.echo=1;p.denoise=.35f;p.pitch_shift_semitones=5;
        CHECK(!mat_set_parameters(framed,&p)&&!mat_set_parameters(streamed,&p));mat_reset(framed);mat_reset(streamed);
        std::vector<float> input(48000),a(48000),b(48000);
        for(int i=0;i<48000;++i)input[i]=.12f*std::sin(i*.035f)+.005f*std::sin(i*1.337f);
        for(int i=0;i<48000;i+=480){CHECK(!mat_process_frame(framed,input.data()+i,a.data()+i));CHECK(!mat_process(streamed,input.data()+i,b.data()+i,480));}
        for(int i=480;i<48000;++i)CHECK(b[i]==a[i-480]);
        mat_destroy(framed);mat_destroy(streamed);
    }
    CHECK(nearestScaleNote(61,0,MAT_MAJOR)==60);CHECK(nearestScaleNote(68,9,MAT_MINOR)==67);
    std::puts("All core tests passed");
}
