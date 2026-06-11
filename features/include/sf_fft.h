/* SPDX-License-Identifier: Apache-2.0 */

/**
 * Internal STFT FFT backend (builtin, CMSIS-DSP, esp-dsp, or ARM NEON).
 * Link exactly one src/sf_fft_*.c implementation.
 */
#ifndef SF_FFT_H
#define SF_FFT_H

#include <stddef.h>

/** Human-readable backend id, e.g. "builtin", "cmsis", "esp-dsp", "neon". */
const char *sf_fft_backend_name(void);

/** Work-buffer bytes required inside sf_context (aligned). */
size_t sf_fft_work_bytes(int n_fft);

/** Prepare backend state in work (n_fft must be a power of two). Returns 0 or negative. */
int sf_fft_init(void *work, int n_fft);

/**
 * Real windowed frame -> power spectrum (magnitude squared), bins [0 .. n_freq_bins-1].
 * windowed_frame length = n_fft; power_out length = n_freq_bins (= n_fft/2 + 1).
 */
void sf_fft_power_spectrum(
    void *work,
    const float *windowed_frame,
    int n_fft,
    int n_freq_bins,
    float *power_out
);

#endif /* SF_FFT_H */
