#include <array>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <stdexcept>
#include <string>
#include <vector>

#include "key_audio.hpp"
#include "key_sha256.hpp"

#define CHECK(condition)                                                      \
  do {                                                                        \
    if (!(condition)) {                                                       \
      std::fprintf(stderr, "Audio test line %d: %s\n", __LINE__, #condition); \
      std::exit(1);                                                           \
    }                                                                         \
  } while (0)

namespace {
void Write16(std::ostream& output, uint16_t value) {
  output.put(value & 255);
  output.put(value >> 8);
}
void Write32(std::ostream& output, uint32_t value) {
  Write16(output, value & 65535);
  Write16(output, value >> 16);
}
void MakeWave(const std::filesystem::path& path, int rate) {
  std::ofstream output(path, std::ios::binary);
  output.write("RIFF", 4);
  Write32(output, 36 + rate * 2);
  output.write("WAVEfmt ", 8);
  Write32(output, 16);
  Write16(output, 1);
  Write16(output, 1);
  Write32(output, rate);
  Write32(output, rate * 2);
  Write16(output, 2);
  Write16(output, 16);
  output.write("data", 4);
  Write32(output, rate * 2);
  for (int i = 0; i < rate; ++i) {
    Write16(output,
            static_cast<int16_t>(.4 * 32767 *
                                 std::sin(6.283185307179586 * 440 * i / rate)));
  }
}
}  // namespace

int main() {
  auto directory =
      std::filesystem::temp_directory_path() / "mat-key-audio-tests";
  std::filesystem::create_directories(directory);
  auto path = directory / "test.wav";
  for (int rate : {8000, 16000, 22050, 44100, 48000, 96000, 192000}) {
    MakeWave(path, rate);
    mobileautotune::KeyAudioReader reader(path.string());
    CHECK(reader.sample_count() == 16000);
    auto audio = reader.Read(0, 16000);
    auto seam = reader.Read(7999, 105);
    double error = 0;
    for (size_t i = 0; i < seam.size(); ++i) {
      CHECK(seam[i] == audio[7999 + i]);
    }
    for (int i = 100; i < 15900; ++i) {
      double delta =
          audio[i] - .4 * std::sin(6.283185307179586 * 440 * i / 16000);
      error += delta * delta;
    }
    CHECK(std::sqrt(error / 15800) < .0001);
    size_t frames = 0;
    auto mel = mobileautotune::KeyMelSpectrogram(audio, frames);
    CHECK(frames == 101 && mel.size() == 128 * frames);
    for (float value : mel) {
      CHECK(std::isfinite(value));
    }
  }
  {
    std::ofstream output(path, std::ios::binary);
    output << "abc";
  }
  CHECK(mobileautotune::KeyFileSha256(path.string()) ==
        "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad");
  { std::ofstream output(path, std::ios::binary); }
  CHECK(mobileautotune::KeyFileSha256(path.string()) ==
        "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855");
  {
    std::ofstream output(path, std::ios::binary);
    output << std::string(1000000, 'a');
  }
  CHECK(mobileautotune::KeyFileSha256(path.string()) ==
        "cdc76e5c9914fb9281a1c7e284d73e67f1809a48a497200e046d39ccc7112cd0");
  bool rejected = false;
  try {
    mobileautotune::KeyAudioReader invalid(path.string());
  } catch (const std::runtime_error&) {
    rejected = true;
  }
  CHECK(rejected);
  std::atomic<bool> canceled{true};
  rejected = false;
  try {
    mobileautotune::KeyFileSha256(path.string(), &canceled);
  } catch (const std::runtime_error&) {
    rejected = true;
  }
  CHECK(rejected);
  std::filesystem::remove(path);
  std::filesystem::remove(directory);
  std::puts(
      "Key resampling, chunk seams, mel frontend and checksum tests passed");
}
