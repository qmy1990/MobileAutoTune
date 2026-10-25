#ifndef MOBILEAUTOTUNE_SRC_KEY_AUDIO_HPP_
#define MOBILEAUTOTUNE_SRC_KEY_AUDIO_HPP_

#include <cstdint>
#include <fstream>
#include <string>
#include <vector>

namespace mobileautotune {

// Random-access mono decoder with a bounded working set. A windowed
// sinc resampler supplies the same 16kHz input to both pitch estimators.
class KeyAudioReader {
 public:
  explicit KeyAudioReader(const std::string& path, bool raw_pcm = false);
  int64_t sample_count() const { return sample_count_; }
  double duration() const {
    return static_cast<double>(source_frames_) / sample_rate_;
  }
  std::vector<float> Read(int64_t first, int64_t count);

 private:
  float Decode(const unsigned char* data) const;
  std::ifstream file_;
  int sample_rate_ = 16000;
  int channels_ = 1;
  int bits_ = 32;
  int format_ = 3;
  int alignment_ = 4;
  int64_t data_offset_ = 0;
  int64_t source_frames_ = 0;
  int64_t sample_count_ = 0;
  int radius_ = 0;
  int phase_count_ = 0;
  std::vector<double> resampling_weights_;
};

// HTK-spaced, Slaney-normalized mel magnitudes and periodic Hann match RMVPE's
// reference frontend: 16kHz, FFT1024, hop160, 128 bands, 30..8000Hz, reflect
// centering.
std::vector<float> KeyMelSpectrogram(const std::vector<float>& audio,
                                     size_t& frames);

}  // namespace mobileautotune
#endif  // MOBILEAUTOTUNE_SRC_KEY_AUDIO_HPP_
