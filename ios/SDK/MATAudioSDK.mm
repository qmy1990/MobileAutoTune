#import "MATAudioSDK.h"
#import <AVFoundation/AVFoundation.h>
#import <AudioToolbox/AudioToolbox.h>
#include "mat/mobile_audio.h"
#include "mat/offline.h"
#include "recorder.hpp"
#include <memory>
#include <atomic>
#include <array>
#include <chrono>
#include <os/workgroup.h>

static BOOL failure(NSError **error,NSInteger code,NSString *message){if(error)*error=[NSError errorWithDomain:@"MobileAutoTune" code:code userInfo:@{NSLocalizedDescriptionKey:message}];return NO;}
static mat_parameters convert(MATAudioParameters *p){return {int(p.tonic),int(p.scale),p.tuning,p.reverb,p.echo,p.denoise,p.echoMs,p.concertAHz,p.pitchShiftSemitones};}
@implementation MATAudioParameters
- (instancetype)init{if((self=[super init])){auto p=mat_default_parameters();_tonic=p.tonic;_scale=p.scale;_tuning=p.tuning;_reverb=p.reverb;_echo=p.echo;_denoise=p.denoise;_echoMs=p.echo_ms;_concertAHz=p.concert_a_hz;_pitchShiftSemitones=p.pitch_shift_semitones;}return self;}
- (id)copyWithZone:(NSZone *)zone{MATAudioParameters *p=[[MATAudioParameters allocWithZone:zone] init];p.tonic=_tonic;p.scale=_scale;p.tuning=_tuning;p.reverb=_reverb;p.echo=_echo;p.denoise=_denoise;p.echoMs=_echoMs;p.concertAHz=_concertAHz;p.pitchShiftSemitones=_pitchShiftSemitones;return p;}
@end
struct AudioState {
    mat_engine *engine;
    bool offline,monitoring=false,workgroupAvailable=false;
    AudioUnit unit=nullptr;
    std::array<float,4096> input{};
    Recorder recorder;
    std::atomic<float> maximumMs{0},callbackPeriodMs{0};
    std::array<std::atomic<uint64_t>,128> histogram{};
    std::atomic<uint64_t> callbacks{0},lateCallbacks{0},timestampGapSamples{0};
    double expectedSampleTime=0;bool haveSampleTime=false;
    std::atomic<int> renderError{0};
    explicit AudioState(bool mode):engine(mat_create(48000,mode?MAT_OFFLINE:MAT_REALTIME)),offline(mode){}
    ~AudioState(){mat_destroy(engine);}
};
static OSStatus render(void *context,AudioUnitRenderActionFlags *flags,const AudioTimeStamp *time,UInt32,UInt32 frames,AudioBufferList *output){
    auto *s=static_cast<AudioState *>(context);auto begin=std::chrono::steady_clock::now();
    if(frames>s->input.size() || output->mNumberBuffers!=1 || output->mBuffers[0].mDataByteSize<frames*sizeof(float)){
        s->renderError=-1;for(UInt32 i=0;i<output->mNumberBuffers;++i)memset(output->mBuffers[i].mData,0,output->mBuffers[i].mDataByteSize);return noErr;
    }
    if(time->mFlags & kAudioTimeStampSampleTimeValid){
        if(s->haveSampleTime && time->mSampleTime>s->expectedSampleTime+.5)
            s->timestampGapSamples.fetch_add(uint64_t(time->mSampleTime-s->expectedSampleTime+.5),std::memory_order_relaxed);
        s->expectedSampleTime=time->mSampleTime+frames;s->haveSampleTime=true;
    }
    AudioBufferList in{1,{{1,frames*UInt32(sizeof(float)),s->input.data()}}};
    OSStatus status=AudioUnitRender(s->unit,flags,time,1,frames,&in);
    if(status){s->renderError=status;std::fill(s->input.begin(),s->input.begin()+frames,0);}
    auto *destination=static_cast<float *>(output->mBuffers[0].mData);
    mat_process(s->engine,s->input.data(),destination,int(frames));
    s->recorder.push(s->input.data(),destination,int(frames));
    if(!s->monitoring)std::fill(destination,destination+frames,0);
    float ms=std::chrono::duration<float,std::milli>(std::chrono::steady_clock::now()-begin).count();
    if(ms>s->maximumMs.load(std::memory_order_relaxed))s->maximumMs.store(ms,std::memory_order_relaxed);
    s->histogram[std::min(127,int(ms*10))].fetch_add(1,std::memory_order_relaxed);
    s->callbacks.fetch_add(1,std::memory_order_relaxed);
    s->callbackPeriodMs.store(frames/48.f,std::memory_order_relaxed);
    if(ms>frames/48.f)s->lateCallbacks.fetch_add(1,std::memory_order_relaxed);
    return noErr;
}
@implementation MATAudioSDK {
    std::unique_ptr<AudioState> _state;
    id _interruptionObserver;
    id _routeObserver;
    id _resetObserver;
    BOOL _running;
}
- (instancetype)initWithOffline:(BOOL)offline{
    if((self=[super init])){
        try{_state=std::make_unique<AudioState>(offline);}catch(...){return nil;}
        if(!_state->engine)return nil;
        __weak MATAudioSDK *weakSelf=self;
        auto center=NSNotificationCenter.defaultCenter;
        _interruptionObserver=[center addObserverForName:AVAudioSessionInterruptionNotification object:nil queue:NSOperationQueue.mainQueue usingBlock:^(NSNotification *n){
            if([n.userInfo[AVAudioSessionInterruptionTypeKey] unsignedIntegerValue]==AVAudioSessionInterruptionTypeBegan)[weakSelf stopLive];
        }];
        _routeObserver=[center addObserverForName:AVAudioSessionRouteChangeNotification object:nil queue:NSOperationQueue.mainQueue usingBlock:^(NSNotification *n){
            // Our own category setup emits category-change notifications.
            // Stop only an established stream when its device route changes.
            auto reason=[n.userInfo[AVAudioSessionRouteChangeReasonKey] unsignedIntegerValue];
            if(weakSelf.running && reason!=AVAudioSessionRouteChangeReasonCategoryChange)[weakSelf stopLive];
        }];
        _resetObserver=[center addObserverForName:AVAudioSessionMediaServicesWereResetNotification object:nil queue:NSOperationQueue.mainQueue usingBlock:^(NSNotification *){[weakSelf stopLive];}];
    }return self;
}
- (BOOL)setParameters:(MATAudioParameters *)p error:(NSError **)error{if(p.tonic<0||p.tonic>11||p.scale<0||p.scale>2)return failure(error,-1,@"Invalid tonic/scale");auto parameters=convert(p);if(mat_set_parameters(_state->engine,&parameters))return failure(error,-1,@"Invalid audio parameters");return YES;}
- (BOOL)processInput:(const float *)input output:(float *)output frames:(NSInteger)frames{if(_running||frames<0||frames>INT_MAX)return NO;return mat_process(_state->engine,input,output,int(frames))==0;}
- (void)reset{if(!_running)mat_reset(_state->engine);}
- (NSInteger)latencySamples{return mat_stream_latency_samples(_state->engine);}
- (BOOL)running{return _running;}
- (NSDictionary *)telemetry{
    auto t=mat_get_telemetry(_state->engine);AVAudioSession *session=AVAudioSession.sharedInstance;
    std::array<uint64_t,128> histogram{};uint64_t total=0,cumulative=0;float p99=0;
    for(int i=0;i<128;++i){histogram[i]=_state->histogram[i].load(std::memory_order_relaxed);total+=histogram[i];}
    for(int i=0;total && i<128;++i){cumulative+=histogram[i];if(cumulative>=total-total/100){p99=i==127?_state->maximumMs.load():(i+1)*.1f;break;}}
    return @{@"pitchHz":@(t.input_pitch_hz),@"targetHz":@(t.target_pitch_hz),@"semitones":@(t.correction_semitones),@"confidence":@(t.pitch_confidence),@"callbackMaxMs":@(_state->maximumMs.load()),@"droppedSamples":@(_state->recorder.dropped.load()),@"renderError":@(_state->renderError.load()),@"diskError":@(_state->recorder.error.load()),@"ioBufferMs":@(session.IOBufferDuration*1000),@"inputLatencyMs":@(session.inputLatency*1000),@"outputLatencyMs":@(session.outputLatency*1000),@"audioWorkgroupAvailable":@(_state->workgroupAvailable),@"processedSamples":@(t.processed_frames),@"callbacks":@(_state->callbacks.load()),@"callbackP99UpperMs":@(p99),@"callbackPeriodMs":@(_state->callbackPeriodMs.load()),@"callbackOverPeriod":@(_state->lateCallbacks.load()),@"timestampGapSamples":@(_state->timestampGapSamples.load())};
}
- (BOOL)startLiveRawURL:(NSURL *)raw processedURL:(NSURL *)wet monitor:(BOOL)monitor error:(NSError **)error{
    if(_state->offline||_running||!raw.isFileURL||!wet.isFileURL||[raw.path isEqual:wet.path])return failure(error,-1,@"Invalid live session or paths");
    AVAudioSession *session=AVAudioSession.sharedInstance;
    if(session.recordPermission!=AVAudioSessionRecordPermissionGranted)return failure(error,-2,@"Microphone permission is required");
    if(![session setCategory:AVAudioSessionCategoryPlayAndRecord mode:AVAudioSessionModeMeasurement options:AVAudioSessionCategoryOptionDefaultToSpeaker error:error] || ![session setPreferredSampleRate:48000 error:error] || ![session setPreferredIOBufferDuration:128.0/48000 error:error] || ![session setActive:YES error:error])return NO;
    if(fabs(session.sampleRate-48000)>1){[session setActive:NO error:nil];return failure(error,-3,@"This route is not 48kHz; select a wired/USB audio route");}
    AudioComponentDescription desc{kAudioUnitType_Output,kAudioUnitSubType_RemoteIO,kAudioUnitManufacturer_Apple,0,0};
    auto component=AudioComponentFindNext(nullptr,&desc);OSStatus status=component?AudioComponentInstanceNew(component,&_state->unit):-1;
    UInt32 enable=1,maximum=4096;
    AudioStreamBasicDescription format{48000,kAudioFormatLinearPCM,kAudioFormatFlagIsFloat|kAudioFormatFlagIsPacked,4,1,4,1,32,0};
    AURenderCallbackStruct callback{render,_state.get()};
    if(!status)status=AudioUnitSetProperty(_state->unit,kAudioOutputUnitProperty_EnableIO,kAudioUnitScope_Input,1,&enable,sizeof(enable));
    if(!status)status=AudioUnitSetProperty(_state->unit,kAudioUnitProperty_StreamFormat,kAudioUnitScope_Output,1,&format,sizeof(format));
    if(!status)status=AudioUnitSetProperty(_state->unit,kAudioUnitProperty_StreamFormat,kAudioUnitScope_Input,0,&format,sizeof(format));
    if(!status)status=AudioUnitSetProperty(_state->unit,kAudioUnitProperty_MaximumFramesPerSlice,kAudioUnitScope_Global,0,&maximum,sizeof(maximum));
    if(!status)status=AudioUnitSetProperty(_state->unit,kAudioUnitProperty_SetRenderCallback,kAudioUnitScope_Input,0,&callback,sizeof(callback));
    if(!status)status=AudioUnitInitialize(_state->unit);
    _state->workgroupAvailable=false;
    // iOS14+ automatically joins RemoteIO's render thread to the device's
    // audio workgroup. All DSP stays there; the disk worker must not join it.
    // Query on the control thread for diagnostics; lack of support is harmless.
    if(!status){
        os_workgroup_t group=nullptr;UInt32 size=sizeof(group);
        OSStatus info=AudioUnitGetProperty(_state->unit,kAudioOutputUnitProperty_OSWorkgroup,kAudioUnitScope_Global,0,&group,&size);
        _state->workgroupAvailable=info==noErr && group!=nullptr;
        // The strong local consumes the returned +1 reference under ARC.
    }
    if(!status && !_state->recorder.start(raw.fileSystemRepresentation,wet.fileSystemRepresentation))status=-4;
    mat_reset(_state->engine);_state->monitoring=monitor;_state->renderError=0;_state->maximumMs=0;
    _state->callbacks=0;_state->lateCallbacks=0;_state->timestampGapSamples=0;_state->callbackPeriodMs=0;_state->haveSampleTime=false;
    for(auto &bin:_state->histogram)bin.store(0,std::memory_order_relaxed);
    if(!status)status=AudioOutputUnitStart(_state->unit);
    if(status){[self stopLive];[session setActive:NO error:nil];return failure(error,status,@"Cannot start RemoteIO audio or recording");}
    _running=YES;return YES;
}
- (void)stopLive{
    if(!_state)return;
    if(_state->unit){AudioOutputUnitStop(_state->unit);AudioUnitUninitialize(_state->unit);AudioComponentInstanceDispose(_state->unit);_state->unit=nullptr;}
    _state->recorder.stop();BOOL wasRunning=_running;_running=NO;
    if(wasRunning)[AVAudioSession.sharedInstance setActive:NO withOptions:AVAudioSessionSetActiveOptionNotifyOthersOnDeactivation error:nil];
}
+ (BOOL)processWavInputURL:(NSURL *)input outputURL:(NSURL *)output parameters:(MATAudioParameters *)p offline:(BOOL)offline appendTail:(BOOL)tail error:(NSError **)error{
    if(!input.isFileURL||!output.isFileURL||p.tonic<0||p.tonic>11||p.scale<0||p.scale>2)return failure(error,-1,@"Invalid file URLs/parameters");
    auto parameters=convert(p);int result=mat_process_wav(input.fileSystemRepresentation,output.fileSystemRepresentation,&parameters,offline?MAT_OFFLINE:MAT_REALTIME,tail,nullptr,nullptr);
    if(result)return failure(error,result,@"WAV processing failed; use 48kHz mono/stereo PCM16 or float32 WAV");return YES;
}
- (void)dealloc{[self stopLive];auto center=NSNotificationCenter.defaultCenter;if(_interruptionObserver)[center removeObserver:_interruptionObserver];if(_routeObserver)[center removeObserver:_routeObserver];if(_resetObserver)[center removeObserver:_resetObserver];}
@end
