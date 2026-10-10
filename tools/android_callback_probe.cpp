#include <oboe/Oboe.h>
#include <oboe/OboeExtensions.h>

#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <memory>
#include <thread>
struct Callback : oboe::AudioStreamDataCallback {
  std::atomic<int> calls{0}, last_frames{0};
  oboe::DataCallbackResult onAudioReady(oboe::AudioStream* s, void* p,
                                        int n) override {
    memset(p, 0, n * s->getBytesPerFrame());
    calls++;
    last_frames = n;
    return oboe::DataCallbackResult::Continue;
  }
};
int main() {
  for (int requested : {0, 480}) {
    Callback c;
    oboe::AudioStreamBuilder b;
    std::shared_ptr<oboe::AudioStream> s;
    b.setDirection(oboe::Direction::Output)
        ->setChannelCount(1)
        ->setFormat(oboe::AudioFormat::Float)
        ->setSampleRate(48000)
        ->setPerformanceMode(oboe::PerformanceMode::LowLatency)
        ->setSharingMode(oboe::SharingMode::Exclusive)
        ->setUsage(oboe::Usage::Media)
        ->setDataCallback(&c)
        ->setPackageName("com.mobileautotune.demo");
    if (requested) {
      b.setFramesPerDataCallback(requested);
    }
    auto r = b.openStream(s);
    if (r != oboe::Result::OK) {
      printf("open_error=%d\n", int(r));
      continue;
    }
    s->setBufferSizeInFrames(2 * s->getFramesPerBurst());
    s->requestStart();
    std::this_thread::sleep_for(std::chrono::seconds(4));
    auto l = s->calculateLatencyMillis();
    auto x = s->getXRunCount();
    printf(
        "requested=%d actualCallback=%d lastFrames=%d burst=%d size=%d "
        "latency_ms=%.3f xruns=%d calls=%d mmap=%d\n",
        requested, s->getFramesPerDataCallback(), c.last_frames.load(),
        s->getFramesPerBurst(), s->getBufferSizeInFrames(), l ? l.value() : -1,
        x ? x.value() : -1, c.calls.load(),
        oboe::OboeExtensions::isMMapUsed(s.get()));
    fflush(stdout);
    s->requestStop();
    s->close();
  }
}
