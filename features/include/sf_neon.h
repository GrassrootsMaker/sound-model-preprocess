/* SPDX-License-Identifier: Apache-2.0 */

/**
 * Optional ARM NEON helpers for sound_features (mel dot product, window multiply).
 * Enabled with -DSF_USE_NEON when SF_FFT_BACKEND=neon.
 */
#ifndef SF_NEON_H
#define SF_NEON_H

#if defined(SF_USE_NEON) && (defined(__ARM_NEON) || defined(__ARM_NEON__))
#include <arm_neon.h>

static inline float sf_neon_dot_f32(const float *a, const float *b, int n) {
    float32x4_t sum = vdupq_n_f32(0.0f);
    int i = 0;

    for (; i + 3 < n; i += 4) {
        sum = vmlaq_f32(sum, vld1q_f32(a + i), vld1q_f32(b + i));
    }
    {
        float32x2_t s2 = vadd_f32(vget_low_f32(sum), vget_high_f32(sum));
        s2 = vpadd_f32(s2, s2);
        float total = vget_lane_f32(s2, 0);
        for (; i < n; ++i) {
            total += a[i] * b[i];
        }
        return total;
    }
}

static inline void sf_neon_mul_f32(const float *a, const float *b, float *out, int n) {
    int i = 0;

    for (; i + 3 < n; i += 4) {
        vst1q_f32(out + i, vmulq_f32(vld1q_f32(a + i), vld1q_f32(b + i)));
    }
    for (; i < n; ++i) {
        out[i] = a[i] * b[i];
    }
}

#else

static inline float sf_neon_dot_f32(const float *a, const float *b, int n) {
    float total = 0.0f;
    int i;
    for (i = 0; i < n; ++i) {
        total += a[i] * b[i];
    }
    return total;
}

static inline void sf_neon_mul_f32(const float *a, const float *b, float *out, int n) {
    int i;
    for (i = 0; i < n; ++i) {
        out[i] = a[i] * b[i];
    }
}

#endif

#endif /* SF_NEON_H */
