#ifndef MOBILEAUTOTUNE_SRC_RECORDER_HPP_
#define MOBILEAUTOTUNE_SRC_RECORDER_HPP_
#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <memory>
#include <thread>

namespace mobileautotune {
// One audio producer, one disk consumer. Disk I/O never runs in the callback.
// Stop the audio stream before Stop(). Files retain pipeline latency for
// monitoring recordings; use the raw_file_ WAV with mat_process_wav for aligned
// offline export.
class Recorder {
 public:
  std::atomic<uint64_t> dropped_{0};
  std::atomic<int> error_{0};
  bool Start(const char* raw_path, const char* wet_path) {
    if (worker_.joinable()) {
      return false;
    }
    raw_file_ = fopen(raw_path, "wb");
    wet_file_ = fopen(wet_path, "wb");
    if (!raw_file_ || !wet_file_) {
      if (raw_file_) {
        fclose(raw_file_);
      }
      if (wet_file_) {
        fclose(wet_file_);
      }
      raw_file_ = wet_file_ = nullptr;
      return false;
    }
    WriteHeader(raw_file_, 0);
    WriteHeader(wet_file_, 0);
    write_position_ = read_position_ = 0;
    done_ = false;
    sample_count_ = 0;
    dropped_ = 0;
    error_ = 0;
    worker_ = std::thread([this] {
      std::array<unsigned char, 2048> a{}, b{};
      while (!done_.load() || read_position_.load() != write_position_.load()) {
        uint32_t r = read_position_.load(std::memory_order_relaxed),
                 w = write_position_.load(std::memory_order_acquire);
        uint32_t n = std::min<uint32_t>(1024, w - r);
        if (!n) {
          std::this_thread::sleep_for(std::chrono::milliseconds(2));
          continue;
        }
        for (uint32_t i = 0; i < n; ++i) {
          auto value = ring_[(r + i) & (kCapacity - 1)];
          int16_t x = ToPcm(value.raw), y = ToPcm(value.wet);
          a[2 * i] = x & 255;
          a[2 * i + 1] = (uint16_t(x) >> 8) & 255;
          b[2 * i] = y & 255;
          b[2 * i + 1] = (uint16_t(y) >> 8) & 255;
        }
        if (sample_count_ + n > (UINT32_MAX - 36) / 2) {
          error_ = 1;
        } else if (!error_.load()) {
          if (fwrite(a.data(), 2, n, raw_file_) != n ||
              fwrite(b.data(), 2, n, wet_file_) != n) {
            error_ = 1;
          }
          sample_count_ += n;
        }
        read_position_.store(r + n, std::memory_order_release);
      }
    });
    return true;
  }
  void Push(const float* input, const float* output, int n) {
    uint32_t w = write_position_.load(std::memory_order_relaxed),
             r = read_position_.load(std::memory_order_acquire);
    if (uint32_t(n) > kCapacity - (w - r)) {
      dropped_.fetch_add(n);
      return;
    }
    for (int i = 0; i < n; ++i) {
      ring_[(w + i) & (kCapacity - 1)] = {input[i], output[i]};
    }
    write_position_.store(w + n, std::memory_order_release);
  }
  void Stop() {
    if (worker_.joinable()) {
      done_ = true;
      worker_.join();
    }
    if (raw_file_) {
      WriteHeader(raw_file_, uint32_t(sample_count_ * 2));
      bool failed = ferror(raw_file_);
      if (fclose(raw_file_) || failed) {
        error_ = 1;
      }
      raw_file_ = nullptr;
    }
    if (wet_file_) {
      WriteHeader(wet_file_, uint32_t(sample_count_ * 2));
      bool failed = ferror(wet_file_);
      if (fclose(wet_file_) || failed) {
        error_ = 1;
      }
      wet_file_ = nullptr;
    }
  }
  ~Recorder() { Stop(); }

 private:
  struct Pair {
    float raw, wet;
  };
  static constexpr uint32_t kCapacity = 1 << 18;
  std::unique_ptr<Pair[]> ring_{new Pair[kCapacity]};
  std::atomic<uint32_t> write_position_{0}, read_position_{0};
  std::atomic<bool> done_{false};
  std::thread worker_;
  FILE *raw_file_ = nullptr, *wet_file_ = nullptr;
  uint64_t sample_count_ = 0;
  static void Write16(FILE* f, uint16_t n) {
    unsigned char b[]{static_cast<unsigned char>(n),
                      static_cast<unsigned char>(n >> 8)};
    fwrite(b, 1, 2, f);
  }
  static void Write32(FILE* f, uint32_t n) {
    Write16(f, n & 65535);
    Write16(f, n >> 16);
  }
  static void WriteHeader(FILE* f, uint32_t size) {
    fseek(f, 0, SEEK_SET);
    fwrite("RIFF", 1, 4, f);
    Write32(f, 36 + size);
    fwrite("WAVEfmt ", 1, 8, f);
    Write32(f, 16);
    Write16(f, 1);
    Write16(f, 1);
    Write32(f, 48000);
    Write32(f, 96000);
    Write16(f, 2);
    Write16(f, 16);
    fwrite("data", 1, 4, f);
    Write32(f, size);
  }
  static int16_t ToPcm(float x) {
    return int16_t(std::lrint(std::clamp(x, -1.f, 0.999969f) * 32768.f));
  }
};

}  // namespace mobileautotune

#endif  // MOBILEAUTOTUNE_SRC_RECORDER_HPP_
