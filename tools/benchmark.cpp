#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <vector>

#include "mat/mobile_audio.h"
int main() {
  std::puts("{\"benchmark\":\"desktop callback compute only\",\"results\":[");
  for (int mode = 0; mode < 2; ++mode) {
    auto* e = mat_create(48000, mat_mode(mode));
    if (!e) {
      return 1;
    }
    auto p = mat_default_parameters();
    p.tuning = p.reverb = p.echo = p.denoise = 1;
    mat_set_parameters(e, &p);
    mat_reset(e);
    std::vector<float> times;
    times.reserve(3000);
    float x[480], y[480];
    for (int frame = 0; frame < 3100; ++frame) {
      for (int i = 0; i < 480; ++i) {
        double time = (frame * 480 + i) / 48000.;
        x[i] = 0.1f * std::sin(6.28318530718 * (150 + 80 * std::sin(time)) *
                               time) +
               0.05f * std::sin(6.28318530718 * 470 * time);
      }
      auto before = std::chrono::steady_clock::now();
      mat_process_frame(e, x, y);
      auto us = std::chrono::duration<float, std::micro>(
                    std::chrono::steady_clock::now() - before)
                    .count();
      if (frame >= 100) {
        times.push_back(us);
      }
    }
    std::sort(times.begin(), times.end());
    double sum = 0;
    int missed = 0;
    for (auto us : times) {
      sum += us;
      if (us > 10000) {
        ++missed;
      }
    }
    std::printf(
        "%s{\"mode\":\"%s\",\"frame_ms\":10,\"mean_us\":%.3f,\"p50_us\":%.3f,"
        "\"p95_us\":%.3f,\"p99_us\":%.3f,\"max_us\":%.3f,\"over_10ms\":%d,"
        "\"frame_latency_ms\":%.3f,\"stream_nominal_latency_ms\":%.3f,\"stream_"
        "max_history_ms\":%.3f}",
        mode ? ",\n" : "", mode ? "offline" : "realtime", sum / times.size(),
        times[1500], times[2850], times[2970], times.back(), missed,
        mat_latency_samples(e) / 48., mat_stream_latency_samples(e) / 48.,
        mat_stream_max_history_samples(e) / 48.);
    mat_destroy(e);
  }
  std::puts("\n]}");
}
