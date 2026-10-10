#ifndef MOBILEAUTOTUNE_SRC_PITCH_HPP_
#define MOBILEAUTOTUNE_SRC_PITCH_HPP_
#include <algorithm>
#include <array>
#include <cmath>

namespace mobileautotune {

// Causal YIN (de Cheveigne/Kawahara): cumulative normalized difference,
// first periodic minimum, parabolic refinement, confidence and energy gate.
// Analysis at 12kHz; two-pole low-pass precedes decimation.
class PitchTracker {
 public:
  float hz() const { return hz_; }
  float confidence() const { return confidence_; }
  explicit PitchTracker(bool offline) : window_size_(offline ? 1024 : 384) {}
  void Reset() {
    ring_.fill(0);
    write_position_ = filled_samples_ = decimation_ = 0;
    lowpass_first_ = lowpass_second_ = hz_ = confidence_ = 0;
  }
  void Feed(const float* x, int n) {
    for (int i = 0; i < n; ++i) {
      lowpass_first_ += 0.24f * (x[i] - lowpass_first_);
      lowpass_second_ += 0.24f * (lowpass_first_ - lowpass_second_);
      if (++decimation_ == 4) {
        decimation_ = 0;
        ring_[write_position_] = lowpass_second_;
        write_position_ = (write_position_ + 1) % window_size_;
        filled_samples_ = std::min(filled_samples_ + 1, window_size_);
      }
    }
  }
  void Analyze() {
    hz_ = confidence_ = 0;
    if (filled_samples_ < window_size_) {
      return;
    }
    std::array<float, 1024> x{};
    float energy = 0, mean = 0;
    for (int i = 0; i < window_size_; ++i) {
      x[i] = ring_[(write_position_ + i) % window_size_];
      mean += x[i];
    }
    mean /= window_size_;
    for (int i = 0; i < window_size_; ++i) {
      x[i] -= mean;
      energy += x[i] * x[i];
    }
    if (energy / window_size_ < 1e-7f) {
      return;
    }
    int max_lag = std::min(window_size_ / 2 - 2, 184),
        count = window_size_ - max_lag;
    float sum = 0;
    difference_[0] = 1;
    for (int lag = 1; lag <= max_lag; ++lag) {
      float d = 0;
      for (int i = 0; i < count; ++i) {
        float v = x[i] - x[i + lag];
        d += v * v;
      }
      sum += d;
      difference_[lag] = sum > 1e-12f ? d * lag / sum : 1;
    }
    int best = 0;
    for (int lag = 12; lag < max_lag - 1; ++lag) {
      if (difference_[lag] < 0.18f) {
        while (lag + 1 < max_lag && difference_[lag + 1] < difference_[lag]) {
          ++lag;
        }
        best = lag;
        break;
      }
    }
    if (!best) {
      return;
    }
    float a = difference_[best - 1], b = difference_[best],
          c = difference_[best + 1];
    float denominator = a - 2 * b + c;
    float offset =
        std::abs(denominator) > 1e-9f ? 0.5f * (a - c) / denominator : 0;
    offset = std::max(-0.5f, std::min(0.5f, offset));
    hz_ = 12000.0f / (best + offset);
    confidence_ = 1 - b;
  }

 private:
  float hz_ = 0;
  float confidence_ = 0;
  std::array<float, 1024> ring_{}, difference_{};
  int write_position_ = 0, filled_samples_ = 0, decimation_ = 0, window_size_;
  float lowpass_first_ = 0, lowpass_second_ = 0;
};
inline float NearestScaleNote(float midi, int tonic, int scale) {
  static constexpr int kMajor[]{0, 2, 4, 5, 7, 9, 11};
  static constexpr int kMinor[]{0, 2, 3, 5, 7, 8, 10};
  float best = std::round(midi), distance = 1e9f;
  for (int n = int(std::floor(midi)) - 12; n <= int(std::ceil(midi)) + 12;
       ++n) {
    int pc = ((n - tonic) % 12 + 12) % 12;
    bool allowed = scale == 2;
    for (int k = 0; k < 7; ++k) {
      allowed |= pc == (scale == 1 ? kMinor[k] : kMajor[k]);
    }
    if (allowed && std::abs(n - midi) < distance) {
      distance = std::abs(n - midi);
      best = float(n);
    }
  }
  return best;
}

}  // namespace mobileautotune

#endif  // MOBILEAUTOTUNE_SRC_PITCH_HPP_
