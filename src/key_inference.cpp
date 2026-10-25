#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <iomanip>
#include <limits>
#include <numeric>
#include <sstream>
#include <vector>

#include "mat/key_analysis.h"

namespace mobileautotune {
namespace {
constexpr double kFrameSeconds = 0.01;
constexpr std::array<double, 12> kMajorProfile{
    6.35, 2.23, 3.48, 2.33, 4.38, 4.09, 2.52, 5.19, 2.39, 3.66, 2.29, 2.88};
constexpr std::array<double, 12> kMinorProfile{
    6.33, 2.68, 3.52, 5.38, 2.60, 3.53, 2.54, 4.75, 3.98, 2.69, 3.34, 3.17};
constexpr std::array<int, 7> kMajorNotes{0, 2, 4, 5, 7, 9, 11};
constexpr std::array<int, 7> kMinorNotes{0, 2, 3, 5, 7, 8, 10};

struct Frame {
  double midi = 0;
  double weight = 0;
  bool agreement = false;
};
struct Note {
  double start = 0;
  double end = 0;
  double weight = 0;
  int midi = 0;
};
struct Ranking {
  std::array<double, 24> scores{};
  std::array<double, 24> coverage{};
  std::array<int, 24> order{};
  std::array<double, 24> support{};
};

int PitchClass(int midi) { return (midi % 12 + 12) % 12; }
double Midi(double hz) { return 69 + 12 * std::log2(hz / 440); }

std::string JsonBackend(const char* backend, size_t capacity) {
  std::ostringstream output;
  output << '"';
  for (size_t i = 0; i < capacity && backend[i]; ++i) {
    unsigned char value = static_cast<unsigned char>(backend[i]);
    if (value == '"' || value == '\\') {
      output << '\\' << backend[i];
    } else if (value < 32 || value > 126) {
      output << "\\u00" << std::hex << std::setw(2) << std::setfill('0')
             << static_cast<int>(value);
    } else {
      output << backend[i];
    }
  }
  output << '"';
  return output.str();
}

bool ValidTrack(const mat_pitch_observation* track, size_t count,
                double duration) {
  if ((!track && count) || count > 36000000) {
    return false;
  }
  double previous = -1;
  for (size_t i = 0; i < count; ++i) {
    const auto& p = track[i];
    if (!std::isfinite(p.time_seconds) || !std::isfinite(p.frequency_hz) ||
        !std::isfinite(p.confidence) || p.time_seconds < 0 ||
        p.time_seconds <= previous || p.time_seconds > duration ||
        p.frequency_hz < 0 || p.frequency_hz > 8000 || p.confidence < 0 ||
        p.confidence > 1) {
      return false;
    }
    previous = p.time_seconds;
  }
  return true;
}

const mat_pitch_observation* At(const mat_pitch_observation* track,
                                size_t count, double time, size_t& position) {
  if (!count) {
    return nullptr;
  }
  while (position + 1 < count &&
         std::abs(track[position + 1].time_seconds - time) <
             std::abs(track[position].time_seconds - time)) {
    ++position;
  }
  return std::abs(track[position].time_seconds - time) <= .025
             ? track + position
             : nullptr;
}

std::vector<Frame> Fuse(const mat_pitch_observation* swift, size_t swift_count,
                        const mat_pitch_observation* rmvpe, size_t rmvpe_count,
                        double duration) {
  std::vector<Frame> frames(
      static_cast<size_t>(std::ceil(duration / kFrameSeconds)));
  size_t a_index = 0, b_index = 0;
  for (size_t i = 0; i < frames.size(); ++i) {
    const auto* a = At(swift, swift_count, i * kFrameSeconds, a_index);
    const auto* b = At(rmvpe, rmvpe_count, i * kFrameSeconds, b_index);
    bool a_valid = a && a->frequency_hz >= 46 && a->confidence >= .5;
    bool b_valid = b && b->frequency_hz >= 46 && b->confidence >= .1;
    if (!a_valid && !b_valid) {
      continue;
    }
    auto& frame = frames[i];
    if (a_valid && b_valid) {
      double a_midi = Midi(a->frequency_hz), b_midi = Midi(b->frequency_hz);
      double offset = a_midi - b_midi;
      double chroma_error = std::abs(offset - 12 * std::round(offset / 12));
      if (chroma_error <= .5) {
        frame.midi = (a_midi + b_midi + 12 * std::round(offset / 12)) * .5;
        frame.weight = .65 + .35 * a->confidence;
        frame.agreement = true;
      } else {
        frame.midi = a->confidence >= .85 ? a_midi : b_midi;
        frame.weight = .2;
      }
    } else {
      frame.midi = Midi(a_valid ? a->frequency_hz : b->frequency_hz);
      frame.weight = swift_count && rmvpe_count ? .4 : .7;
    }
  }
  return frames;
}

double EstimateTuning(const std::vector<Frame>& frames) {
  double x = 0, y = 0;
  for (const auto& frame : frames) {
    if (frame.weight < .4) {
      continue;
    }
    double angle = 6.283185307179586 * frame.midi;
    x += frame.weight * std::cos(angle);
    y += frame.weight * std::sin(angle);
  }
  if (x == 0 && y == 0) {
    return 0;
  }
  return std::atan2(y, x) / 6.283185307179586;
}

std::vector<Note> SegmentNotes(const std::vector<Frame>& frames,
                               double tuning) {
  std::vector<Frame> smoothed = frames;
  for (size_t i = 0; i < frames.size(); ++i) {
    if (frames[i].weight < .4) {
      continue;
    }
    std::array<double, 5> nearby{};
    size_t count = 0;
    for (size_t j = i > 2 ? i - 2 : 0; j < std::min(frames.size(), i + 3);
         ++j) {
      if (frames[j].weight >= .4) {
        nearby[count++] = frames[j].midi;
      }
    }
    std::sort(nearby.begin(), nearby.begin() + count);
    smoothed[i].midi = nearby[count / 2];
  }
  std::vector<Note> notes;
  size_t begin = 0;
  while (begin < smoothed.size()) {
    if (smoothed[begin].weight < .4) {
      ++begin;
      continue;
    }
    int midi = static_cast<int>(std::round(smoothed[begin].midi - tuning));
    size_t end = begin + 1;
    double weight = smoothed[begin].weight;
    while (end < smoothed.size() && smoothed[end].weight >= .4 &&
           std::abs(smoothed[end].midi - tuning - midi) < .65) {
      weight += smoothed[end].weight;
      ++end;
    }
    if (end - begin >= 6) {
      Note note{begin * kFrameSeconds, end * kFrameSeconds,
                weight / (end - begin), midi};
      if (!notes.empty() && notes.back().midi == midi &&
          note.start - notes.back().end <= .05) {
        notes.back().end = note.end;
        notes.back().weight = (notes.back().weight + note.weight) * .5;
      } else {
        notes.push_back(note);
      }
    }
    begin = end;
  }
  return notes;
}

bool InScale(int pitch_class, int tonic, int scale) {
  int degree = PitchClass(pitch_class - tonic);
  const auto& allowed = scale ? kMinorNotes : kMajorNotes;
  return std::find(allowed.begin(), allowed.end(), degree) != allowed.end();
}

double Correlation(const std::array<double, 12>& histogram,
                   const std::array<double, 12>& profile, int tonic) {
  double mean = std::accumulate(histogram.begin(), histogram.end(), 0.) / 12;
  double p_mean = std::accumulate(profile.begin(), profile.end(), 0.) / 12;
  double covariance = 0, variance = 0, p_variance = 0;
  for (int i = 0; i < 12; ++i) {
    double a = histogram[i] - mean;
    double b = profile[PitchClass(i - tonic)] - p_mean;
    covariance += a * b;
    variance += a * a;
    p_variance += b * b;
  }
  return covariance / std::sqrt(std::max(1e-20, variance * p_variance));
}

Ranking Rank(const std::vector<Note>& notes, double start, double end) {
  Ranking result;
  std::array<double, 12> histogram{};
  double total = 0, ending_total = 0;
  std::array<double, 12> endings{};
  for (size_t i = 0; i < notes.size(); ++i) {
    const auto& note = notes[i];
    double duration = std::min(note.end, end) - std::max(note.start, start);
    if (duration <= 0) {
      continue;
    }
    double weight = note.weight * (std::sqrt(std::min(duration, 2.)) + .2);
    histogram[PitchClass(note.midi)] += weight;
    total += weight;
    bool phrase_end =
        i + 1 == notes.size() || notes[i + 1].start - note.end >= .18;
    if (phrase_end && duration >= .12) {
      double emphasis = i + 1 == notes.size() ? 1.75 : 1;
      endings[PitchClass(note.midi)] += weight * emphasis;
      ending_total += weight * emphasis;
    }
  }
  for (int key = 0; key < 24; ++key) {
    int tonic = key % 12, scale = key / 12;
    double coverage = 0;
    for (int pc = 0; pc < 12; ++pc) {
      if (InScale(pc, tonic, scale)) {
        coverage += histogram[pc];
      }
    }
    result.coverage[key] = coverage / std::max(total, 1e-20);
    double cadence = ending_total ? endings[tonic] / ending_total : 0;
    double sequence = 0, transitions = 0;
    for (size_t i = 1; i < notes.size(); ++i) {
      if (notes[i].start < start || notes[i].start >= end) {
        continue;
      }
      int previous = PitchClass(notes[i - 1].midi - tonic);
      int current = PitchClass(notes[i].midi - tonic);
      double weight = std::min(notes[i - 1].weight, notes[i].weight);
      if (current == 0 && (previous == 7 || previous == 2 || previous == 11)) {
        sequence += weight;
      }
      transitions += weight;
    }
    result.scores[key] =
        3 * Correlation(histogram, scale ? kMinorProfile : kMajorProfile,
                        tonic) +
        3 * result.coverage[key] + 1.1 * cadence +
        .7 * sequence / std::max(1., transitions);
    result.order[key] = key;
  }
  std::stable_sort(result.order.begin(), result.order.end(), [&](int a, int b) {
    return result.scores[a] > result.scores[b];
  });
  double maximum = result.scores[result.order[0]], sum = 0;
  for (int key = 0; key < 24; ++key) {
    result.support[key] = std::exp((result.scores[key] - maximum) * 2);
    sum += result.support[key];
  }
  for (auto& support : result.support) {
    support /= sum;
  }
  return result;
}

void Infer(const std::vector<Frame>& frames, double duration, bool segment,
           mat_key_result& result) {
  result.duration_seconds = duration;
  result.status = MAT_KEY_INSUFFICIENT_MELODY;
  double tuning = EstimateTuning(frames);
  result.tuning_cents = tuning * 100;
  size_t voiced = 0, agreement = 0;
  for (const auto& frame : frames) {
    voiced += frame.weight > 0;
    agreement += frame.agreement;
  }
  result.voiced_seconds = voiced * kFrameSeconds;
  result.model_agreement = voiced ? static_cast<double>(agreement) / voiced : 0;
  auto notes = SegmentNotes(frames, tuning);
  result.note_count = static_cast<int>(notes.size());
  std::array<bool, 12> seen{};
  for (const auto& note : notes) {
    seen[PitchClass(note.midi)] = true;
  }
  result.distinct_pitch_classes =
      static_cast<int>(std::count(seen.begin(), seen.end(), true));
  if (notes.empty()) {
    return;
  }
  auto ranking = Rank(notes, 0, duration);
  result.candidate_count = 24;
  for (int i = 0; i < 24; ++i) {
    int key = ranking.order[i];
    result.candidates[i] = {key % 12, key / 12, ranking.support[key],
                            ranking.coverage[key]};
  }
  int best = ranking.order[0];
  if (result.voiced_seconds >= 2 && notes.size() >= 6 &&
      result.distinct_pitch_classes >= 3) {
    result.status =
        ranking.scores[best] - ranking.scores[ranking.order[1]] >= .4 &&
                ranking.coverage[best] >= .85 && ranking.support[best] >= .35
            ? MAT_KEY_ESTIMATED
            : MAT_KEY_AMBIGUOUS;
  }
  if (!segment) {
    return;
  }
  constexpr double kWindow = 12;
  for (double begin = 0; begin < duration && result.segment_count < 256;
       begin += kWindow) {
    double end = std::min(duration, begin + kWindow);
    if (end - begin < 4) {
      continue;
    }
    auto local = Rank(notes, begin, end);
    int key = local.order[0];
    int count = 0;
    for (const auto& note : notes) {
      count += note.start < end && note.end > begin;
    }
    if (count < 4) {
      continue;
    }
    result.segments[result.segment_count++] = {begin, end, key % 12, key / 12,
                                               local.support[key]};
    bool relative =
        (key / 12 != best / 12) &&
        PitchClass((key / 12 ? key : best) - (key / 12 ? best : key)) == 9;
    if (count >= 8 && key != best && !relative && local.support[key] >= .5 &&
        local.scores[key] - local.scores[best] > 1.2) {
      result.possible_modulation = 1;
    }
  }
  if (result.possible_modulation && result.status == MAT_KEY_ESTIMATED) {
    result.status = MAT_KEY_AMBIGUOUS;
  }
}
}  // namespace
}  // namespace mobileautotune

