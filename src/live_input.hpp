#ifndef MOBILEAUTOTUNE_SRC_LIVE_INPUT_HPP_
#define MOBILEAUTOTUNE_SRC_LIVE_INPUT_HPP_
#include <algorithm>
#include <vector>

namespace mobileautotune {

// Single audio-thread bounded capture FIFO. No counters from the hardware:
// legacy AAudio's written-read difference can include already lost frames.
class LiveInput {
 public:
  explicit LiveInput(int capacity) : data_(std::max(1, capacity), 0) {}
  int size() const { return count_; }
  int Discard(int n) {
    n = std::clamp(n, 0, count_);
    head_ = (head_ + n) % int(data_.size());
    count_ -= n;
    return n;
  }
  int Push(const float* p, int n) {
    int lost = 0;
    if (n > int(data_.size())) {
      lost = n - int(data_.size());
      p += lost;
      n -= lost;
    }
    lost += Discard(std::max(0, count_ + n - int(data_.size())));
    int tail = (head_ + count_) % int(data_.size());
    for (int i = 0; i < n; ++i) {
      data_[tail] = p[i];
      if (++tail == int(data_.size())) {
        tail = 0;
      }
    }
    count_ += n;
    return lost;
  }
  int Pop(float* p, int n) {
    n = std::min(n, count_);
    for (int i = 0; i < n; ++i) {
      p[i] = data_[head_];
      if (++head_ == int(data_.size())) {
        head_ = 0;
      }
    }
    count_ -= n;
    return n;
  }
  // Fractional clock recovery, continuous across callbacks. Ratio is limited
  // by the caller to +/-200ppm (<0.004 semitone). Unity with zero phase is an
  // exact copy. Requires one lookahead sample only when interpolating.
  int PopClocked(float* p, int n, double ratio) {
    int produced = 0;
    while (produced < n && count_) {
      if (phase_ > 0 && count_ < 2) {
        break;
      }
      float x = data_[head_];
      if (phase_ > 0) {
        int next = head_ + 1;
        if (next == int(data_.size())) {
          next = 0;
        }
        x += float(phase_) * (data_[next] - x);
      }
      p[produced++] = x;
      phase_ += ratio;
      int advance = int(phase_);
      phase_ -= advance;
      Discard(advance);
    }
    return produced;
  }

 private:
  std::vector<float> data_;
  int head_ = 0, count_ = 0;
  double phase_ = 0;
};

}  // namespace mobileautotune

#endif  // MOBILEAUTOTUNE_SRC_LIVE_INPUT_HPP_
