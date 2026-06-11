/* SPDX-License-Identifier: Apache-2.0 */

/**
 * ARM NEON radix-2 FFT (Cortex-A7/A9, RV1106 Linux, AArch32).
 *
 * Build with SF_FFT_BACKEND=neon and ARM flags, e.g.:
 *   -march=armv7-a -mfpu=neon-vfpv4 -mfloat-abi=hard -DSF_USE_NEON
 */
#include "sf_fft.h"

#include <math.h>
#include <string.h>

#if !defined(__ARM_NEON) && !defined(__ARM_NEON)
#error "sf_fft_neon.c requires ARM NEON (use an ARMv7-A+ cross compiler)"
#endif

#include <arm_neon.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

typedef struct {
    int n_fft;
    float *real;
    float *imag;
    float *twiddle_r;
    float *twiddle_i;
} sf_fft_neon_hdr_t;

const char *sf_fft_backend_name(void) {
    return "neon";
}

size_t sf_fft_work_bytes(int n_fft) {
    const size_t n_twiddle = (size_t)(n_fft - 1);
    return sizeof(sf_fft_neon_hdr_t)
        + (size_t)n_fft * 2U * sizeof(float)
        + n_twiddle * 2U * sizeof(float);
}

static void precompute_twiddles(float *tw_r, float *tw_i, int n_fft) {
    int len;

    for (len = 2; len <= n_fft; len <<= 1) {
        const int half = len >> 1;
        const float angle = -2.0f * (float)M_PI / (float)len;
        const float wlen_r = cosf(angle);
        const float wlen_i = sinf(angle);
        float w_r = 1.0f;
        float w_i = 0.0f;
        int j;

        for (j = 0; j < half; ++j) {
            tw_r[j] = w_r;
            tw_i[j] = w_i;
            {
                const float next_r = w_r * wlen_r - w_i * wlen_i;
                w_i = w_r * wlen_i + w_i * wlen_r;
                w_r = next_r;
            }
        }
        tw_r += half;
        tw_i += half;
    }
}

int sf_fft_init(void *work, int n_fft) {
    sf_fft_neon_hdr_t *hdr = (sf_fft_neon_hdr_t *)work;
    uint8_t *cursor = (uint8_t *)work + sizeof(sf_fft_neon_hdr_t);
    const size_t n_twiddle = (size_t)(n_fft - 1);

    hdr->n_fft = n_fft;
    hdr->real = (float *)cursor;
    cursor += (size_t)n_fft * sizeof(float);
    hdr->imag = (float *)cursor;
    cursor += (size_t)n_fft * sizeof(float);
    hdr->twiddle_r = (float *)cursor;
    cursor += n_twiddle * sizeof(float);
    hdr->twiddle_i = (float *)cursor;

    precompute_twiddles(hdr->twiddle_r, hdr->twiddle_i, n_fft);
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

static void fft_inplace_neon(sf_fft_neon_hdr_t *hdr) {
    float *real = hdr->real;
    float *imag = hdr->imag;
    const int n = hdr->n_fft;
    float *tw_r = hdr->twiddle_r;
    float *tw_i = hdr->twiddle_i;
    int len;

    bit_reverse_perm(real, imag, n);

    for (len = 2; len <= n; len <<= 1) {
        const int half = len >> 1;
        int i;

        for (i = 0; i < n; i += len) {
            int j = 0;

            for (; j + 3 < half; j += 4) {
                const float32x4_t w_r = vld1q_f32(tw_r + j);
                const float32x4_t w_i = vld1q_f32(tw_i + j);
                const float32x4_t u_r = vld1q_f32(real + i + j);
                const float32x4_t u_i = vld1q_f32(imag + i + j);
                const float32x4_t o_r = vld1q_f32(real + i + j + half);
                const float32x4_t o_i = vld1q_f32(imag + i + j + half);
                const float32x4_t v_r = vsubq_f32(vmulq_f32(o_r, w_r), vmulq_f32(o_i, w_i));
                const float32x4_t v_i = vaddq_f32(vmulq_f32(o_r, w_i), vmulq_f32(o_i, w_r));

                vst1q_f32(real + i + j, vaddq_f32(u_r, v_r));
                vst1q_f32(imag + i + j, vaddq_f32(u_i, v_i));
                vst1q_f32(real + i + j + half, vsubq_f32(u_r, v_r));
                vst1q_f32(imag + i + j + half, vsubq_f32(u_i, v_i));
            }
            for (; j < half; ++j) {
                const float w_r = tw_r[j];
                const float w_i = tw_i[j];
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
            }
        }
        tw_r += half;
        tw_i += half;
    }
}

static void power_spectrum_neon(
    const float *real,
    const float *imag,
    int n_freq_bins,
    float *power_out
) {
    int bin = 0;

    for (; bin + 3 < n_freq_bins; bin += 4) {
        const float32x4_t re = vld1q_f32(real + bin);
        const float32x4_t im = vld1q_f32(imag + bin);
        vst1q_f32(power_out + bin, vaddq_f32(vmulq_f32(re, re), vmulq_f32(im, im)));
    }
    for (; bin < n_freq_bins; ++bin) {
        const float re = real[bin];
        const float im = imag[bin];
        power_out[bin] = re * re + im * im;
    }
}

void sf_fft_power_spectrum(
    void *work,
    const float *windowed_frame,
    int n_fft,
    int n_freq_bins,
    float *power_out
) {
    sf_fft_neon_hdr_t *hdr = (sf_fft_neon_hdr_t *)work;

    memcpy(hdr->real, windowed_frame, (size_t)n_fft * sizeof(float));
    memset(hdr->imag, 0, (size_t)n_fft * sizeof(float));
    fft_inplace_neon(hdr);
    power_spectrum_neon(hdr->real, hdr->imag, n_freq_bins, power_out);
}
