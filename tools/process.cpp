#include <chrono>
#include <iostream>
#include <stdexcept>
#include <string>

#include "mat/offline.h"
int main(int argc, char** argv) {
  if (argc < 3) {
    std::cerr
        << "Usage: mat_process input.wav output.wav [--mode realtime|offline] "
           "[--tonic 0..11] [--scale major|minor|chromatic] [--tuning 0..1] "
           "[--pitch-shift -12..12] [--reverb 0..1] [--echo 0..1] [--denoise "
           "0..1] [--echo-ms 40..800] [--a-hz 400..480] [--tail]\n";
    return 1;
  }
  auto p = mat_default_parameters();
  mat_mode mode = MAT_OFFLINE;
  bool tail = false;
  try {
    for (int i = 3; i < argc; ++i) {
      std::string key = argv[i];
      if (key == "--tail") {
        tail = true;
        continue;
      }
      if (++i == argc) {
        throw std::runtime_error("Missing option value");
      }
      std::string value = argv[i];
      if (key == "--mode") {
        if (value == "realtime") {
          mode = MAT_REALTIME;
        } else if (value == "offline") {
          mode = MAT_OFFLINE;
        } else {
          throw std::runtime_error("Invalid mode");
        }
      } else if (key == "--tonic") {
        p.tonic = std::stoi(value);
      } else if (key == "--scale") {
        if (value == "major") {
          p.scale = MAT_MAJOR;
        } else if (value == "minor") {
          p.scale = MAT_MINOR;
        } else if (value == "chromatic") {
          p.scale = MAT_CHROMATIC;
        } else {
          throw std::runtime_error("Invalid scale");
        }
      } else if (key == "--pitch-shift") {
        p.pitch_shift_semitones = std::stof(value);
      } else if (key == "--tuning") {
        p.tuning = std::stof(value);
      } else if (key == "--reverb") {
        p.reverb = std::stof(value);
      } else if (key == "--echo") {
        p.echo = std::stof(value);
      } else if (key == "--denoise") {
        p.denoise = std::stof(value);
      } else if (key == "--echo-ms") {
        p.echo_ms = std::stof(value);
      } else if (key == "--a-hz") {
        p.concert_a_hz = std::stof(value);
      } else {
        throw std::runtime_error("Unknown option: " + key);
      }
    }
    auto start = std::chrono::steady_clock::now();
    int result =
        mat_process_wav(argv[1], argv[2], &p, mode, tail, nullptr, nullptr);
    auto seconds =
        std::chrono::duration<double>(std::chrono::steady_clock::now() - start)
            .count();
    auto* e = mat_create(48000, mode);
    std::cout << "{\"result\":" << result << ",\"seconds\":" << seconds
              << ",\"frame_latency_ms\":" << mat_latency_samples(e) / 48.f
              << ",\"stream_latency_ms\":"
              << mat_stream_latency_samples(e) / 48.f
              << ",\"stream_max_history_ms\":"
              << mat_stream_max_history_samples(e) / 48.f << "}\n";
    mat_destroy(e);
    return result ? 2 : 0;
  } catch (const std::exception& e) {
    std::cerr << e.what() << "\n";
    return 1;
  }
}
