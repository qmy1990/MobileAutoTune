#include "key_audio.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <complex>
#include <cstring>
#include <limits>
#include <numeric>
#include <stdexcept>

#include "fft.h"

namespace mobileautotune {
namespace {
uint16_t Read16(const unsigned char* p) {
  return p[0] | static_cast<uint16_t>(p[1]) << 8;
}
uint32_t Read32(const unsigned char* p) {
  return p[0] | static_cast<uint32_t>(p[1]) << 8 |
         static_cast<uint32_t>(p[2]) << 16 | static_cast<uint32_t>(p[3]) << 24;
}
double HzToMel(double hz) { return 2595 * std::log10(1 + hz / 700); }
double MelToHz(double mel) { return 700 * (std::pow(10., mel / 2595) - 1); }
int Reflect(int index, int size) {
  if (size <= 1) {
    return 0;
  }
  int period = 2 * (size - 1);
  index = (index % period + period) % period;
  return index < size ? index : period - index;
}
}  // namespace

KeyAudioReader::KeyAudioReader(const std::string& path, bool raw_pcm)
    : file_(path, std::ios::binary) {
  if (!file_) {
    throw std::runtime_error("Cannot open audio file");
  }
  file_.seekg(0, std::ios::end);
  int64_t length = file_.tellg();
  if (length <= 0) {
    throw std::runtime_error("Empty audio file");
  }
  int64_t bytes = length;
  file_.seekg(0);
  if (!raw_pcm) {
    std::array<unsigned char, 40> header{};
    file_.read(reinterpret_cast<char*>(header.data()), 12);
    if (!file_ || std::memcmp(header.data(), "RIFF", 4) ||
        std::memcmp(header.data() + 8, "WAVE", 4)) {
      throw std::runtime_error("Expected a PCM WAV file");
    }
    int64_t riff_end = static_cast<int64_t>(Read32(header.data() + 4)) + 8;
    if (riff_end > length || riff_end < 12) {
      throw std::runtime_error("Truncated RIFF container");
    }
    bool have_format = false, have_data = false;
    while (static_cast<int64_t>(file_.tellg()) + 8 <= riff_end) {
      file_.read(reinterpret_cast<char*>(header.data()), 8);
      int64_t size = Read32(header.data() + 4);
      int64_t begin = file_.tellg();
      if (begin < 0 || begin + size > riff_end) {
        throw std::runtime_error("Truncated WAV chunk");
      }
      if (!std::memcmp(header.data(), "fmt ", 4)) {
        if (size < 16) {
          throw std::runtime_error("Invalid WAV format");
        }
        file_.read(reinterpret_cast<char*>(header.data()), 16);
        format_ = Read16(header.data());
        channels_ = Read16(header.data() + 2);
        sample_rate_ = Read32(header.data() + 4);
        alignment_ = Read16(header.data() + 12);
        bits_ = Read16(header.data() + 14);
        if (format_ == 0xfffe) {
          if (size < 40) {
            throw std::runtime_error("Invalid extensible WAV");
          }
          file_.read(reinterpret_cast<char*>(header.data() + 16), 24);
          constexpr std::array<unsigned char, 14> kGuidSuffix{
              0, 0, 0, 0, 16, 0, 128, 0, 0, 170, 0, 56, 155, 113};
          if (Read16(header.data() + 16) < 22 ||
              Read16(header.data() + 18) != bits_ ||
              std::memcmp(header.data() + 26, kGuidSuffix.data(), 14)) {
            throw std::runtime_error("Unsupported WAV subformat");
          }
          format_ = Read16(header.data() + 24);
        }
        have_format = true;
      } else if (!std::memcmp(header.data(), "data", 4) && !have_data) {
        data_offset_ = begin;
        bytes = size;
        have_data = true;
      }
      file_.seekg(begin + size + (size & 1));
      if (!file_) {
        throw std::runtime_error("Invalid WAV chunk boundary");
      }
    }
    if (!have_format || !have_data || sample_rate_ < 8000 ||
        sample_rate_ > 192000 || channels_ < 1 || channels_ > 8 ||
        alignment_ != channels_ * bits_ / 8 ||
        !((format_ == 1 && (bits_ == 16 || bits_ == 24 || bits_ == 32)) ||
          (format_ == 3 && bits_ == 32))) {
      throw std::runtime_error("Unsupported WAV encoding/rate/channel count");
    }
  }
  if (bytes <= 0 || bytes % alignment_) {
    throw std::runtime_error("Invalid PCM data length");
  }
  source_frames_ = bytes / alignment_;
  if (static_cast<double>(source_frames_) / sample_rate_ > 86400) {
    throw std::runtime_error("Audio exceeds the 24-hour analysis limit");
  }
  sample_count_ = (source_frames_ * 16000 + sample_rate_ - 1) / sample_rate_;
  radius_ = 32 * std::max(1, (sample_rate_ + 15999) / 16000);
  phase_count_ = 16000 / std::gcd(sample_rate_, 16000);
  // Common audio rates have few rational phases. Cache their exact kernels;
  // unusual rates retain the same filter without a large coefficient table.
  if (sample_rate_ != 16000 && phase_count_ <= 1024) {
    resampling_weights_.resize(phase_count_ * radius_ * 2);
    double cutoff = std::min(1., 16000. / sample_rate_) * .94;
    for (int phase_index = 0; phase_index < phase_count_; ++phase_index) {
      double fraction = static_cast<double>(phase_index) / phase_count_;
      double normalization = 0;
      double* kernel = resampling_weights_.data() + phase_index * radius_ * 2;
      for (int tap = -radius_ + 1; tap <= radius_; ++tap) {
        double distance = tap - fraction;
        double phase = 3.141592653589793 * distance * cutoff;
        double sinc = std::abs(phase) < 1e-12 ? 1 : std::sin(phase) / phase;
        double window = .42 +
                        .5 * std::cos(3.141592653589793 * distance / radius_) +
                        .08 * std::cos(6.283185307179586 * distance / radius_);
        kernel[tap + radius_ - 1] = sinc * window;
        normalization += sinc * window;
      }
      for (int tap = 0; tap < radius_ * 2; ++tap) {
        kernel[tap] /= normalization;
      }
    }
  }
}

float KeyAudioReader::Decode(const unsigned char* data) const {
  if (format_ == 3) {
    uint32_t bits = Read32(data);
    float value;
    std::memcpy(&value, &bits, sizeof(value));
    if (!std::isfinite(value)) {
      throw std::runtime_error("Nonfinite PCM sample");
    }
    return value;
  }
  if (bits_ == 16) {
    return static_cast<int16_t>(Read16(data)) / 32768.f;
  }
  if (bits_ == 24) {
    int32_t value = data[0] | static_cast<int32_t>(data[1]) << 8 |
                    static_cast<int32_t>(data[2]) << 16;
    if (value & 0x800000) {
      value -= 0x1000000;
    }
    return value / 8388608.f;
  }
  return static_cast<int32_t>(Read32(data)) / 2147483648.f;
}

std::vector<float> KeyAudioReader::Read(int64_t first, int64_t count) {
  if (first < 0 || count < 0 || first > sample_count_ ||
      count > sample_count_ - first || count > 16000 * 60) {
    throw std::runtime_error("Invalid audio read range");
  }
  if (!count) {
    return {};
  }
  double ratio = sample_rate_ / 16000.;
  const int kRadius = radius_;
  int64_t begin = std::max<int64_t>(0, std::floor(first * ratio) - kRadius);
  int64_t end = std::min<int64_t>(source_frames_,
                                  std::ceil((first + count) * ratio) + kRadius);
  std::vector<unsigned char> bytes((end - begin) * alignment_);
  file_.clear();
  file_.seekg(data_offset_ + begin * alignment_);
  file_.read(reinterpret_cast<char*>(bytes.data()), bytes.size());
  if (!file_) {
    throw std::runtime_error("Truncated PCM data");
  }
  std::vector<float> mono(end - begin), output(count);
  for (int64_t frame = 0; frame < end - begin; ++frame) {
    double sample = 0;
    for (int channel = 0; channel < channels_; ++channel) {
      sample +=
          Decode(bytes.data() + frame * alignment_ + channel * (bits_ / 8));
    }
    mono[frame] = sample / channels_;
  }
  if (sample_rate_ == 16000) {
    std::copy_n(mono.begin() + (first - begin), count, output.begin());
    return output;
  }
  double cutoff = std::min(1., 16000. / sample_rate_) * .94;
  for (int64_t i = 0; i < count; ++i) {
    int64_t numerator = (first + i) * sample_rate_;
    int64_t center = numerator / 16000;
    if (!resampling_weights_.empty()) {
      int phase = (numerator % 16000) / (16000 / phase_count_);
      const double* kernel = resampling_weights_.data() + phase * kRadius * 2;
      double sum = 0;
      for (int tap = -kRadius + 1; tap <= kRadius; ++tap) {
        int64_t index =
            std::clamp<int64_t>(center + tap, 0, source_frames_ - 1);
        sum += mono[index - begin] * kernel[tap + kRadius - 1];
      }
      output[i] = sum;
      continue;
    }
    double position = static_cast<double>(numerator) / 16000;
    double sum = 0, normalization = 0;
    for (int tap = -kRadius + 1; tap <= kRadius; ++tap) {
      double distance = center + tap - position;
      double phase = 3.141592653589793 * distance * cutoff;
      double sinc = std::abs(phase) < 1e-12 ? 1 : std::sin(phase) / phase;
      double window = .42 +
                      .5 * std::cos(3.141592653589793 * distance / kRadius) +
                      .08 * std::cos(6.283185307179586 * distance / kRadius);
      double weight = sinc * window;
      int64_t index = std::clamp<int64_t>(center + tap, 0, source_frames_ - 1);
      sum += mono[index - begin] * weight;
      normalization += weight;
    }
    output[i] = sum / normalization;
  }
  return output;
}

std::vector<float> KeyMelSpectrogram(const std::vector<float>& audio,
                                     size_t& frames) {
  if (audio.empty()) {
    throw std::runtime_error("Empty mel input");
  }
  frames = audio.size() / 160 + 1;
  std::vector<float> mel(128 * frames);
  std::array<double, 130> frequencies{};
  double lower = HzToMel(30), upper = HzToMel(8000);
  for (int i = 0; i < 130; ++i) {
    frequencies[i] = MelToHz(lower + (upper - lower) * i / 129);
  }
  std::array<std::vector<std::pair<int, float>>, 128> filters;
  for (int band = 0; band < 128; ++band) {
    for (int bin = 0; bin <= 512; ++bin) {
      double hz = bin * 16000. / 1024;
      double weight = std::max(
          0., std::min((hz - frequencies[band]) /
                           (frequencies[band + 1] - frequencies[band]),
                       (frequencies[band + 2] - hz) /
                           (frequencies[band + 2] - frequencies[band + 1])));
      weight *= 2 / (frequencies[band + 2] - frequencies[band]);
      if (weight > 0) {
        filters[band].emplace_back(bin, weight);
      }
    }
  }
  signalsmith::linear::FFT<float> fft(1024);
  std::array<std::complex<float>, 1024> input{}, spectrum{};
  std::array<float, 513> magnitude{};
  std::array<float, 1024> window{};
  for (int i = 0; i < 1024; ++i) {
    window[i] = .5 - .5 * std::cos(6.283185307179586 * i / 1024);
  }
  for (size_t frame = 0; frame < frames; ++frame) {
    for (int i = 0; i < 1024; ++i) {
      input[i] = {audio[Reflect(static_cast<int>(frame * 160) + i - 512,
                                static_cast<int>(audio.size()))] *
                      window[i],
                  0};
    }
    fft.fft(input.data(), spectrum.data());
    for (int bin = 0; bin <= 512; ++bin) {
      magnitude[bin] = std::sqrt(std::norm(spectrum[bin]));
    }
    for (int band = 0; band < 128; ++band) {
      float value = 0;
      for (const auto& weight : filters[band]) {
        value += magnitude[weight.first] * weight.second;
      }
      mel[band * frames + frame] = std::log(std::max(value, 1e-5f));
    }
  }
  return mel;
}
}  // namespace mobileautotune
