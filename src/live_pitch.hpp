#ifndef MOBILEAUTOTUNE_SRC_LIVE_PITCH_HPP_
#define MOBILEAUTOTUNE_SRC_LIVE_PITCH_HPP_
#include <algorithm>
#include <array>
#include <cmath>

namespace mobileautotune {
// Pitch-synchronous dual read heads, periodic Hann crossfade. At a stable F0,
// each reset spans two pitch periods so harmonics meet at the same phase.
// Bounded 20ms history; low voices (<100Hz) use the maximum window.
class LivePitchShifter {
 public:
  static constexpr int kLatency = 480;
  static constexpr int kMaximumDelay = 960;
  void Reset() {
    ring_.fill(0);
    position_ = 0;
    phase_ = 0;
    span_ = 960;
  }
  void Process(const float* input, float* output, int count, float semitones,
               float hz) {
    float ratio = std::exp2(semitones / 12.f);
    float wanted = hz > 0 ? std::clamp(96000.f / hz, 160.f, 960.f) : span_;
    for (int i = 0; i < count; ++i) {
      ring_[position_] = input[i];
      span_ += 0.001f * (wanted - span_);
      float second = phase_ + 0.5f;
      if (second >= 1) {
        second -= 1;
      }
      float w = 0.5f - 0.5f * std::cos(6.28318530718f * phase_);
      output[i] = w * Read(kLatency + (phase_ - 0.5f) * span_) +
                  (1 - w) * Read(kLatency + (second - 0.5f) * span_);
      phase_ += (1 - ratio) / span_;
      phase_ -= std::floor(phase_);
      position_ = (position_ + 1) & 2047;
    }
  }

 private:
  std::array<float, 2048> ring_{};
  int position_ = 0;
  float phase_ = 0, span_ = 960;
  float Read(float delay) const {
    float p = position_ - delay;
    while (p < 0) {
      p += 2048;
    }
    int a = int(p);
    float f = p - a;
    return ring_[a] * (1 - f) + ring_[(a + 1) & 2047] * f;
  }
};

}  // namespace mobileautotune

#endif  // MOBILEAUTOTUNE_SRC_LIVE_PITCH_HPP_
