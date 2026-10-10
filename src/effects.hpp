#ifndef MOBILEAUTOTUNE_SRC_EFFECTS_HPP_
#define MOBILEAUTOTUNE_SRC_EFFECTS_HPP_
#include <algorithm>
#include <array>
#include <cmath>
#include <vector>

namespace mobileautotune {
class Delay {
 public:
  explicit Delay(size_t size) : data_(std::max<size_t>(1, size), 0) {}
  void Reset() {
    std::fill(data_.begin(), data_.end(), 0);
    position_ = 0;
  }
  float Tick(float x) {
    float y = data_[position_];
    data_[position_] = x;
    if (++position_ == data_.size()) {
      position_ = 0;
    }
    return y;
  }

 private:
  std::vector<float> data_;
  size_t position_ = 0;
};
class Effects {
 public:
  Effects() : echo_buffer_(38402, 0) {
    for (int k = 0; k < 8; ++k) {
      lines_[k].resize(kLengths[k], 0);
      feedback_gains_[k] = std::pow(0.001f, kLengths[k] / (48000.f * 1.8f));
    }
  }
  void Reset() {
    for (auto& l : lines_) {
      std::fill(l.begin(), l.end(), 0);
    }
    positions_.fill(0);
    filters_.fill(0);
    std::fill(echo_buffer_.begin(), echo_buffer_.end(), 0);
    echo_position_ = 0;
    echo_delay_ = 13440;
  }
  float Tick(float x, float reverb, float echo, float delay_ms) {
    // Orthogonal Householder feedback matrix, damped for a smooth diffuse tail.
    float sum = 0;
    std::array<float, 8> reads{};
    for (int k = 0; k < 8; ++k) {
      reads[k] = lines_[k][positions_[k]];
      filters_[k] += 0.30f * (reads[k] - filters_[k]);
      sum += filters_[k];
    }
    float wet = 0;
    for (int k = 0; k < 8; ++k) {
      float feedback = feedback_gains_[k];
      lines_[k][positions_[k]] =
          0.22f * x + feedback * (filters_[k] - 0.25f * sum);
      wet += (k & 1 ? -1 : 1) * reads[k];
      if (++positions_[k] == kLengths[k]) {
        positions_[k] = 0;
      }
    }
    // Fractional echo delay; bounded read-pointer motion avoids abrupt clicks.
    float wanted = delay_ms * 48.f;
    echo_delay_ += std::max(-0.02f, std::min(0.02f, wanted - echo_delay_));
    float p = echo_position_ - echo_delay_;
    if (p < 0) {
      p += echo_buffer_.size();
    }
    int a = int(p);
    float f = p - a;
    float e = echo_buffer_[a] * (1 - f) +
              echo_buffer_[(a + 1) % echo_buffer_.size()] * f;
    echo_buffer_[echo_position_] = x + 0.42f * e;
    if (++echo_position_ == int(echo_buffer_.size())) {
      echo_position_ = 0;
    }
    float y = x + 0.32f * reverb * wet + 0.65f * echo * e;
    // Transparent below -2 dBFS, instantaneous bounded safety soft knee.
    float magnitude = std::abs(y);
    if (magnitude > 0.8f) {
      y = std::copysign(0.8f + 0.2f * std::tanh((magnitude - 0.8f) / 0.2f), y);
    }
    return y;
  }

 private:
  static constexpr std::array<int, 8> kLengths{1493, 1601, 1867, 2053,
                                               2381, 2671, 2999, 3253};
  std::array<std::vector<float>, 8> lines_;
  std::array<int, 8> positions_{};
  std::array<float, 8> filters_{}, feedback_gains_{};
  std::vector<float> echo_buffer_;
  int echo_position_ = 0;
  float echo_delay_ = 13440;
};

}  // namespace mobileautotune

#endif  // MOBILEAUTOTUNE_SRC_EFFECTS_HPP_
