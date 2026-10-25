#ifndef MOBILEAUTOTUNE_INCLUDE_MAT_KEY_ANALYSIS_H_
#define MOBILEAUTOTUNE_INCLUDE_MAT_KEY_ANALYSIS_H_

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct mat_key_analyzer mat_key_analyzer;

typedef enum {
  MAT_KEY_AUTO = 0,
  MAT_KEY_CPU = 1,
  MAT_KEY_ACCELERATED = 2
} mat_key_execution;

typedef enum {
  MAT_KEY_ESTIMATED = 0,
  MAT_KEY_AMBIGUOUS = 1,
  MAT_KEY_INSUFFICIENT_MELODY = 2
} mat_key_status;

typedef struct {
  int threads; /* 0 chooses a bounded pool from available hardware. */
  mat_key_execution execution;
  int detect_modulations;
} mat_key_options;

typedef struct {
  double time_seconds;
  float frequency_hz;
  float confidence;
} mat_pitch_observation;

typedef struct {
  int tonic;      /* C=0 ... B=11; -1 when no usable melody exists. */
  int scale;      /* 0 major, 1 minor. */
  double support; /* Relative ranking weight, not calibrated accuracy. */
  double scale_coverage;
} mat_key_candidate;

typedef struct {
  double start_seconds;
  double end_seconds;
  int tonic;
  int scale;
  double support;
} mat_key_segment;

typedef struct {
  mat_key_status status;
  int candidate_count;
  mat_key_candidate candidates[24];
  int segment_count;
  mat_key_segment segments[256];
  int possible_modulation;
  double duration_seconds;
  double voiced_seconds;
  double tuning_cents;
  double model_agreement;
  double elapsed_seconds;
  int note_count;
  int distinct_pitch_classes;
  int threads;
  char swift_backend[32];
  char rmvpe_backend[32];
} mat_key_result;

/* Worker-thread APIs. One analysis owner per handle. Models must be the pinned
 * FP32 files swift-f0.onnx and rmvpe.onnx in model_directory. No network I/O.
 * Construction and analysis must not run on an audio callback or UI thread.
 * Set cancel atomically from any thread; do not destroy during analysis. */
mat_key_options mat_default_key_options(void);
mat_key_analyzer* mat_key_create(const char* model_directory,
                                 const mat_key_options* options);
void mat_key_destroy(mat_key_analyzer* analyzer);
void mat_key_cancel(mat_key_analyzer* analyzer);
const char* mat_key_last_error(const mat_key_analyzer* analyzer);

/* Progress runs on the analysis thread; return nonzero to cancel.
 * WAV: PCM16/24/32 or IEEE float32, 8..192kHz, 1..8 channels.
 * Platform wrappers also decode supported compressed files before analysis.
 * Errors: -1 arguments, -2 file/format, -3 model/runtime, -4 allocation,
 * -5 canceled. On failure result is cleared, never partially published. */
typedef int (*mat_key_progress_callback)(float progress, void* user);
int mat_key_analyze_wav(mat_key_analyzer* analyzer, const char* path,
                        mat_key_result* result,
                        mat_key_progress_callback progress, void* user);

/* Normalized, little-endian, mono float32 at 16kHz. Suitable for platform
 * decoders to keep PCM memory bounded independently of the source file's
 * duration. */
int mat_key_analyze_pcm_file(mat_key_analyzer* analyzer, const char* path,
                             mat_key_result* result,
                             mat_key_progress_callback progress, void* user);

/* Model-independent melody inference. Tracks must be finite, chronological,
 * and use confidence 0..1. Zero Hz denotes unvoiced frames. Either track may
 * be absent. The first track uses SwiftF0 confidence; the second uses RMVPE
 * salience. Duration must cover all observations. This API has no ML runtime
 * dependency and supports externally supplied pitch estimators. */
int mat_key_analyze_tracks(const mat_pitch_observation* swift,
                           size_t swift_count,
                           const mat_pitch_observation* rmvpe,
                           size_t rmvpe_count, double duration_seconds,
                           int detect_modulations, mat_key_result* result);

/* JSON contains ranking weights and diagnostics, not confidence guarantees.
 * Returns required bytes including NUL. Null/undersized buffer is not written.
 */
size_t mat_key_result_json(const mat_key_result* result, char* buffer,
                           size_t capacity);

#ifdef __cplusplus
}
#endif
#endif  // MOBILEAUTOTUNE_INCLUDE_MAT_KEY_ANALYSIS_H_
