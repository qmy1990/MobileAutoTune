#include <oboe/Oboe.h>
#include <oboe/OboeExtensions.h>
#include <memory>
#include <cstdio>
#include <cstring>
#include <thread>
#include <chrono>
#include <dlfcn.h>
struct Callback:oboe::AudioStreamDataCallback{
 oboe::DataCallbackResult onAudioReady(oboe::AudioStream *s,void *p,int n)override{memset(p,0,n*s->getBytesPerFrame());return oboe::DataCallbackResult::Continue;}
};
int main(int argc,char **argv){Callback cb;auto usage=argc>1?oboe::Usage(std::atoi(argv[1])):oboe::Usage::Media;
 printf("mmap_supported=%d mmap_enabled=%d usage=%d\n",oboe::OboeExtensions::isMMapSupported(),oboe::OboeExtensions::isMMapEnabled(),int(usage));
 if(argc>2){void *lib=dlopen("libaaudio.so",RTLD_NOW);auto policy=reinterpret_cast<int(*)(int)>(dlsym(lib,"AAudio_setMMapPolicy"));if(policy)printf("force_mmap_result=%d\n",policy(3));}
 for(int channel:{1,2})for(auto format:{oboe::AudioFormat::Float,oboe::AudioFormat::I16})for(auto sharing:{oboe::SharingMode::Exclusive,oboe::SharingMode::Shared}){
  oboe::AudioStreamBuilder b;std::shared_ptr<oboe::AudioStream>s;
  b.setDirection(oboe::Direction::Output)->setChannelCount(channel)->setFormat(format)->setSampleRate(48000)->setPerformanceMode(oboe::PerformanceMode::LowLatency)->setSharingMode(sharing)->setDataCallback(&cb)->setUsage(usage);
  if(argc>3)b.setPackageName(argv[3]);
  if(argc>4)b.setDeviceId(std::atoi(argv[4]));
  auto r=b.openStream(s);
  if(r!=oboe::Result::OK){printf("open %d %d %d error %d\n",channel,int(format),int(sharing),int(r));continue;}
  s->setBufferSizeInFrames(s->getFramesPerBurst()*2);s->requestStart();std::this_thread::sleep_for(std::chrono::seconds(2));
  auto latency=s->calculateLatencyMillis();auto xr=s->getXRunCount();
  printf("channels=%d format=%d requestedSharing=%d actualSharing=%d api=%d mmap=%d burst=%d size=%d capacity=%d latency_ms=%.3f xruns=%d\n",channel,int(format),int(sharing),int(s->getSharingMode()),int(s->getAudioApi()),oboe::OboeExtensions::isMMapUsed(s.get()),s->getFramesPerBurst(),s->getBufferSizeInFrames(),s->getBufferCapacityInFrames(),latency?latency.value():-1,xr?xr.value():-1);fflush(stdout);s->requestStop();s->close();
 }
}
