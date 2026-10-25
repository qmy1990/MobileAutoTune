#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <memory>
#include <mutex>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

#include "key_audio.hpp"
#include "key_sha256.hpp"
#include "mat/key_analysis.h"
#include "onnxruntime_cxx_api.h"
#if defined(__ANDROID__)
#include <android/api-level.h>

#include "nnapi_provider_factory.h"
#endif

namespace mobileautotune {
namespace {
using Clock = std::chrono::steady_clock;

struct Prediction {
  std::vector<float> pitch;
  std::vector<float> confidence;
};

Ort::Env& Environment() {
  static Ort::Env environment(ORT_LOGGING_LEVEL_ERROR, "MobileAutoTuneKey");
  return environment;
}

std::unique_ptr<Ort::Session> OpenSession(const std::string& path, int threads,
                                          const std::string& provider) {
  Ort::SessionOptions options;
  options.SetIntraOpNumThreads(threads);
  options.SetInterOpNumThreads(1);
  options.SetExecutionMode(ExecutionMode::ORT_SEQUENTIAL);
  options.SetGraphOptimizationLevel(GraphOptimizationLevel::ORT_ENABLE_ALL);
  options.AddConfigEntry("session.intra_op.allow_spinning", "0");
  options.AddConfigEntry("session.inter_op.allow_spinning", "0");
  if (provider == "NNAPI") {
#if defined(__ANDROID__)
    Ort::ThrowOnError(
        OrtSessionOptionsAppendExecutionProvider_Nnapi(options, 0));
#else
    throw std::runtime_error("NNAPI is unavailable on this platform");
#endif
  } else if (provider == "XNNPACK") {
    const char* keys[]{"intra_op_num_threads"};
    std::string count = std::to_string(threads);
    const char* values[]{count.c_str()};
    Ort::ThrowOnError(Ort::GetApi().SessionOptionsAppendExecutionProvider(
        options, "XNNPACK", keys, values, 1));
  }
  return std::make_unique<Ort::Session>(Environment(), path.c_str(), options);
}

Prediction RunSwift(Ort::Session& session, std::vector<float>& audio,
                    Ort::RunOptions& run_options) {
  auto memory =
      Ort::MemoryInfo::CreateCpu(OrtArenaAllocator, OrtMemTypeDefault);
  std::array<int64_t, 2> shape{1, static_cast<int64_t>(audio.size())};
  float minimum = 46.875f, maximum = 2093.75f;
  std::array<Ort::Value, 3> values{
      Ort::Value::CreateTensor<float>(memory, audio.data(), audio.size(),
                                      shape.data(), 2),
      Ort::Value::CreateTensor<float>(memory, &minimum, 1, nullptr, 0),
      Ort::Value::CreateTensor<float>(memory, &maximum, 1, nullptr, 0)};
  const char* inputs[]{"audio", "fmin", "fmax"};
  const char* outputs[]{"pitch", "confidence"};
  auto tensors = session.Run(run_options, inputs, values.data(), 3, outputs, 2);
  size_t count = tensors[0].GetTensorTypeAndShapeInfo().GetElementCount();
  if (count != tensors[1].GetTensorTypeAndShapeInfo().GetElementCount()) {
    throw std::runtime_error("SwiftF0 output shape mismatch");
  }
  const double* pitch = tensors[0].GetTensorData<double>();
  const float* confidence = tensors[1].GetTensorData<float>();
  Prediction result{std::vector<float>(count),
                    {confidence, confidence + count}};
  std::transform(pitch, pitch + count, result.pitch.begin(),
                 [](double value) { return static_cast<float>(value); });
  for (size_t i = 0; i < count; ++i) {
    float peak = 0;
    for (size_t j = i * 256; j < std::min(audio.size(), (i + 1) * 256); ++j) {
      peak = std::max(peak, std::abs(audio[j]));
    }
    if (peak < .001f) {
      result.confidence[i] = 0;
    }
  }
  return result;
}

Prediction RunRmvpe(Ort::Session& session, std::vector<float>& mel,
                    size_t frames, Ort::RunOptions& run_options) {
  auto memory =
      Ort::MemoryInfo::CreateCpu(OrtArenaAllocator, OrtMemTypeDefault);
  std::array<int64_t, 3> shape{1, 128, static_cast<int64_t>(frames)};
  auto value = Ort::Value::CreateTensor<float>(memory, mel.data(), mel.size(),
                                               shape.data(), 3);
  const char* input[]{"input"};
  const char* output[]{"output"};
  auto tensors = session.Run(run_options, input, &value, 1, output, 1);
  auto dimensions = tensors[0].GetTensorTypeAndShapeInfo().GetShape();
  if (dimensions.size() != 3 || dimensions[0] != 1 || dimensions[2] != 360 ||
      dimensions[1] < 1 || dimensions[1] > static_cast<int64_t>(frames)) {
    throw std::runtime_error("RMVPE output shape mismatch");
  }
  size_t count = dimensions[1];
  const float* salience = tensors[0].GetTensorData<float>();
  Prediction result{std::vector<float>(count), std::vector<float>(count)};
  constexpr double kCentsOffset = 1997.3794084376191;
  for (size_t i = 0; i < count; ++i) {
    const float* frame = salience + 360 * i;
    int center = static_cast<int>(std::max_element(frame, frame + 360) - frame);
    float peak = frame[center];
    double weighted = 0, sum = 0;
    for (int bin = std::max(0, center - 4); bin < std::min(360, center + 5);
         ++bin) {
      weighted += frame[bin] * (kCentsOffset + 20 * bin);
      sum += frame[bin];
    }
    result.confidence[i] = peak;
    if (peak >= .03f && sum > 0) {
      result.pitch[i] = 10 * std::exp2(weighted / sum / 1200);
    }
  }
  return result;
}

bool Matches(const Prediction& reference, const Prediction& accelerated) {
  if (reference.pitch.size() != accelerated.pitch.size()) {
    return false;
  }
  for (size_t i = 0; i < reference.pitch.size(); ++i) {
    if (!std::isfinite(accelerated.pitch[i]) ||
        !std::isfinite(accelerated.confidence[i])) {
      return false;
    }
    if (std::abs(reference.confidence[i] - accelerated.confidence[i]) > .005f) {
      return false;
    }
    if ((reference.pitch[i] > 0) != (accelerated.pitch[i] > 0)) {
      return false;
    }
    if (reference.pitch[i] > 0 &&
        std::abs(1200 * std::log2(reference.pitch[i] / accelerated.pitch[i])) >
            2) {
      return false;
    }
  }
  return true;
}

class PitchSession {
 public:
  PitchSession(std::string path, bool swift, int threads,
               mat_key_execution execution)
      : path_(std::move(path)),
        swift_(swift),
        threads_(threads),
        execution_(execution) {}

