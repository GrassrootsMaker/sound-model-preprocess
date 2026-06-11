/* SPDX-License-Identifier: Apache-2.0 */

/**
 * Default in-tree radix-2 FFT (no external dependency).
 */
#include "sf_fft.h"

#include <math.h>
#include <string.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

typedef struct {
    float *real;
    float *imag;
} sf_fft_builtin_state_t;

const char *sf_fft_backend_name(void) {
    return "builtin";
}

size_t sf_fft_work_bytes(int n_fft) {
    return (size_t)n_fft * sizeof(float) * 2U;
}

int sf_fft_init(void *work, int n_fft) {
    (void)work;
    (void)n_fft;
    return 0;
}

static void bit_reverse_perm(float *real, float *imag, int n) {
    int i, j, k;
    for (i = 1, j = 0; i < n; ++i) {
        for (k = n >> 1; !((j ^= k) & k); k >>= 1) {
        }
        if (i < j) {
            const float tr = real[i];
            const float ti = imag[i];
            real[i] = real[j];
            imag[i] = imag[j];
            real[j] = tr;
            imag[j] = ti;
        }
    }
}

static void fft_inplace(float *real, float *imag, int n) {
    int len, half, i, j;
    bit_reverse_perm(real, imag, n);
    for (len = 2; len <= n; len <<= 1) {
        const float angle = -2.0f * (float)M_PI / (float)len;
        const float wlen_r = cosf(angle);
        const float wlen_i = sinf(angle);
        half = len >> 1;
        for (i = 0; i < n; i += len) {
            float w_r = 1.0f;
            float w_i = 0.0f;
            for (j = 0; j < half; ++j) {
                const int even = i + j;
                const int odd = i + j + half;
                const float u_r = real[even];
                const float u_i = imag[even];
                const float v_r = real[odd] * w_r - imag[odd] * w_i;
                const float v_i = real[odd] * w_i + imag[odd] * w_r;
                real[even] = u_r + v_r;
                imag[even] = u_i + v_i;
                real[odd] = u_r - v_r;
                imag[odd] = u_i - v_i;
                const float next_w_r = w_r * wlen_r - w_i * wlen_i;
                w_i = w_r * wlen_i + w_i * wlen_r;
                w_r = next_w_r;
            }
        }
    }
}

void sf_fft_power_spectrum(
    void *work,
    const float *windowed_frame,
    int n_fft,
    int n_freq_bins,
    float *power_out
) {
    sf_fft_builtin_state_t st;
    int bin;

    st.real = (float *)work;
    st.imag = st.real + n_fft;
    memcpy(st.real, windowed_frame, (size_t)n_fft * sizeof(float));
    memset(st.imag, 0, (size_t)n_fft * sizeof(float));
    fft_inplace(st.real, st.imag, n_fft);

    for (bin = 0; bin < n_freq_bins; ++bin) {
        const float re = st.real[bin];
        const float im = st.imag[bin];
        power_out[bin] = re * re + im * im;
    }
}
