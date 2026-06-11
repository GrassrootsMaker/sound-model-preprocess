/* SPDX-License-Identifier: Apache-2.0 */

/**
 * CMSIS-DSP real FFT backend (ARM Cortex-M).
 *
 * Build with -DSF_FFT_CMSIS and link CMSIS-DSP, e.g.:
 *   -I${CMSIS_DSP}/Include
 *   -L${CMSIS_DSP}/Lib/GCC -larm_cortexM4lf_math
 */
#include "sf_fft.h"

#include <stdint.h>
#include <string.h>

#include "arm_math.h"

const char *sf_fft_backend_name(void) {
    return "cmsis";
}

size_t sf_fft_work_bytes(int n_fft) {
    return sizeof(arm_rfft_fast_instance_f32)
        + (size_t)(n_fft + (n_fft + 2)) * sizeof(float);
}

int sf_fft_init(void *work, int n_fft) {
    arm_rfft_fast_instance_f32 *instance = (arm_rfft_fast_instance_f32 *)work;
    if (arm_rfft_fast_init_f32(instance, (uint16_t)n_fft) != ARM_MATH_SUCCESS) {
        return -1;
    }
    return 0;
}

void sf_fft_power_spectrum(
    void *work,
    const float *windowed_frame,
    int n_fft,
    int n_freq_bins,
    float *power_out
) {
    const arm_rfft_fast_instance_f32 *instance = (const arm_rfft_fast_instance_f32 *)work;
    float *in_buf = (float *)((uint8_t *)work + sizeof(arm_rfft_fast_instance_f32));
    float *out_buf = in_buf + n_fft;
    int bin;

    memcpy(in_buf, windowed_frame, (size_t)n_fft * sizeof(float));
    arm_rfft_fast_f32(instance, in_buf, out_buf, 0);

    power_out[0] = out_buf[0] * out_buf[0];
    if (n_freq_bins > n_fft / 2) {
        power_out[n_fft / 2] = out_buf[1] * out_buf[1];
    }
    for (bin = 1; bin < n_fft / 2 && bin < n_freq_bins; ++bin) {
        const float re = out_buf[2 * bin];
        const float im = out_buf[2 * bin + 1];
        power_out[bin] = re * re + im * im;
    }
}