  Prediction Run(std::vector<float>& data, Ort::RunOptions& run_options,
                 const std::atomic<bool>& canceled, size_t frames = 0) {
    if (!session_) {
      return Select(data, frames, run_options, canceled);
    }
    try {
      return Predict(*session_, data, frames, run_options);
    } catch (const Ort::Exception&) {
      if (backend_ == "CPU" || canceled.load()) {
        throw;
      }
      session_.reset();
      session_ = OpenSession(path_, threads_, "CPU");
      backend_ = "CPU";
      return Predict(*session_, data, frames, run_options);
    }
  }
  const std::string& backend() const { return backend_; }

 private:
  Prediction Predict(Ort::Session& session, std::vector<float>& data,
                     size_t frames, Ort::RunOptions& run_options) {
    return swift_ ? RunSwift(session, data, run_options)
                  : RunRmvpe(session, data, frames, run_options);
  }

  Prediction Select(std::vector<float>& data, size_t frames,
                    Ort::RunOptions& run_options,
                    const std::atomic<bool>& canceled) {
    std::vector<std::string> providers;
#if defined(__ANDROID__)
    if (android_get_device_api_level() >= 27) {
      providers.emplace_back("NNAPI");
    }
    providers.emplace_back("XNNPACK");
#endif
    session_ = OpenSession(path_, threads_, "CPU");
    if (execution_ == MAT_KEY_CPU || providers.empty()) {
      return Predict(*session_, data, frames, run_options);
    }
    // Compare the same full-precision model on the same first chunk. Release
    // each session before opening another to bound peak weight memory.
    auto reference = Predict(*session_, data, frames, run_options);
    auto begin = Clock::now();
    Predict(*session_, data, frames, run_options);
    double best = std::chrono::duration<double>(Clock::now() - begin).count();
    std::string winner = "CPU";
    session_.reset();
    for (const auto& provider : providers) {
      if (canceled.load()) {
        throw std::runtime_error("Analysis canceled");
      }
      try {
        auto candidate = OpenSession(path_, threads_, provider);
        auto output = Predict(*candidate, data, frames, run_options);
        if (!Matches(reference, output)) {
          continue;
        }
        auto start = Clock::now();
        output = Predict(*candidate, data, frames, run_options);
        double seconds =
            std::chrono::duration<double>(Clock::now() - start).count();
        if (Matches(reference, output) && seconds < best * .9) {
          winner = provider;
          best = seconds;
        }
      } catch (const std::exception&) {
        if (canceled.load()) {
          throw;
        }
        // Unsupported graph partitions retain the verified CPU implementation.
      }
    }
    if (canceled.load()) {
      throw std::runtime_error("Analysis canceled");
    }
    backend_ = winner == "CPU" ? "CPU" : winner + "+CPU (validated)";
    session_ = OpenSession(path_, threads_, winner);
    return reference;
  }

