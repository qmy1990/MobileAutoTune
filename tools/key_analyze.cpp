#include <cstdio>
#include <cstdlib>
#include <memory>
#include <string>
#include <vector>

#include "mat/key_analysis.h"

int main(int argc, char** argv) {
  if (argc < 3) {
    std::fprintf(
        stderr,
        "Usage: mat_key_analyze models/key input.wav [--cpu] [--threads N]\n");
    return 1;
  }
  auto options = mat_default_key_options();
  for (int i = 3; i < argc; ++i) {
    std::string argument = argv[i];
    if (argument == "--cpu") {
      options.execution = MAT_KEY_CPU;
    } else if (argument == "--threads" && i + 1 < argc) {
      options.threads = std::atoi(argv[++i]);
    } else {
      std::fprintf(stderr, "Unknown argument\n");
      return 1;
    }
  }
  std::unique_ptr<mat_key_analyzer, decltype(&mat_key_destroy)> analyzer(
      mat_key_create(argv[1], &options), mat_key_destroy);
  if (!analyzer) {
    std::fprintf(stderr, "Cannot open pinned key models\n");
    return 2;
  }
  mat_key_result result{};
  int code =
      mat_key_analyze_wav(analyzer.get(), argv[2], &result, nullptr, nullptr);
  if (code) {
    std::fprintf(stderr, "Analysis failed (%d): %s\n", code,
                 mat_key_last_error(analyzer.get()));
    return 2;
  }
  std::vector<char> json(mat_key_result_json(&result, nullptr, 0));
  mat_key_result_json(&result, json.data(), json.size());
  std::puts(json.data());
  return 0;
}
