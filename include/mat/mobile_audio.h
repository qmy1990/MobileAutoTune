#ifndef MAT_MOBILE_AUDIO_H
#define MAT_MOBILE_AUDIO_H
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
/* Mono, normalized float32 PCM, 48000 Hz. A handle has one processing owner.
 * Control changes may be sent concurrently from one control thread.
 * Create/reset/destroy must never run concurrently with processing.
 */
typedef struct mat_engine mat_engine;
typedef enum { MAT_REALTIME = 0, MAT_OFFLINE = 1 } mat_mode;
typedef enum { MAT_MAJOR = 0, MAT_MINOR = 1, MAT_CHROMATIC = 2 } mat_scale;
typedef struct {
    int tonic;             /* C=0, C#=1, ... Eb=3, A=9 */
    int scale;             /* major, natural minor, chromatic */
    float tuning;          /* 0..1.2; 1=hard, 1.2=enhanced mechanical retune */
    float reverb;          /* wet amount 0..1 */
    float echo;            /* wet amount 0..1 */
    float denoise;         /* aligned dry/denoised mix 0..1 */
    float echo_ms;         /* 40..800 ms */
    float concert_a_hz;    /* 400..480 Hz, default 440 */
    float pitch_shift_semitones; /* -12..12, default0; negative=lower, positive=higher */
} mat_parameters;
typedef struct {
    float input_pitch_hz;
    float target_pitch_hz;
    float correction_semitones;
    float pitch_confidence;
    uint64_t processed_frames;
} mat_telemetry;
mat_parameters mat_default_parameters(void);
mat_engine *mat_create(int sample_rate, mat_mode mode);
void mat_destroy(mat_engine *engine);
int mat_set_parameters(mat_engine *engine, const mat_parameters *parameters);
/* Exactly 480 samples/10ms, input and output may alias. No allocation/locks/I/O.
 * Fixed pipeline latency even at strength zero, to avoid jumps during automation.
 * Returns 0 on success; invalid pointers return -1. */
int mat_process_frame(mat_engine *engine, const float *input, float *output);
/* Accepts arbitrary frame counts (e.g. hardware callbacks); adds 480 samples of
 * adapter latency. In-place supported. Use consistently after reset.
 * Never mix this function with mat_process_frame on the same stream. */
int mat_process(mat_engine *engine, const float *input, float *output, int frames);
void mat_reset(mat_engine *engine);
int mat_latency_samples(const mat_engine *engine); /* frame API */
int mat_stream_latency_samples(const mat_engine *engine); /* arbitrary-size API */
/* Conservative maximum audio-history age; live pitch heads move around the
 * nominal center. Excludes hardware/driver/scheduling latency. */
int mat_max_history_samples(const mat_engine *engine);
int mat_stream_max_history_samples(const mat_engine *engine);
int mat_tail_samples(const mat_engine *engine); /* recommended maximum tail */
mat_telemetry mat_get_telemetry(const mat_engine *engine);
const char *mat_version(void);
#ifdef __cplusplus
}
#endif
#endif
