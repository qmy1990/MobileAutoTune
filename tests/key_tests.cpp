#include <array>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <limits>
#include <vector>

#include "mat/key_analysis.h"

#define CHECK(condition)                                                    \
  do {                                                                      \
    if (!(condition)) {                                                     \
      std::fprintf(stderr, "Key test line %d: %s\n", __LINE__, #condition); \
      std::exit(1);                                                         \
    }                                                                       \
  } while (0)

namespace {
std::vector<mat_pitch_observation> Melody(const std::vector<int>& notes,
                                          int transpose, double detuning = 0,
                                          double offset = 0) {
  std::vector<mat_pitch_observation> track;
  int frame = 0;
  for (int note : notes) {
    int length = note < 0 ? 25 : 50;
    for (int i = 0; i < length; ++i, ++frame) {
      double vibrato = .08 * std::sin(i * .3);
      float hz = note < 0 ? 0
                          : 440 * std::exp2((note + transpose + detuning / 100 +
                                             vibrato - 69) /
                                            12);
      track.push_back({offset + frame * .01, hz, note < 0 ? 0.f : .95f});
    }
  }
  return track;
}
}  // namespace

int main() {
  const std::vector<int> major{60, 64, 67, 65, 64, 62, 60, -1,
                               67, 69, 67, 65, 64, 62, 59, 60};
  const std::vector<int> minor{60, 63, 67, 65, 63, 62, 60, -1,
                               67, 68, 67, 65, 63, 62, 59, 60};
  for (int scale = 0; scale < 2; ++scale) {
    for (int tonic = 0; tonic < 12; ++tonic) {
      auto track = Melody(scale ? minor : major, tonic, 22);
      mat_key_result result{};
      CHECK(mat_key_analyze_tracks(
                track.data(), track.size(), track.data(), track.size(),
                track.back().time_seconds + .01, 1, &result) == 0);
      CHECK(result.candidate_count == 24);
      std::printf("tonic %d scale %d => %d/%d support %.3f\n", tonic, scale,
                  result.candidates[0].tonic, result.candidates[0].scale,
                  result.candidates[0].support);
      CHECK(result.candidates[0].tonic == tonic);
      CHECK(result.candidates[0].scale == scale);
      CHECK(std::abs(result.tuning_cents - 22) < 4);
      CHECK(result.note_count >= 14);
      double sum = 0;
      for (const auto& candidate : result.candidates) {
        sum += candidate.support;
      }
      CHECK(std::abs(sum - 1) < 1e-8);
      std::vector<char> json(mat_key_result_json(&result, nullptr, 0));
      CHECK(mat_key_result_json(&result, json.data(), json.size()) ==
            json.size());
      CHECK(json.back() == 0);
    }
  }
  mat_key_result result{};
  CHECK(mat_key_analyze_tracks(nullptr, 0, nullptr, 0, 10, 1, &result) == 0);
  CHECK(result.status == MAT_KEY_INSUFFICIENT_MELODY &&
        result.candidate_count == 0);
  auto monotone = Melody({60, 60, 60, 60, 60, 60}, 0);
  CHECK(!mat_key_analyze_tracks(monotone.data(), monotone.size(), nullptr, 0, 3,
                                1, &result));
  CHECK(result.status == MAT_KEY_INSUFFICIENT_MELODY);
  monotone[1].time_seconds = 0;
  CHECK(mat_key_analyze_tracks(monotone.data(), monotone.size(), nullptr, 0, 3,
                               1, &result) == -1);
  CHECK(result.candidate_count == 0);
  CHECK(mat_key_analyze_tracks(nullptr, 1, nullptr, 0, 1, 1, &result) == -1);
  CHECK(mat_key_analyze_tracks(nullptr, 0, nullptr, 0, NAN, 1, &result) == -1);
  std::puts("Key inference tests passed");
}
