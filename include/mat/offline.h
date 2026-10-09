#ifndef MAT_OFFLINE_H
#define MAT_OFFLINE_H
#include "mobile_audio.h"
#ifdef __cplusplus
extern "C" {
#endif
/* PCM16 / IEEE float32 WAV input, mono/stereo, 48kHz. Stereo is downmixed.
 * Writes mono PCM16 WAV; removes pipeline delay and optionally appends 4s tail.
 * Worker-thread API, bounded memory independent of file duration. Parameters
 * copied once; callbacks optional, return nonzero to cancel. Partial output is
 * removed on error. Distinct input/output paths required (including symlinks).
 */
typedef int (*mat_progress_callback)(float progress, void *user);
int mat_process_wav(const char *input_path,const char *output_path,
    const mat_parameters *parameters, mat_mode mode, int append_tail,
    mat_progress_callback progress, void *user);
/* Errors: -1 invalid argument, -2 unsupported/truncated input, -3 I/O failure,
 * -4 allocation/init failure, -5 canceled. */
#ifdef __cplusplus
}
#endif
#endif
