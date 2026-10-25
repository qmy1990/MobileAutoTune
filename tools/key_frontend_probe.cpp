#include <cstdio>
#include <fstream>

#include "../src/key_audio.hpp"

int main(int argc, char** argv) {
  if (argc != 3) {
    return 1;
  }
  mobileautotune::KeyAudioReader reader(argv[1]);
  if (reader.sample_count() > 16000 * 60) {
    return 2;
  }
  auto audio = reader.Read(0, reader.sample_count());
  std::ofstream pcm(std::string(argv[2]) + ".pcm", std::ios::binary);
  pcm.write(reinterpret_cast<const char*>(audio.data()),
            audio.size() * sizeof(float));
  size_t frames = 0;
  auto mel = mobileautotune::KeyMelSpectrogram(audio, frames);
  std::ofstream features(std::string(argv[2]) + ".mel", std::ios::binary);
  features.write(reinterpret_cast<const char*>(mel.data()),
                 mel.size() * sizeof(float));
  std::printf("samples=%zu mel_frames=%zu\n", audio.size(), frames);
}