  std::string path_;
  bool swift_;
  int threads_;
  mat_key_execution execution_;
  std::unique_ptr<Ort::Session> session_;
  std::string backend_ = "CPU";
};
}  // namespace
}  // namespace mobileautotune

struct mat_key_analyzer {
  mat_key_options options;
  std::string directory;
  std::string error;
  std::atomic<bool> canceled{false};
  std::mutex analysis_mutex;
  Ort::RunOptions run_options;
  bool verified = false;
  std::unique_ptr<mobileautotune::PitchSession> swift;
  std::unique_ptr<mobileautotune::PitchSession> rmvpe;
};

namespace {
int Analyze(mat_key_analyzer* analyzer, const char* path, bool pcm,
            mat_key_result* result, mat_key_progress_callback progress,
            void* user) {
  if (result) {
    *result = {};
  }
  if (!analyzer || !path || !result) {
    return -1;
  }
  std::unique_lock<std::mutex> lock(analyzer->analysis_mutex, std::try_to_lock);
  if (!lock.owns_lock()) {
    return -1;
  }
  analyzer->canceled = false;
  analyzer->error.clear();
  auto begin = mobileautotune::Clock::now();
  auto report = [&](float fraction) {
    if (analyzer->canceled.load() || (progress && progress(fraction, user))) {
      throw std::runtime_error("Analysis canceled");
    }
  };
  int stage = -2;
  try {
    mobileautotune::KeyAudioReader audio(path, pcm);
    report(0);
    analyzer->run_options.UnsetTerminate();
    if (audio.duration() < 2) {
      mat_key_result complete{};
      complete.status = MAT_KEY_INSUFFICIENT_MELODY;
      complete.duration_seconds = audio.duration();
      complete.elapsed_seconds =
          std::chrono::duration<double>(mobileautotune::Clock::now() - begin)
              .count();
      report(1);
      *result = complete;
      return 0;
    }
    stage = -3;
    if (!analyzer->verified) {
      if (mobileautotune::KeyFileSha256(analyzer->directory + "/swift-f0.onnx",
                                        &analyzer->canceled) !=
              "6385e8c2ebc3872e82c9ff5946228de44cd3be77a750ea53698b7dbfe94b0a2"
              "2" ||
          mobileautotune::KeyFileSha256(analyzer->directory + "/rmvpe.onnx",
                                        &analyzer->canceled) !=
              "5370e71ac80af8b4b7c793d27efd51fd8bf962de3a7ede0766dac0befa3660f"
              "d") {
        throw std::runtime_error(
            "Key model checksum mismatch; install the pinned FP32 model pack");
      }
      analyzer->verified = true;
    }
    std::vector<mat_pitch_observation> swift, rmvpe;
    int64_t sample_count = audio.sample_count();
    // Track memory is proportional to duration, PCM and model activations are
    // bounded by one chunk. Absolute hop alignment avoids drift at seams.
    constexpr int64_t kSwiftChunk = 256 * 1500;
    constexpr int64_t kSwiftLeft = 256 * 11, kSwiftRight = 256 * 10;
    stage = -3;
    for (int64_t start = 0; start < sample_count; start += kSwiftChunk) {
      report(.02f + .18f * start / sample_count);
      int64_t end = std::min(sample_count, start + kSwiftChunk);
      int64_t left = std::max<int64_t>(0, start - kSwiftLeft);
      int64_t right = std::min(sample_count, end + kSwiftRight);
      auto input = audio.Read(left, right - left);
      auto prediction = analyzer->swift->Run(input, analyzer->run_options,
                                             analyzer->canceled);
      size_t offset = (start - left) / 256;
      for (size_t i = offset; i < prediction.pitch.size(); ++i) {
        double time = (left + i * 256) / 16000.;
        if (time >= end / 16000.) {
          break;
        }
        swift.push_back({time, prediction.pitch[i], prediction.confidence[i]});
      }
    }
    constexpr int64_t kRmvpeChunk = 160 * 768;
    constexpr int64_t kRmvpeContext = 160 * 128;
    for (int64_t start = 0; start < sample_count; start += kRmvpeChunk) {
      report(.2f + .75f * start / sample_count);
      int64_t end = std::min(sample_count, start + kRmvpeChunk);
      int64_t left = std::max<int64_t>(0, start - kRmvpeContext);
      int64_t right = std::min(sample_count, end + kRmvpeContext);
      auto input = audio.Read(left, right - left);
      size_t frames = 0;
      auto mel = mobileautotune::KeyMelSpectrogram(input, frames);
      // The U-Net downsamples time five times. Zero-pad the log-mel time
      // axis exactly as the reference RMVPE inference does.
      size_t padded = (frames + 31) / 32 * 32;
      std::vector<float> aligned(128 * padded);
      for (int band = 0; band < 128; ++band) {
        std::copy_n(mel.data() + band * frames, frames,
                    aligned.data() + band * padded);
        std::fill(aligned.begin() + band * padded + frames,
                  aligned.begin() + (band + 1) * padded, 0.f);
      }
      auto prediction = analyzer->rmvpe->Run(aligned, analyzer->run_options,
                                             analyzer->canceled, padded);
      size_t offset = (start - left) / 160;
      for (size_t i = offset; i < prediction.pitch.size(); ++i) {
        double time = (left + i * 160) / 16000.;
        if (time >= end / 16000.) {
          break;
        }
        float peak = 0;
        size_t sample = i * 160;
        for (size_t j = sample; j < std::min(input.size(), sample + 160); ++j) {
          peak = std::max(peak, std::abs(input[j]));
        }
        rmvpe.push_back({time, prediction.pitch[i],
                         peak >= .001f ? prediction.confidence[i] : 0});
      }
    }
    report(.96f);
    mat_key_result complete{};
    int code = mat_key_analyze_tracks(
        swift.data(), swift.size(), rmvpe.data(), rmvpe.size(),
        audio.duration(), analyzer->options.detect_modulations, &complete);
    if (code) {
      analyzer->error = "Invalid neural pitch output";
      return code;
    }
    complete.threads = analyzer->options.threads;
    std::snprintf(complete.swift_backend, sizeof(complete.swift_backend), "%s",
                  analyzer->swift->backend().c_str());
    std::snprintf(complete.rmvpe_backend, sizeof(complete.rmvpe_backend), "%s",
                  analyzer->rmvpe->backend().c_str());
    complete.elapsed_seconds =
        std::chrono::duration<double>(mobileautotune::Clock::now() - begin)
            .count();
    report(1);
    *result = complete;
    return 0;
  } catch (const std::bad_alloc&) {
    analyzer->error = "Insufficient memory for the full-precision model";
    return -4;
  } catch (const std::exception& error) {
    analyzer->error = error.what();
    return analyzer->canceled.load() || analyzer->error == "Analysis canceled"
               ? -5
               : stage;
  }
}
}  // namespace

