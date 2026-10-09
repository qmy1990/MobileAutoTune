#include <jni.h>
#include <oboe/Oboe.h>
#include <oboe/FullDuplexStream.h>
#include <oboe/OboeExtensions.h>
#include <android/trace.h>
#include <android/log.h>
#include <dlfcn.h>
#include "mat/mobile_audio.h"
#include "mat/offline.h"
#include "recorder.hpp"
#include "live_input.hpp"
#include <atomic>
#include <memory>
#include <array>
#include <chrono>
#include <string>
#include <cmath>

struct Live : oboe::FullDuplexStream, oboe::AudioStreamErrorCallback {
    mat_engine *engine;
    bool monitoring=false;
    std::shared_ptr<oboe::AudioStream> input,output;
    Recorder recorder;
    std::atomic<float> maximumMs{0};
    std::atomic<float> recentMaximumMs{0};
    std::array<std::atomic<uint64_t>,128> histogram{};
    std::atomic<uint64_t> callbacks{0},lateCallbacks{0},resyncFrames{0};
    std::atomic<int> pendingFrames{0},maximumPendingFrames{0};
    std::vector<float> discard;
    std::vector<float> sourceBuffer;
    std::unique_ptr<LiveInput> capture;
    int warmup=20;
    double captureRatio=1;
    std::atomic<float> ratioPpm{0};
    bool processingStarted=false;
    bool legacyPolicy=false,inputMmap=false,outputMmap=false;
    int requestedPreset=0,actualPreset=9,requestedCallback=480;
    bool frameMode=false,requireMmap=false;
    std::atomic<float> callbackPeriodMs{0};
    bool presetFallback=false;
    std::atomic<uint64_t> underruns{0};
    std::atomic<bool> disconnected{false};
    explicit Live(mat_engine *e):engine(e){}
    ~Live(){close();}
    int open(const char *raw,const char *wet){
        oboe::AudioStreamBuilder out;
        out.setDirection(oboe::Direction::Output)->setPerformanceMode(oboe::PerformanceMode::LowLatency)
           ->setSharingMode(oboe::SharingMode::Exclusive)->setFormat(oboe::AudioFormat::Float)
           ->setChannelCount(1)->setSampleRate(48000)->setDataCallback(this)->setErrorCallback(this)
           ->setUsage(oboe::Usage::Media)->setSampleRateConversionQuality(oboe::SampleRateConversionQuality::None);
        if(!legacyPolicy)out.setFramesPerDataCallback(requestedCallback);
        auto result=out.openStream(output);
        if(result!=oboe::Result::OK){
            out.setSharingMode(oboe::SharingMode::Shared);result=out.openStream(output);
        }
        if(result!=oboe::Result::OK&&!requireMmap){
            out.setAudioApi(oboe::AudioApi::OpenSLES);result=out.openStream(output);
        }
        if(result!=oboe::Result::OK && requestedCallback && !requireMmap){
            out.setFramesPerDataCallback(0);result=out.openStream(output);
        }
        if(result!=oboe::Result::OK)return int(result);
        frameMode=output->getFramesPerDataCallback()==480;
        if(output->getSampleRate()!=48000 || output->getFormat()!=oboe::AudioFormat::Float)return -100;
        // One burst leaves no scheduling margin on the legacy FAST path.
        // Two bursts add only one hardware period (4 ms on the test device).
        output->setBufferSizeInFrames(output->getFramesPerBurst()*(legacyPolicy?1:2));
        if(!legacyPolicy && oboe::getSdkVersion()>=31)output->setPerformanceHintEnabled(true);
        oboe::AudioStreamBuilder in;
        in.setDirection(oboe::Direction::Input)->setAudioApi(output->getAudioApi())->setPerformanceMode(oboe::PerformanceMode::LowLatency)
          ->setSharingMode(oboe::SharingMode::Exclusive)->setFormat(oboe::AudioFormat::Float)
          ->setChannelCount(1)->setSampleRate(48000)
          ->setBufferCapacityInFrames(output->getBufferCapacityInFrames()*2)
          ->setSampleRateConversionQuality(oboe::SampleRateConversionQuality::None);
        // API 29 adds a karaoke capture path; older OS versions keep raw capture.
        // Do not force MMAP: the vendor may only support the shared FAST path.
        int preset=requestedPreset?requestedPreset:(oboe::getSdkVersion()>=29?10:9);
        if(legacyPolicy)preset=9;
        if(output->getAudioApi()==oboe::AudioApi::OpenSLES && preset==10){preset=9;presetFallback=true;}
        if(preset==10 && oboe::getSdkVersion()<29){preset=9;presetFallback=true;}
        in.setInputPreset(oboe::InputPreset(preset));
        result=in.openStream(input);
        if(result!=oboe::Result::OK && preset!=9){
            presetFallback=true;in.setInputPreset(oboe::InputPreset::Unprocessed);
            result=in.openStream(input);
        }
        if(result!=oboe::Result::OK){
            in.setSharingMode(oboe::SharingMode::Shared);result=in.openStream(input);
        }
        if(result!=oboe::Result::OK&&!requireMmap){
            presetFallback=true;in.setAudioApi(oboe::AudioApi::OpenSLES)->setInputPreset(oboe::InputPreset::Unprocessed);
            result=in.openStream(input);
        }
        if(result!=oboe::Result::OK)return int(result);
        actualPreset=int(input->getInputPreset());
        presetFallback=presetFallback || actualPreset!=preset;
        if(input->getSampleRate()!=48000 || input->getFormat()!=oboe::AudioFormat::Float)return -101;
        discard.resize(std::max(input->getBufferCapacityInFrames(),output->getBufferCapacityInFrames()));
        sourceBuffer.resize(output->getBufferCapacityInFrames());
        capture=std::make_unique<LiveInput>(discard.size()+sourceBuffer.size());
        inputMmap=oboe::OboeExtensions::isMMapUsed(input.get());outputMmap=oboe::OboeExtensions::isMMapUsed(output.get());
        if(requireMmap && (!inputMmap || !outputMmap))return int(oboe::Result::ErrorUnavailable);
        __android_log_print(ANDROID_LOG_INFO,"MobileAudio","open api=%s mmap=%d/%d burst=%d/%d buffer=%d inputCapacity=%d preset=%d fallback=%d",
            oboe::convertToText(output->getAudioApi()),oboe::OboeExtensions::isMMapUsed(input.get()),oboe::OboeExtensions::isMMapUsed(output.get()),input->getFramesPerBurst(),output->getFramesPerBurst(),output->getBufferSizeInFrames(),input->getBufferCapacityInFrames(),actualPreset,presetFallback);
        setSharedInputStream(input);setSharedOutputStream(output);setNumInputBurstsCushion(0);
        if(!recorder.start(raw,wet))return -102;
        mat_reset(engine);result=start();return int(result);
    }
    oboe::ResultWithValue<int32_t> readInput(int32_t n) override {
        // Only used by the debug baseline's original FullDuplex policy.
        // This counter is not reliable physical queue occupancy after overruns.
        auto available=input->getAvailableFrames();
        if(processingStarted && available){
            int queued=std::max(0,available.value()-n);
            pendingFrames.store(queued,std::memory_order_relaxed);
            if(queued>maximumPendingFrames.load(std::memory_order_relaxed))maximumPendingFrames.store(queued,std::memory_order_relaxed);
        }
        return oboe::FullDuplexStream::readInput(n);
    }
    oboe::DataCallbackResult render(void *out,int n){
        if(n<0||n>int(sourceBuffer.size())||(frameMode&&n!=480)){disconnected=true;return oboe::DataCallbackResult::Stop;}
        // Exactly one nonblocking read per callback. Pull all actually available
        // input into a preallocated FIFO, so an output stall cannot leave old
        // audio indefinitely in a large hardware capture buffer.
        auto read=input->read(discard.data(),discard.size(),0);
        if(!read){disconnected=true;return oboe::DataCallbackResult::Stop;}
        if(warmup){if(read.value()>0)--warmup;std::fill_n(static_cast<float*>(out),n,0);return oboe::DataCallbackResult::Continue;}
        int dropped=capture->push(discard.data(),read.value());
        int burst=std::max(input->getFramesPerBurst(),output->getFramesPerBurst());
        if(!processingStarted&&capture->size()<n+burst){std::fill_n(static_cast<float*>(out),n,0);return oboe::DataCallbackResult::Continue;}
        // Start at one burst of cushion. After startup, recover normal clock
        // drift gradually; hard trimming is reserved for scheduling outages.
        int limit=n+(processingStarted?2:1)*burst;
        dropped+=capture->discard(std::max(0,capture->size()-limit));
        if(dropped&&processingStarted)resyncFrames.fetch_add(dropped,std::memory_order_relaxed);
        double wanted=1+std::clamp((capture->size()-n-burst)*1e-6,-.0002,.0002);
        captureRatio+=.01*(wanted-captureRatio);ratioPpm.store(float((captureRatio-1)*1e6),std::memory_order_relaxed);
        int count=capture->popClocked(sourceBuffer.data(),n,captureRatio),queued=capture->size();
        pendingFrames.store(queued,std::memory_order_relaxed);
        if(queued>maximumPendingFrames.load(std::memory_order_relaxed))maximumPendingFrames.store(queued,std::memory_order_relaxed);
        return onBothStreamsReady(sourceBuffer.data(),count,out,n);
    }
    oboe::DataCallbackResult onAudioReady(oboe::AudioStream *s,void *out,int n) override {
        auto begin=std::chrono::steady_clock::now();bool active=processingStarted;
        bool trace=ATrace_isEnabled();if(trace)ATrace_beginSection("MAT audio callback");
        auto result=legacyPolicy?oboe::FullDuplexStream::onAudioReady(s,out,n):render(out,n);
        if(trace)ATrace_endSection();
        if(active){
            float ms=std::chrono::duration<float,std::milli>(std::chrono::steady_clock::now()-begin).count();
            if(ms>maximumMs.load(std::memory_order_relaxed))maximumMs.store(ms,std::memory_order_relaxed);
            // Exchange by the control reader requires CAS to avoid losing a peak.
            float old=recentMaximumMs.load(std::memory_order_relaxed);
            while(ms>old&&!recentMaximumMs.compare_exchange_weak(old,ms,std::memory_order_relaxed)){}
            histogram[std::min(127,int(ms*10))].fetch_add(1,std::memory_order_relaxed);
            callbacks.fetch_add(1,std::memory_order_relaxed);
            callbackPeriodMs.store(n/48.f,std::memory_order_relaxed);
            if(ms>n/48.f)lateCallbacks.fetch_add(1,std::memory_order_relaxed);
        }
        return result;
    }
    oboe::DataCallbackResult onBothStreamsReady(const void *in,int nIn,void *out,int nOut) override {
        processingStarted=true;
        auto *source=static_cast<const float *>(in);auto *dest=static_cast<float *>(out);
        bool trace=ATrace_isEnabled();if(trace)ATrace_beginSection("MAT DSP");
        int count=std::min(nIn,nOut);
        if(frameMode){
            // Same DSP frame API, without the arbitrary-size adapter's 10ms delay.
            // Never mix processing entry points on a running engine.
            if(count==480)mat_process_frame(engine,source,dest);
            else {
                std::array<float,480> frame{};if(count)std::copy_n(source,count,frame.data());
                mat_process_frame(engine,frame.data(),dest);underruns.fetch_add(1);
            }
            if(trace)ATrace_endSection();
            if(count)recorder.push(source,dest,count);
            if(count<480){std::array<float,480> zero{};recorder.push(zero.data(),dest+count,480-count);}
            if(!monitoring)std::fill_n(dest,480,0);
            return oboe::DataCallbackResult::Continue;
        }
        if(count)mat_process(engine,source,dest,count);
        if(count<nOut){
            underruns.fetch_add(1);std::array<float,480> zeros{};
            for(int i=count;i<nOut;){int n=std::min(480,nOut-i);mat_process(engine,zeros.data(),dest+i,n);i+=n;}
        }
        if(trace)ATrace_endSection();
        // Record exact callback timeline; zero-pad underruns in raw track as well.
        if(count)recorder.push(source,dest,count);
        if(count<nOut){std::array<float,480> zeros{};for(int i=count;i<nOut;){int n=std::min(480,nOut-i);recorder.push(zeros.data(),dest+i,n);i+=n;}}
        if(!monitoring)std::fill(dest,dest+nOut,0);
        return oboe::DataCallbackResult::Continue;
    }
    void stats(float *v){
        v[4]=maximumMs.load();v[5]=float(underruns.load());v[6]=float(recorder.dropped.load());v[7]=disconnected?1:0;v[8]=float(recorder.error.load());
        v[9]=recentMaximumMs.exchange(0);uint64_t total=0;std::array<uint64_t,128> h{};
        for(int i=0;i<128;++i){h[i]=histogram[i].load(std::memory_order_relaxed);total+=h[i];}
        uint64_t cumulative=0;for(int i=0;total&&i<128;++i){cumulative+=h[i];if(cumulative>=total-total/100){v[10]=i==127?v[4]:(i+1)*.1f;break;}}
        auto xr=output->getXRunCount();v[11]=xr?float(xr.value()):-1;
        v[12]=pendingFrames.load()/48.f;v[13]=maximumPendingFrames.load()/48.f;v[14]=float(resyncFrames.load());
        v[15]=float(callbacks.load());v[16]=float(lateCallbacks.load());
        // Timestamp queries stay on the control thread, outside audio callbacks.
        auto outLatency=output->calculateLatencyMillis(),inLatency=input->calculateLatencyMillis();
        v[17]=outLatency?float(outLatency.value()):-1;v[18]=inLatency?float(inLatency.value()):-1;
        v[19]=output->getFramesPerBurst()/48.f;v[20]=output->getBufferSizeInFrames()/48.f;
        v[21]=outputMmap?1:0;v[22]=inputMmap?1:0;
        v[23]=float(output->getAudioApi());v[24]=input->getFramesPerBurst()/48.f;
        v[25]=ratioPpm.load();v[26]=float(actualPreset);v[27]=presetFallback?1:0;
        v[28]=output->isPerformanceHintEnabled()?1:0;
        v[29]=float(output->getSharingMode());v[30]=float(output->getPerformanceMode());v[31]=float(input->getAudioApi());
        v[32]=(frameMode?mat_latency_samples(engine):mat_stream_latency_samples(engine))/48.f;v[33]=callbackPeriodMs.load();
    }
    void onErrorAfterClose(oboe::AudioStream *,oboe::Result) override {disconnected=true;}
    void close(){
        // close() waits for active callbacks; only then release recording worker.
        if(output){output->requestStop();output->close();output.reset();}
        if(input){input->requestStop();input->close();input.reset();}
        recorder.stop();
    }
};
struct Handle {
    mat_engine *engine;
    int mode;
    std::unique_ptr<Live> live;
    std::array<float,34> lastStats{};
    bool legacyPolicy=false,requireMmap=false;
    Handle(int m):engine(mat_create(48000,mat_mode(m))),mode(m){}
    ~Handle(){live.reset();mat_destroy(engine);}
};
static Handle *get(jlong value){return reinterpret_cast<Handle *>(value);}
struct JString {
    JNIEnv *env;jstring value;const char *text;
    JString(JNIEnv *e,jstring v):env(e),value(v),text(v?e->GetStringUTFChars(v,nullptr):nullptr){}
    ~JString(){if(text)env->ReleaseStringUTFChars(value,text);}
};
static bool parameters(JNIEnv *env,jfloatArray array,mat_parameters &p){
    if(!array || (env->GetArrayLength(array)!=8 && env->GetArrayLength(array)!=9))return false;
    float values[9]{};env->GetFloatArrayRegion(array,0,env->GetArrayLength(array),values);if(env->ExceptionCheck())return false;
    for(float v:values)if(!std::isfinite(v))return false;
    if(values[0]<0||values[0]>11||values[1]<0||values[1]>2)return false;
    p={int(values[0]),int(values[1]),values[2],values[3],values[4],values[5],values[6],values[7],values[8]};return true;
}
#define JNI_METHOD(name) Java_com_moon_mobileautotune_MobileAudio_##name
extern "C" {
JNIEXPORT jlong JNICALL JNI_METHOD(nCreate)(JNIEnv *,jclass,jint mode){try{auto h=std::make_unique<Handle>(mode);if(!h->engine)return 0;return reinterpret_cast<jlong>(h.release());}catch(...){return 0;}}
JNIEXPORT void JNICALL JNI_METHOD(nDestroy)(JNIEnv *,jclass,jlong h){delete get(h);}
JNIEXPORT jint JNICALL JNI_METHOD(nSet)(JNIEnv *env,jclass,jlong h,jfloatArray array){mat_parameters p{};return h && parameters(env,array,p)?mat_set_parameters(get(h)->engine,&p):-1;}
JNIEXPORT void JNICALL JNI_METHOD(nReset)(JNIEnv *,jclass,jlong h){if(h&&!get(h)->live)mat_reset(get(h)->engine);}
JNIEXPORT jint JNICALL JNI_METHOD(nProcess)(JNIEnv *env,jclass,jlong h,jfloatArray in,jfloatArray out,jint n){
    if(!h || get(h)->live || !in || !out || n<0 || env->GetArrayLength(in)<n || env->GetArrayLength(out)<n)return -1;
    if(env->IsSameObject(in,out)){auto *p=env->GetFloatArrayElements(in,nullptr);if(!p)return -1;int r=mat_process(get(h)->engine,p,p,n);env->ReleaseFloatArrayElements(in,p,0);return r;}
    auto *a=env->GetFloatArrayElements(in,nullptr);if(!a)return -1;
    auto *b=env->GetFloatArrayElements(out,nullptr);if(!b){env->ReleaseFloatArrayElements(in,a,JNI_ABORT);return -1;}
    int r=mat_process(get(h)->engine,a,b,n);env->ReleaseFloatArrayElements(in,a,JNI_ABORT);env->ReleaseFloatArrayElements(out,b,0);return r;
}
JNIEXPORT jint JNICALL JNI_METHOD(nLatency)(JNIEnv *,jclass,jlong h){return h?(get(h)->live&&get(h)->live->frameMode?mat_latency_samples(get(h)->engine):mat_stream_latency_samples(get(h)->engine)):0;}
JNIEXPORT jfloatArray JNICALL JNI_METHOD(nTelemetry)(JNIEnv *env,jclass,jlong h){
    if(!h)return nullptr;auto t=mat_get_telemetry(get(h)->engine);auto *l=get(h)->live.get();
    auto v=get(h)->lastStats;v[0]=t.input_pitch_hz;v[1]=t.target_pitch_hz;v[2]=t.correction_semitones;v[3]=t.pitch_confidence;if(l)l->stats(v.data());
    auto result=env->NewFloatArray(v.size());if(result)env->SetFloatArrayRegion(result,0,v.size(),v.data());return result;
}
JNIEXPORT jint JNICALL JNI_METHOD(nStart)(JNIEnv *env,jclass,jlong h,jstring raw,jstring wet,jboolean monitor,jint preset,jboolean compact){
    if(!h||get(h)->mode!=MAT_REALTIME||get(h)->live || (preset!=0&&preset!=6&&preset!=9&&preset!=10))return -1;
    JString a(env,raw),b(env,wet);if(!a.text||!b.text||std::string(a.text)==b.text)return -1;
    try{auto l=std::make_unique<Live>(get(h)->engine);l->monitoring=monitor;l->requestedPreset=preset;l->requestedCallback=compact?480:0;l->legacyPolicy=get(h)->legacyPolicy;l->requireMmap=get(h)->requireMmap;int r=l->open(a.text,b.text);if(!r)get(h)->live=std::move(l);return r;}catch(...){return -2;}
}
JNIEXPORT void JNICALL JNI_METHOD(nStop)(JNIEnv *,jclass,jlong h){if(h&&get(h)->live){auto *handle=get(h);handle->live->stats(handle->lastStats.data());handle->live->close();handle->lastStats[8]=float(handle->live->recorder.error.load());handle->live.reset();}}
#ifdef MAT_ANDROID_DIAGNOSTICS
// Diagnostic policy applies only to this process, never system properties.
JNIEXPORT jint JNICALL Java_com_moon_mobileautotune_demo_LivePerformanceInstrumentation_nMmap(JNIEnv *,jclass,jlong h,jint policy){
    if(h&&get(h)->live)return -1;
    void *library=dlopen("libaaudio.so",RTLD_NOW);
    auto setPolicy=library?reinterpret_cast<int(*)(int)>(dlsym(library,"AAudio_setMMapPolicy")):nullptr;
    int result=setPolicy?setPolicy(policy):int(oboe::Result::ErrorUnimplemented);
    if(!result&&h)get(h)->requireMmap=policy==3;
    if(library)dlclose(library);return result;
}
// Debug test hook; absent from the release AAR. Does not change production API.
JNIEXPORT void JNICALL Java_com_moon_mobileautotune_demo_LivePerformanceInstrumentation_nLegacy(JNIEnv *,jclass,jlong h,jboolean enabled){if(h&&!get(h)->live)get(h)->legacyPolicy=enabled;}
#endif
JNIEXPORT jint JNICALL JNI_METHOD(nWav)(JNIEnv *env,jclass,jstring input,jstring output,jfloatArray array,jint mode,jboolean tail){
    mat_parameters p{};if(!parameters(env,array,p))return -1;JString a(env,input),b(env,output);
    return a.text&&b.text?mat_process_wav(a.text,b.text,&p,mat_mode(mode),tail,nullptr,nullptr):-1;
}
}
