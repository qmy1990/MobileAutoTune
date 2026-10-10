#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <ctime>
#include <vector>

#include "mat/mobile_audio.h"

static double CurrentSeconds(clockid_t clock) {
  timespec t{};
  clock_gettime(clock, &t);
  return t.tv_sec * 1e6 + t.tv_nsec / 1e3;
}
// Runs the SAME core with/without ARM flush-to-zero. Isolates DSP CPU time
// from audio scheduling and exercises long silence after voiced reverb tails.
int main(int argc, char** argv) {
  bool flush = argc > 1 && std::atoi(argv[1]);
#if defined(__aarch64__)
  unsigned long fpcr;
  asm volatile("mrs %0, fpcr" : "=r"(fpcr));
  std::printf("{\"flush\":%s,\"original_fpcr\":%lu,\"buckets\":[",
              flush ? "true" : "false", fpcr);
  auto next = flush ? fpcr | (1ul << 24) : fpcr & ~(1ul << 24);
  asm volatile("msr fpcr, %0" ::"r"(next));
#else
  std::printf("{\"flush\":false,\"buckets\":[");
#endif
  auto* e = mat_create(48000, MAT_REALTIME);
  if (!e) {
    return 1;
  }
  auto p = mat_default_parameters();
  p.tuning = 1;
  p.reverb = .9f;
  p.echo = .25f;
  p.denoise = argc > 2 ? std::atof(argv[2]) : .35f;
  mat_set_parameters(e, &p);
  mat_reset(e);
  float input[480], output[480];
  double checksum = 0;
  for (int bucket = 0; bucket < 10; bucket++) {
    std::vector<double> wall, cpu;
    wall.reserve(3000);
    cpu.reserve(3000);
    for (int f = 0; f < 3000; f++) {
      int frame = bucket * 3000 + f;
      for (int i = 0; i < 480; i++) {
        double t = (frame * 480 + i) / 48000.;
        input[i] = frame < 1000 ? .1f * std::sin(6.28318530718 * 225 * t) +
                                      .02f * std::sin(6.28318530718 * 470 * t)
                                : 0;
      }
      double w = CurrentSeconds(CLOCK_MONOTONIC),
             c = CurrentSeconds(CLOCK_THREAD_CPUTIME_ID);
      mat_process_frame(e, input, output);
      cpu.push_back(CurrentSeconds(CLOCK_THREAD_CPUTIME_ID) - c);
      wall.push_back(CurrentSeconds(CLOCK_MONOTONIC) - w);
      checksum += output[17];
    }
    std::sort(wall.begin(), wall.end());
    std::sort(cpu.begin(), cpu.end());
    double sum = 0;
    for (double x : cpu) {
      sum += x;
    }
    std::printf(
        "%s{\"audio_start_s\":%d,\"mean_cpu_us\":%.3f,\"p99_cpu_us\":%.3f,"
        "\"p99_wall_us\":%.3f,\"max_wall_us\":%.3f}",
        bucket ? "," : "", bucket * 30, sum / cpu.size(), cpu[2970], wall[2970],
        wall.back());
    std::fflush(stdout);
  }
  mat_destroy(e);
  std::printf("],\"checksum\":%.9g}\n", checksum);
}
