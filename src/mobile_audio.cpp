#include "mat/mobile_audio.h"

#include <atomic>
#include <memory>
#include <mutex>
#include <new>

#include "effects.hpp"
#include "live_pitch.hpp"
#include "pitch.hpp"
#include "rnnoise.h"
#include "signalsmith-stretch.h"

extern "C" {
DenoiseState* mat_legacy_create(RNNModel*);
void mat_legacy_destroy(DenoiseState*);
int mat_legacy_init(DenoiseState*, RNNModel*);
float mat_legacy_process_frame(DenoiseState*, float*, const float*);
}
static_assert(std::atomic<float>::is_always_lock_free,
              "Float atomics must be lock free");
static_assert(std::atomic<uint64_t>::is_always_lock_free,
              "64-bit atomics must be lock free");
namespace {
constexpr float kRetuneDrive = 1.2f;
std::once_flag legacy_tables;
float Clamp(float x, float lo, float hi) {
  return std::max(lo, std::min(hi, x));
}
bool IsValid(const mat_parameters& p) {
  return p.tonic >= 0 && p.tonic < 12 && p.scale >= 0 && p.scale <= 2 &&
         std::isfinite(p.tuning) && p.tuning >= 0 && p.tuning <= 1.f &&
         std::isfinite(p.reverb) && p.reverb >= 0 && p.reverb <= 1 &&
         std::isfinite(p.echo) && p.echo >= 0 && p.echo <= 1 &&
         std::isfinite(p.denoise) && p.denoise >= 0 && p.denoise <= 1 &&
         std::isfinite(p.echo_ms) && p.echo_ms >= 40 && p.echo_ms <= 800 &&
         std::isfinite(p.concert_a_hz) && p.concert_a_hz >= 400 &&
         p.concert_a_hz <= 480 && std::isfinite(p.pitch_shift_semitones) &&
         p.pitch_shift_semitones >= -12 && p.pitch_shift_semitones <= 12;
}
// One control writer. Atomic payload + generation allow bounded, coherent
// reads.
struct Mailbox {
  std::atomic<uint64_t> seq{0};
  std::atomic<int> tonic{3}, scale{0};
  std::atomic<float> tuning{0.625f}, reverb{0.18f}, echo{0.08f}, denoise{0.35f},
      echo_ms{280}, concert_a{440}, pitch_shift{0};
  void Set(const mat_parameters& p) {
    seq.fetch_add(1);
    tonic.store(p.tonic);
    scale.store(p.scale);
    tuning.store(p.tuning);
    reverb.store(p.reverb);
    echo.store(p.echo);
    denoise.store(p.denoise);
    echo_ms.store(p.echo_ms);
    concert_a.store(p.concert_a_hz);
    pitch_shift.store(p.pitch_shift_semitones);
    seq.fetch_add(1);
  }
  void Read(mat_parameters& p) {
    auto before = seq.load();
    if (before & 1) {
      return;
    }
    mat_parameters next{tonic.load(),   scale.load(),     tuning.load(),
                        reverb.load(),  echo.load(),      denoise.load(),
                        echo_ms.load(), concert_a.load(), pitch_shift.load()};
    if (seq.load() == before) {
      p = next;
    }
  }
};
}  // namespace
struct mat_engine {
  mat_mode mode;
  Mailbox mailbox;
  mat_parameters params = mat_default_parameters();
  DenoiseState* noise = nullptr;
  mobileautotune::PitchTracker pitch;
  signalsmith::stretch::SignalsmithStretch<float> shifter{42};
  mobileautotune::LivePitchShifter live_shifter;
  std::unique_ptr<mobileautotune::Delay> pitch_dry, noise_dry;
  mobileautotune::Effects effects;
  std::array<float, 480> stream_input{}, stream_output{};
  int stream_pos = 0, unvoiced = 0, latency = 0;
  float shift = 0, last_target = -999, sm_denoise = 0, sm_reverb = 0,
        sm_echo = 0, sm_tune = 0, sm_voice = 0, sm_pitch_shift = 0;
  std::array<float, 5> midi_history{};
  int history_pos = 0, history_count = 0;
  std::atomic<float> hz{0}, target{0}, correction{0}, confidence{0};
  std::atomic<uint64_t> frames{0};
  explicit mat_engine(mat_mode m) : mode(m), pitch(m == MAT_OFFLINE) {
    noise = m == MAT_REALTIME ? mat_legacy_create(nullptr)
                              : rnnoise_create(nullptr);
    if (!noise) {
      throw std::bad_alloc();
    }
    std::unique_ptr<DenoiseState, void (*)(DenoiseState*)> guard(
        noise, m == MAT_REALTIME ? mat_legacy_destroy : rnnoise_destroy);
    // Upstream v0.1 allocates FFT tables on its first process call. Warm
    // under call_once during construction so callbacks neither allocate
    // nor race when multiple independent handles are created.
    if (m == MAT_REALTIME) {
      std::call_once(legacy_tables, [this] {
        std::array<float, 480> a{}, b{};
        mat_legacy_process_frame(noise, b.data(), a.data());
      });
      mat_legacy_init(noise, nullptr);
    }
    // 16ms spectral window in live mode; 80ms window for offline quality.
    shifter.configure(1, m == MAT_REALTIME ? 768 : 3840,
                      m == MAT_REALTIME ? 192 : 480);
    shifter.setFormantFactor(1, true);
    int shift_latency = m == MAT_REALTIME
                            ? mobileautotune::LivePitchShifter::kLatency
                            : shifter.inputLatency() + shifter.outputLatency();
    int noise_latency = m == MAT_REALTIME ? 480 : 960;
    latency = noise_latency + shift_latency;
    noise_dry = std::make_unique<mobileautotune::Delay>(noise_latency);
    pitch_dry = std::make_unique<mobileautotune::Delay>(shift_latency);
    sm_denoise = params.denoise;
    sm_reverb = params.reverb;
    sm_echo = params.echo;
    sm_tune = params.tuning * kRetuneDrive;
    sm_pitch_shift = params.pitch_shift_semitones;
    guard.release();
  }
  ~mat_engine() {
    if (mode == MAT_REALTIME) {
      mat_legacy_destroy(noise);
    } else {
      rnnoise_destroy(noise);
    }
  }
  void Reset() {
    if (mode == MAT_REALTIME) {
      mat_legacy_init(noise, nullptr);
    } else {
      rnnoise_init(noise, nullptr);
    }
    shifter.reset();
    live_shifter.Reset();
    pitch.Reset();
    pitch_dry->Reset();
    noise_dry->Reset();
    effects.Reset();
    stream_input.fill(0);
    stream_output.fill(0);
    stream_pos = unvoiced = 0;
    shift = 0;
    sm_voice = 0;
    last_target = -999;
    history_pos = history_count = 0;
    midi_history.fill(0);
    mailbox.Read(params);
    sm_denoise = params.denoise;
    sm_reverb = params.reverb;
    sm_echo = params.echo;
    sm_tune = params.tuning * kRetuneDrive;
    sm_pitch_shift = params.pitch_shift_semitones;
    hz = target = correction = confidence = 0;
    frames = 0;
  }
  void Process(const float* in, float* out) {
    mailbox.Read(params);
    const float retune_drive = params.tuning * kRetuneDrive;
    std::array<float, 480> raw{}, noisy{}, clean{}, mixed{}, shifted{};
    for (int i = 0; i < 480; ++i) {
      raw[i] = std::isfinite(in[i]) ? Clamp(in[i], -1, 1) : 0;
      noisy[i] = raw[i] * 32768.f;
    }
    if (mode == MAT_REALTIME) {
      mat_legacy_process_frame(noise, clean.data(), noisy.data());
    } else {
      rnnoise_process_frame(noise, clean.data(), noisy.data());
    }
    for (int i = 0; i < 480; ++i) {
      sm_denoise += 0.002f * (params.denoise - sm_denoise);
      mixed[i] = noise_dry->Tick(raw[i]) * (1 - sm_denoise) +
                 clean[i] / 32768.f * sm_denoise;
    }
    // Compute the shifted pitch analytically, then quantize in the selected
    // scale. One tracker and one shifter suffice; no second audio pass.
    sm_pitch_shift +=
        (1 - std::exp(-0.5f)) * (params.pitch_shift_semitones - sm_pitch_shift);
    pitch.Feed(mixed.data(), 480);
    pitch.Analyze();
    float desired = 0, target_hz = 0;
    bool voiced = pitch.hz() > 0 && pitch.confidence() > 0.82f;
    if (voiced) {
      unvoiced = 0;
      float midi = 69 + 12 * std::log2(pitch.hz() / params.concert_a_hz);
      midi_history[history_pos] = midi;
      history_pos = (history_pos + 1) % 5;
      history_count = std::min(history_count + 1, 5);
      if (mode == MAT_OFFLINE && history_count >= 3) {
        auto history = midi_history;
        std::sort(history.begin(), history.begin() + history_count);
        midi = history[history_count / 2];
      }
      midi += sm_pitch_shift;
      float nearest =
          mobileautotune::NearestScaleNote(midi, params.tonic, params.scale);
      // Hold near a scale boundary only if the held note is in the new scale.
      if (last_target > -900 &&
          mobileautotune::NearestScaleNote(last_target, params.tonic,
                                           params.scale) == last_target &&
          std::abs(midi - last_target) < std::abs(midi - nearest) + 0.12f) {
        nearest = last_target;
      }
      last_target = nearest;
      target_hz = params.concert_a_hz * std::exp2((nearest - 69) / 12.f);
      desired = Clamp(nearest - midi, -2, 2) * std::min(retune_drive, 1.f);
    } else if (++unvoiced >= 3) {
      last_target = -999;
      history_count = 0;
      history_pos = 0;
    }
    // Retune drive controls settling speed and note-transition hardness.
    // Pitch correction remains bounded by the selected scale note.
    float retune_ms = 2 / std::max(1.f, retune_drive) +
                      140 * std::pow(1 - std::min(retune_drive, 1.f), 2);
    float alpha = 1 - std::exp(-10.f / retune_ms);
    shift += alpha * (desired - shift);
    float total_shift = shift + sm_pitch_shift;
    const float* src[]{mixed.data()};
    float* dst[]{shifted.data()};
    if (mode == MAT_REALTIME) {
      live_shifter.Process(mixed.data(), shifted.data(), 480, total_shift,
                           pitch.hz());
    } else {
      shifter.setFormantFactor(std::exp2(sm_pitch_shift / 12.f), true);
      shifter.setTransposeSemitones(total_shift, 8000.f / 48000);
      shifter.setFormantBase(pitch.hz() / 48000.f);
      shifter.process(src, 480, dst, 480);
    }
    for (int i = 0; i < 480; ++i) {
      float dry = pitch_dry->Tick(mixed[i]);
      sm_tune += 0.002f * (retune_drive - sm_tune);
      // Preserve clean unvoiced consonants: stopped variable read heads can
      // otherwise comb-filter unvoiced audio even at a zero shift.
      sm_voice += 0.002f * ((voiced ? 1.f : 0.f) - sm_voice);
      float mechanical = Clamp(sm_tune - 1, 0, 0.2f);
      float voice = sm_voice + mechanical * ((voiced ? 1.f : 0.f) - sm_voice);
      float tune_active =
          Clamp(sm_tune * 100, 0, 1) * (mode == MAT_REALTIME ? voice : 1.f);
      // Independent transposition also shifts unvoiced consonants and
      // works with autotune disabled or unavailable pitch detection.
      float pitch_active = Clamp(std::abs(sm_pitch_shift) * 100, 0, 1);
      float active = pitch_active + (1 - pitch_active) * tune_active;
      float x = dry * (1 - active) + shifted[i] * active;
      sm_reverb += 0.002f * (params.reverb - sm_reverb);
      sm_echo += 0.002f * (params.echo - sm_echo);
      out[i] = effects.Tick(x, sm_reverb, sm_echo, params.echo_ms);
    }
    if (target_hz > 0 && sm_pitch_shift != 0 && params.tuning == 0) {
      target_hz = pitch.hz() * std::exp2(sm_pitch_shift / 12.f);
    }
    hz.store(pitch.hz());
    target.store(target_hz);
    correction.store(total_shift);
    confidence.store(pitch.confidence());
    frames.fetch_add(480);
  }
};
extern "C" {
mat_parameters mat_default_parameters() {
  return {3, MAT_MAJOR, 0.625f, 0.18f, 0.08f, 0.35f, 280, 440, 0};
}
mat_engine* mat_create(int sr, mat_mode mode) {
  if (sr != 48000 || (mode != MAT_REALTIME && mode != MAT_OFFLINE)) {
    return nullptr;
  }
  try {
    return new mat_engine(mode);
  } catch (...) {
    return nullptr;
  }
}
void mat_destroy(mat_engine* e) { delete e; }
int mat_set_parameters(mat_engine* e, const mat_parameters* p) {
  if (!e || !p || !IsValid(*p)) {
    return -1;
  }
  e->mailbox.Set(*p);
  return 0;
}
int mat_process_frame(mat_engine* e, const float* in, float* out) {
  if (!e || !in || !out) {
    return -1;
  }
  e->Process(in, out);
  return 0;
}
int mat_process(mat_engine* e, const float* in, float* out, int n) {
  if (!e || !in || !out || n < 0) {
    return -1;
  }
  for (int i = 0; i < n; ++i) {
    e->stream_input[e->stream_pos] = in[i];
    out[i] = e->stream_output[e->stream_pos];
    if (e->stream_pos == 479) {
      e->Process(e->stream_input.data(), e->stream_output.data());
    }
    if (++e->stream_pos == 480) {
      e->stream_pos = 0;
    }
  }
  return 0;
}
void mat_reset(mat_engine* e) {
  if (e) {
    e->Reset();
  }
}
int mat_latency_samples(const mat_engine* e) { return e ? e->latency : 0; }
int mat_stream_latency_samples(const mat_engine* e) {
  return e ? e->latency + 480 : 0;
}
int mat_max_history_samples(const mat_engine* e) {
  return e ? (e->mode == MAT_REALTIME
                  ? 480 + mobileautotune::LivePitchShifter::kMaximumDelay
                  : e->latency)
           : 0;
}
int mat_stream_max_history_samples(const mat_engine* e) {
  return e ? mat_max_history_samples(e) + 480 : 0;
}
int mat_tail_samples(const mat_engine* e) { return e ? 48000 * 4 : 0; }
mat_telemetry mat_get_telemetry(const mat_engine* e) {
  if (!e) {
    return {};
  }
  return {e->hz.load(), e->target.load(), e->correction.load(),
          e->confidence.load(), e->frames.load()};
}
const char* mat_version() { return "0.4.0"; }
}
