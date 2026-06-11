/* SPDX-License-Identifier: Apache-2.0 */

/**
 * Portable log-mel feature extraction for PC and embedded targets.
 *
 * Single source of truth for training (Python via shared lib) and on-device
 * inference. Pure C99, no heap required when using sf_context.
 */

#ifndef SOUND_FEATURES_H
#define SOUND_FEATURES_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stddef.h>
#include <stdint.h>

#define SF_VERSION "1.0.0"

#define SF_ERR_INVALID_CONFIG (-1)
#define SF_ERR_BUFFER_TOO_SMALL (-2)
#define SF_ERR_NULL_PTR (-3)

typedef struct {
    int sample_rate;
    float duration_sec;
    int n_fft;
    int hop_length;
    int n_mels;
    float fmin;
    float fmax;
} sf_config_t;

typedef struct {
    sf_config_t cfg;
    int n_samples;
    int n_frames;
    int n_freq_bins;

    float *mel_filters;
    float *hann_window;
    float *waveform_buf;
    float *frame_buf;
    void *fft_work;
    float *power_spec;
} sf_context_t;

/** Active FFT backend: "builtin", "cmsis", "esp-dsp", or "neon". */
const char *sf_fft_backend_name(void);

/** Number of audio samples after pad/trim for the given config. */
int sf_n_samples(const sf_config_t *cfg);

/** Number of mel time frames for the given config. */
int sf_n_frames(const sf_config_t *cfg);

/** Validate config; returns 0 on success, negative error code otherwise. */
int sf_config_validate(const sf_config_t *cfg);

/**
 * Bytes needed for sf_context buffers (excluding the sf_context_t struct itself).
 * Pass to malloc() or use a static arena on embedded targets.
 */
size_t sf_context_buffer_bytes(const sf_config_t *cfg);

/** Initialize context buffers; caller must zero-initialize *ctx first. */
int sf_context_init(sf_context_t *ctx, const sf_config_t *cfg, void *buffer, size_t buffer_bytes);

/** Release references (does not free the external buffer). */
void sf_context_deinit(sf_context_t *ctx);

/**
 * Pad (center) or trim waveform to cfg duration, then compute log-mel.
 *
 * @param mel_out      Output buffer, size >= n_mels * n_frames floats.
 *                     Layout: mel band major, shape [n_mels][n_frames].
 * @return n_frames on success, negative error code on failure.
 */
int sf_compute_log_mel(
    sf_context_t *ctx,
    const float *waveform,
    int n_waveform_samples,
    float *mel_out
);

/** Center pad or trim waveform to exactly n_out samples. */
void sf_pad_or_trim(
    const float *waveform,
    int n_waveform_samples,
    float *out,
    int n_out
);

/** In-place z-score: mel[i] = (mel[i] - mean) / std. */
void sf_normalize_mel(float *mel, int n_elements, float mean, float std);

/** Convert mono PCM int16 (-32768..32767) to float32 (-1..1). */
void sf_pcm16_to_float(const int16_t *pcm, int n_samples, float *out);

/**
 * Layout transform: mel[m][t] -> model[t][m] (time-major, no channel dim).
 * Output length = target_frames * n_mels; extra frames are zero-padded.
 */
void sf_pack_model_input(
    const float *mel,
    int n_mels,
    int n_frames,
    int target_frames,
    float *model_input
);

/** Quantize float tensor to int8 for TFLite input. */
void sf_quantize_int8(
    const float *src,
    int8_t *dst,
    int n_elements,
    float scale,
    int zero_point
);

/** Dequantize one int8 value. */
float sf_dequantize_int8(int8_t value, float scale, int zero_point);

/** Return index of largest value; optional confidence = softmax max prob. */
int sf_argmax_float(const float *values, int n, float *confidence_out);

/** Argmax on dequantized int8 logits with optional softmax confidence. */
int sf_argmax_int8(
    const int8_t *values,
    int n,
    float scale,
    int zero_point,
    float *confidence_out
);

#ifdef __cplusplus
}
#endif

#endif /* SOUND_FEATURES_H */