extern "C" {
mat_key_analyzer* mat_key_create(const char* directory,
                                 const mat_key_options* options) {
  if (!directory) {
    return nullptr;
  }
  try {
    auto analyzer = std::make_unique<mat_key_analyzer>();
    analyzer->directory = directory;
    analyzer->options = options ? *options : mat_default_key_options();
    if (analyzer->options.threads < 0 || analyzer->options.threads > 64 ||
        analyzer->options.execution < MAT_KEY_AUTO ||
        analyzer->options.execution > MAT_KEY_ACCELERATED) {
      return nullptr;
    }
    if (!analyzer->options.threads) {
      analyzer->options.threads =
          std::clamp<int>(std::thread::hardware_concurrency(), 1, 6);
    }
    std::string swift = analyzer->directory + "/swift-f0.onnx";
    std::string rmvpe = analyzer->directory + "/rmvpe.onnx";
    if (!std::filesystem::is_regular_file(swift) ||
        !std::filesystem::is_regular_file(rmvpe)) {
      return nullptr;
    }
    analyzer->swift = std::make_unique<mobileautotune::PitchSession>(
        swift, true, analyzer->options.threads, analyzer->options.execution);
    analyzer->rmvpe = std::make_unique<mobileautotune::PitchSession>(
        rmvpe, false, analyzer->options.threads, analyzer->options.execution);
    return analyzer.release();
  } catch (...) {
    return nullptr;
  }
}
void mat_key_destroy(mat_key_analyzer* analyzer) { delete analyzer; }
void mat_key_cancel(mat_key_analyzer* analyzer) {
  if (!analyzer) {
    return;
  }
  analyzer->canceled = true;
  try {
    analyzer->run_options.SetTerminate();
  } catch (...) {
  }
}
const char* mat_key_last_error(const mat_key_analyzer* analyzer) {
  return analyzer ? analyzer->error.c_str() : "Invalid key analyzer";
}
int mat_key_analyze_wav(mat_key_analyzer* analyzer, const char* path,
                        mat_key_result* result,
                        mat_key_progress_callback progress, void* user) {
  return Analyze(analyzer, path, false, result, progress, user);
}
int mat_key_analyze_pcm_file(mat_key_analyzer* analyzer, const char* path,
                             mat_key_result* result,
                             mat_key_progress_callback progress, void* user) {
  return Analyze(analyzer, path, true, result, progress, user);
}
}  // extern "C"