extern "C" {
mat_key_options mat_default_key_options(void) { return {0, MAT_KEY_AUTO, 1}; }

int mat_key_analyze_tracks(const mat_pitch_observation* swift,
                           size_t swift_count,
                           const mat_pitch_observation* rmvpe,
                           size_t rmvpe_count, double duration,
                           int detect_modulations, mat_key_result* result) {
  if (result) {
    *result = {};
  }
  if (!result || !std::isfinite(duration) || duration <= 0 ||
      duration > 86400 ||
      !mobileautotune::ValidTrack(swift, swift_count, duration) ||
      !mobileautotune::ValidTrack(rmvpe, rmvpe_count, duration)) {
    return -1;
  }
  try {
    auto frames =
        mobileautotune::Fuse(swift, swift_count, rmvpe, rmvpe_count, duration);
    mobileautotune::Infer(frames, duration, detect_modulations != 0, *result);
    return 0;
  } catch (...) {
    *result = {};
    return -4;
  }
}

size_t mat_key_result_json(const mat_key_result* result, char* buffer,
                           size_t capacity) {
  if (!result) {
    return 0;
  }
  try {
    for (double value : {result->duration_seconds, result->voiced_seconds,
                         result->tuning_cents, result->model_agreement,
                         result->elapsed_seconds}) {
      if (!std::isfinite(value)) {
        return 0;
      }
    }
    for (int i = 0; i < std::clamp(result->candidate_count, 0, 24); ++i) {
      if (!std::isfinite(result->candidates[i].support) ||
          !std::isfinite(result->candidates[i].scale_coverage)) {
        return 0;
      }
    }
    for (int i = 0; i < std::clamp(result->segment_count, 0, 256); ++i) {
      const auto& segment = result->segments[i];
      if (!std::isfinite(segment.start_seconds) ||
          !std::isfinite(segment.end_seconds) ||
          !std::isfinite(segment.support)) {
        return 0;
      }
    }
    std::ostringstream out;
    out.imbue(std::locale::classic());
    out << std::setprecision(10) << "{\"status\":" << result->status
        << ",\"durationSeconds\":" << result->duration_seconds
        << ",\"voicedSeconds\":" << result->voiced_seconds
        << ",\"tuningCents\":" << result->tuning_cents
        << ",\"modelAgreement\":" << result->model_agreement
        << ",\"elapsedSeconds\":" << result->elapsed_seconds
        << ",\"noteCount\":" << result->note_count
        << ",\"distinctPitchClasses\":" << result->distinct_pitch_classes
        << ",\"possibleModulation\":"
        << (result->possible_modulation ? "true" : "false")
        << ",\"threads\":" << result->threads << ",\"swiftBackend\":"
        << mobileautotune::JsonBackend(result->swift_backend,
                                       sizeof(result->swift_backend))
        << ",\"rmvpeBackend\":"
        << mobileautotune::JsonBackend(result->rmvpe_backend,
                                       sizeof(result->rmvpe_backend))
        << ",\"candidates\":[";
    for (int i = 0; i < std::clamp(result->candidate_count, 0, 24); ++i) {
      const auto& candidate = result->candidates[i];
      if (i) {
        out << ',';
      }
      out << "{\"tonic\":" << candidate.tonic
          << ",\"scale\":" << candidate.scale
          << ",\"support\":" << candidate.support
          << ",\"scaleCoverage\":" << candidate.scale_coverage << '}';
    }
    out << "],\"segments\":[";
    for (int i = 0; i < std::clamp(result->segment_count, 0, 256); ++i) {
      const auto& segment = result->segments[i];
      if (i) {
        out << ',';
      }
      out << "{\"startSeconds\":" << segment.start_seconds
          << ",\"endSeconds\":" << segment.end_seconds
          << ",\"tonic\":" << segment.tonic << ",\"scale\":" << segment.scale
          << ",\"support\":" << segment.support << '}';
    }
    out << "]}";
    std::string json = out.str();
    if (buffer && capacity > json.size()) {
      std::memcpy(buffer, json.c_str(), json.size() + 1);
    }
    return json.size() + 1;
  } catch (...) {
    return 0;
  }
}
}  // extern "C"
