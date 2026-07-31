/* SPDX-License-Identifier: Apache-2.0 */

/**
 * Log-mel spectrogram — shared by PC training and ARM inference.
 *
 * Pipeline: pad_or_trim -> Hann window STFT (power=2) -> Slaney mel bank
 *           -> power_to_db(ref=max).
 *
 * STFT uses center=False framing:
 *   n_frames = 1 + (n_samples - n_fft) / hop_length
 */

#include "sound_features.h"
#include "sf_fft.h"
#include "sf_neon.h"

#include <math.h>
#include <stdint.h>
#include <string.h>

#define SF_MAX_MELS 128

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

#define SF_AMIN 1.0e-10f

static int is_power_of_two(int n) {
    return n > 0 && (n & (n - 1)) == 0;
}

static float hz_to_mel_slaney(float hz) {
    const float f_min = 0.0f;
    const float f_sp = 200.0f / 3.0f;
    const float min_log_hz = 1000.0f;
    const float min_log_mel = (min_log_hz - f_min) / f_sp;
    const float logstep = logf(6.4f) / 27.0f;

    if (hz >= min_log_hz) {
        return min_log_mel + logf(hz / min_log_hz) / logstep;
    }
    return (hz - f_min) / f_sp;
}

static float mel_to_hz_slaney(float mel) {
    const float f_min = 0.0f;
    const float f_sp = 200.0f / 3.0f;
    const float min_log_hz = 1000.0f;
    const float min_log_mel = (min_log_hz - f_min) / f_sp;
    const float logstep = logf(6.4f) / 27.0f;

    if (mel >= min_log_mel) {
        return min_log_hz * expf(logstep * (mel - min_log_mel));
    }
    return f_min + f_sp * mel;
}

int sf_n_samples(const sf_config_t *cfg) {
    if (!cfg) {
        return 0;
    }
    return (int)(cfg->sample_rate * cfg->duration_sec);
}

int sf_n_frames(const sf_config_t *cfg) {
    const int n_samples = sf_n_samples(cfg);
    if (!cfg || n_samples < cfg->n_fft) {
        return 1;
    }
    return 1 + (n_samples - cfg->n_fft) / cfg->hop_length;
}

int sf_config_validate(const sf_config_t *cfg) {
    if (!cfg) {
        return SF_ERR_NULL_PTR;
    }
    if (cfg->sample_rate <= 0 || cfg->duration_sec <= 0.0f) {
        return SF_ERR_INVALID_CONFIG;
    }
    if (!is_power_of_two(cfg->n_fft)) {
        return SF_ERR_INVALID_CONFIG;
    }
    if (cfg->hop_length <= 0 || cfg->n_mels <= 0) {
        return SF_ERR_INVALID_CONFIG;
    }
    if (cfg->fmin < 0.0f || cfg->fmax <= cfg->fmin) {
        return SF_ERR_INVALID_CONFIG;
    }
    if (cfg->fmax > cfg->sample_rate * 0.5f) {
        return SF_ERR_INVALID_CONFIG;
    }
    if (sf_n_samples(cfg) < cfg->n_fft) {
        return SF_ERR_INVALID_CONFIG;
    }
    return 0;
}

void sf_pad_or_trim(
    const float *waveform,
    int n_waveform_samples,
    float *out,
    int n_out
) {
    if (!out || n_out <= 0) {
        return;
    }
    if (!waveform || n_waveform_samples <= 0) {
        memset(out, 0, (size_t)n_out * sizeof(float));
        return;
    }
    if (n_waveform_samples >= n_out) {
        const int start = (n_waveform_samples - n_out) / 2;
        memcpy(out, waveform + start, (size_t)n_out * sizeof(float));
        return;
    }
    const int pad = n_out - n_waveform_samples;
    const int pad_left = pad / 2;
    const int pad_right = pad - pad_left;
    memset(out, 0, (size_t)pad_left * sizeof(float));
    memcpy(out + pad_left, waveform, (size_t)n_waveform_samples * sizeof(float));
    memset(out + pad_left + n_waveform_samples, 0, (size_t)pad_right * sizeof(float));
}

void sf_normalize_mel(float *mel, int n_elements, float mean, float std) {
    int i;
    const float denom = std + 1.0e-6f;
    if (!mel || n_elements <= 0) {
        return;
    }
    for (i = 0; i < n_elements; ++i) {
        mel[i] = (mel[i] - mean) / denom;
    }
}

static void build_hann_window(float *window, int n_fft) {
    int i;
    for (i = 0; i < n_fft; ++i) {
        window[i] = 0.5f - 0.5f * cosf(2.0f * (float)M_PI * (float)i / (float)n_fft);
    }
}

static int build_mel_filterbank(const sf_config_t *cfg, float *filters, int n_freq_bins) {
    const int n_mels = cfg->n_mels;
    const int n_fft = cfg->n_fft;
    const float sr = (float)cfg->sample_rate;
    float mel_points[SF_MAX_MELS + 2];
    float hz_points[SF_MAX_MELS + 2];
    int bin_points[SF_MAX_MELS + 2];
    int m, k;

    if (n_mels + 2 > SF_MAX_MELS + 2) {
        return SF_ERR_INVALID_CONFIG;
    }

    for (m = 0; m < n_mels + 2; ++m) {
        const float mel_min = hz_to_mel_slaney(cfg->fmin);
        const float mel_max = hz_to_mel_slaney(cfg->fmax);
        mel_points[m] = mel_min + ((mel_max - mel_min) * (float)m / (float)(n_mels + 1));
        hz_points[m] = mel_to_hz_slaney(mel_points[m]);
        bin_points[m] = (int)floorf((n_fft + 1) * hz_points[m] / sr);
        if (bin_points[m] < 0) {
            bin_points[m] = 0;
        }
        if (bin_points[m] > n_freq_bins - 1) {
            bin_points[m] = n_freq_bins - 1;
        }
    }

    memset(filters, 0, (size_t)n_mels * (size_t)n_freq_bins * sizeof(float));

    for (m = 0; m < n_mels; ++m) {
        const int left = bin_points[m];
        const int center = bin_points[m + 1];
        const int right = bin_points[m + 2];
        const float enorm = 2.0f / (hz_points[m + 2] - hz_points[m]);

        for (k = left; k < center && k < n_freq_bins; ++k) {
            if (center != left) {
                filters[m * n_freq_bins + k] = (float)(k - left) / (float)(center - left);
            }
        }
        for (k = center; k < right && k < n_freq_bins; ++k) {
            if (right != center) {
                filters[m * n_freq_bins + k] = (float)(right - k) / (float)(right - center);
            }
        }
        for (k = 0; k < n_freq_bins; ++k) {
            filters[m * n_freq_bins + k] *= enorm;
        }
    }

    (void)n_fft;
    return 0;
}

size_t sf_context_buffer_bytes(const sf_config_t *cfg) {
    const int n_samples = sf_n_samples(cfg);
    const int n_frames = sf_n_frames(cfg);
    const int n_freq_bins = cfg->n_fft / 2 + 1;
    size_t bytes = 0;

    bytes += (size_t)cfg->n_mels * (size_t)n_freq_bins * sizeof(float);
    bytes += (size_t)cfg->n_fft * sizeof(float);
#ifndef SF_EMBEDDED
    bytes += (size_t)n_samples * sizeof(float);
#endif
    bytes += (size_t)cfg->n_fft * sizeof(float);
    bytes += sizeof(float) - 1U; /* align before fft_work */
    bytes += sf_fft_work_bytes(cfg->n_fft);
    bytes += (size_t)n_freq_bins * sizeof(float);
    (void)n_frames;
    (void)n_samples; /* unused when SF_EMBEDDED drops the waveform buffer */
    return bytes;
}

static void *align_ptr(void *ptr, size_t alignment) {
    uintptr_t p = (uintptr_t)ptr;
    const uintptr_t mask = alignment - 1U;
    return (void *)((p + mask) & ~mask);
}

int sf_context_init(sf_context_t *ctx, const sf_config_t *cfg, void *buffer, size_t buffer_bytes) {
    const int err = sf_config_validate(cfg);
    uint8_t *cursor;
    size_t needed;

    if (!ctx || !cfg || !buffer) {
        return SF_ERR_NULL_PTR;
    }
    if (err != 0) {
        return err;
    }

    memset(ctx, 0, sizeof(*ctx));
    ctx->cfg = *cfg;
    ctx->n_samples = sf_n_samples(cfg);
    ctx->n_frames = sf_n_frames(cfg);
    ctx->n_freq_bins = cfg->n_fft / 2 + 1;

    needed = sf_context_buffer_bytes(cfg);
    if (buffer_bytes < needed) {
        return SF_ERR_BUFFER_TOO_SMALL;
    }

    cursor = (uint8_t *)align_ptr(buffer, sizeof(float));

    ctx->mel_filters = (float *)cursor;
    cursor += (size_t)cfg->n_mels * (size_t)ctx->n_freq_bins * sizeof(float);

    ctx->hann_window = (float *)cursor;
    cursor += (size_t)cfg->n_fft * sizeof(float);

#ifndef SF_EMBEDDED
    ctx->waveform_buf = (float *)cursor;
    cursor += (size_t)ctx->n_samples * sizeof(float);
#else
    ctx->waveform_buf = NULL;
#endif

    ctx->frame_buf = (float *)cursor;
    cursor += (size_t)cfg->n_fft * sizeof(float);

    cursor = (uint8_t *)align_ptr(cursor, sizeof(float));
    ctx->fft_work = (void *)cursor;
    cursor += sf_fft_work_bytes(cfg->n_fft);

    ctx->power_spec = (float *)cursor;

    build_hann_window(ctx->hann_window, cfg->n_fft);
    build_mel_filterbank(cfg, ctx->mel_filters, ctx->n_freq_bins);
    return sf_fft_init(ctx->fft_work, cfg->n_fft);
}

void sf_context_deinit(sf_context_t *ctx) {
    if (!ctx) {
        return;
    }
    memset(ctx, 0, sizeof(*ctx));
}

int sf_compute_log_mel(
    sf_context_t *ctx,
    const float *waveform,
    int n_waveform_samples,
    float *mel_out
) {
    int frame;
    float max_power;
    const int n_mels = ctx->cfg.n_mels;
    const int n_fft = ctx->cfg.n_fft;
    const int hop = ctx->cfg.hop_length;
    const int n_freq_bins = ctx->n_freq_bins;

    if (!ctx || !mel_out) {
        return SF_ERR_NULL_PTR;
    }
    if (sf_config_validate(&ctx->cfg) != 0) {
        return SF_ERR_INVALID_CONFIG;
    }

    sf_pad_or_trim(waveform, n_waveform_samples, ctx->waveform_buf, ctx->n_samples);
    memset(mel_out, 0, (size_t)n_mels * (size_t)ctx->n_frames * sizeof(float));

    for (frame = 0; frame < ctx->n_frames; ++frame) {
        const int offset = frame * hop;
        int mel;

        sf_neon_mul_f32(
            ctx->waveform_buf + offset,
            ctx->hann_window,
            ctx->frame_buf,
            n_fft
        );
        sf_fft_power_spectrum(
            ctx->fft_work,
            ctx->frame_buf,
            n_fft,
            n_freq_bins,
            ctx->power_spec
        );

        for (mel = 0; mel < n_mels; ++mel) {
            const float *weights = ctx->mel_filters + mel * n_freq_bins;
            mel_out[mel * ctx->n_frames + frame] = sf_neon_dot_f32(
                weights,
                ctx->power_spec,
                n_freq_bins
            );
        }
    }

    max_power = 0.0f;
    for (frame = 0; frame < n_mels * ctx->n_frames; ++frame) {
        if (mel_out[frame] > max_power) {
            max_power = mel_out[frame];
        }
    }
    if (max_power < SF_AMIN) {
        max_power = SF_AMIN;
    }

    {
        const float ref_db = 10.0f * log10f(max_power);
        for (frame = 0; frame < n_mels * ctx->n_frames; ++frame) {
            float value = mel_out[frame];
            if (value < SF_AMIN) {
                value = SF_AMIN;
            }
            mel_out[frame] = 10.0f * log10f(value) - ref_db;
        }
    }

    return ctx->n_frames;
}

static float padded_pcm16_sample(
    const int16_t *pcm,
    int n_pcm,
    int n_out,
    int idx
) {
    if (!pcm || idx < 0 || idx >= n_out) {
        return 0.0f;
    }
    if (n_pcm >= n_out) {
        const int start = (n_pcm - n_out) / 2;
        return (float)pcm[start + idx] / 32768.0f;
    }

    {
        const int pad_left = (n_out - n_pcm) / 2;
        if (idx < pad_left || idx >= pad_left + n_pcm) {
            return 0.0f;
        }
        return (float)pcm[idx - pad_left] / 32768.0f;
    }
}

int sf_compute_log_mel_pcm16(
    sf_context_t *ctx,
    const int16_t *pcm,
    int n_pcm_samples,
    float *mel_out
) {
    int frame;
    float max_power;
    const int n_mels = ctx->cfg.n_mels;
    const int n_fft = ctx->cfg.n_fft;
    const int hop = ctx->cfg.hop_length;
    const int n_freq_bins = ctx->n_freq_bins;

    if (!ctx || !mel_out) {
        return SF_ERR_NULL_PTR;
    }
    if (sf_config_validate(&ctx->cfg) != 0) {
        return SF_ERR_INVALID_CONFIG;
    }

    memset(mel_out, 0, (size_t)n_mels * (size_t)ctx->n_frames * sizeof(float));

    for (frame = 0; frame < ctx->n_frames; ++frame) {
        const int offset = frame * hop;
        int mel;
        int i;

        for (i = 0; i < n_fft; ++i) {
            ctx->frame_buf[i] = padded_pcm16_sample(
                pcm,
                n_pcm_samples,
                ctx->n_samples,
                offset + i
            );
        }
        sf_neon_mul_f32(ctx->frame_buf, ctx->hann_window, ctx->frame_buf, n_fft);
        sf_fft_power_spectrum(
            ctx->fft_work,
            ctx->frame_buf,
            n_fft,
            n_freq_bins,
            ctx->power_spec
        );

        for (mel = 0; mel < n_mels; ++mel) {
            const float *weights = ctx->mel_filters + mel * n_freq_bins;
            mel_out[mel * ctx->n_frames + frame] = sf_neon_dot_f32(
                weights,
                ctx->power_spec,
                n_freq_bins
            );
        }
    }

    max_power = 0.0f;
    for (frame = 0; frame < n_mels * ctx->n_frames; ++frame) {
        if (mel_out[frame] > max_power) {
            max_power = mel_out[frame];
        }
    }
    if (max_power < SF_AMIN) {
        max_power = SF_AMIN;
    }

    {
        const float ref_db = 10.0f * log10f(max_power);
        for (frame = 0; frame < n_mels * ctx->n_frames; ++frame) {
            float value = mel_out[frame];
            if (value < SF_AMIN) {
                value = SF_AMIN;
            }
            mel_out[frame] = 10.0f * log10f(value) - ref_db;
        }
    }

    return ctx->n_frames;
}

void sf_pcm16_to_float(const int16_t *pcm, int n_samples, float *out) {
    int i;
    if (!out) {
        return;
    }
    if (!pcm || n_samples <= 0) {
        if (n_samples > 0) {
            memset(out, 0, (size_t)n_samples * sizeof(float));
        }
        return;
    }
    for (i = 0; i < n_samples; ++i) {
        out[i] = (float)pcm[i] / 32768.0f;
    }
}

float sf_pcm16_peak(const int16_t *pcm, int n_samples) {
    int i;
    int16_t peak = 0;

    if (!pcm || n_samples <= 0) {
        return 0.0f;
    }
    for (i = 0; i < n_samples; ++i) {
        int16_t v = pcm[i];
        if (v < 0) {
            /* Avoid UB on INT16_MIN: -(-32768) does not fit in int16_t. */
            v = (int16_t)((v == (int16_t)-32768) ? 32767 : -v);
        }
        if (v > peak) {
            peak = v;
        }
    }
    return (float)peak / 32768.0f;
}

float sf_float_peak(const float *samples, int n_samples) {
    int i;
    float peak = 0.0f;

    if (!samples || n_samples <= 0) {
        return 0.0f;
    }
    for (i = 0; i < n_samples; ++i) {
        float v = samples[i];
        if (v < 0.0f) {
            v = -v;
        }
        if (v > peak) {
            peak = v;
        }
    }
    return peak;
}

int sf_pcm16_is_active(const int16_t *pcm, int n_samples, float min_peak) {
    if (min_peak <= 0.0f) {
        return 1;
    }
    if (!pcm || n_samples <= 0) {
        return 0;
    }
    return sf_pcm16_peak(pcm, n_samples) >= min_peak ? 1 : 0;
}

int sf_float_is_active(const float *samples, int n_samples, float min_peak) {
    if (min_peak <= 0.0f) {
        return 1;
    }
    if (!samples || n_samples <= 0) {
        return 0;
    }
    return sf_float_peak(samples, n_samples) >= min_peak ? 1 : 0;
}

void sf_pack_normalize_quantize_int8(
    const float *mel,
    int n_mels,
    int n_frames,
    int target_frames,
    int8_t *model_input,
    float mean,
    float std,
    float scale,
    int zero_point
) {
    int t;
    int m;
    const int copy_frames = n_frames < target_frames ? n_frames : target_frames;
    const float inv_std = std > 0.0f ? (1.0f / std) : 1.0f;
    const float inv_scale = scale > 0.0f ? (1.0f / scale) : 1.0f;

    if (!model_input || n_mels <= 0 || target_frames <= 0) {
        return;
    }
    memset(model_input, 0, (size_t)target_frames * (size_t)n_mels);
    if (!mel || n_frames <= 0) {
        return;
    }
    for (t = 0; t < copy_frames; ++t) {
        for (m = 0; m < n_mels; ++m) {
            float v = (mel[m * n_frames + t] - mean) * inv_std;
            int q = (int)lroundf(v * inv_scale) + zero_point;
            if (q < -128) {
                q = -128;
            } else if (q > 127) {
                q = 127;
            }
            model_input[t * n_mels + m] = (int8_t)q;
        }
    }
}

void sf_pack_model_input(
    const float *mel,
    int n_mels,
    int n_frames,
    int target_frames,
    float *model_input
) {
    int t;
    int m;
    const int copy_frames = n_frames < target_frames ? n_frames : target_frames;

    if (!model_input || n_mels <= 0 || target_frames <= 0) {
        return;
    }
    memset(model_input, 0, (size_t)target_frames * (size_t)n_mels * sizeof(float));
    if (!mel || n_frames <= 0) {
        return;
    }
    for (t = 0; t < copy_frames; ++t) {
        for (m = 0; m < n_mels; ++m) {
            model_input[t * n_mels + m] = mel[m * n_frames + t];
        }
    }
}

void sf_quantize_int8(
    const float *src,
    int8_t *dst,
    int n_elements,
    float scale,
    int zero_point
) {
    int i;
    const float inv_scale = scale > 0.0f ? (1.0f / scale) : 1.0f;
    if (!src || !dst || n_elements <= 0) {
        return;
    }
    for (i = 0; i < n_elements; ++i) {
        int q = (int)lroundf(src[i] * inv_scale) + zero_point;
        if (q < -128) {
            q = -128;
        } else if (q > 127) {
            q = 127;
        }
        dst[i] = (int8_t)q;
    }
}

float sf_dequantize_int8(int8_t value, float scale, int zero_point) {
    return ((float)value - (float)zero_point) * scale;
}

static int argmax_with_softmax(
    const float *values,
    int n,
    float *confidence_out
) {
    int i;
    int best = 0;
    float max_v;
    float sum;

    if (!values || n <= 0) {
        return -1;
    }
    max_v = values[0];
    for (i = 1; i < n; ++i) {
        if (values[i] > max_v) {
            max_v = values[i];
            best = i;
        }
    }
    if (!confidence_out) {
        return best;
    }
    sum = 0.0f;
    for (i = 0; i < n; ++i) {
        sum += expf(values[i] - max_v);
    }
    if (sum <= 0.0f) {
        *confidence_out = 0.0f;
    } else {
        *confidence_out = expf(values[best] - max_v) / sum;
    }
    return best;
}

int sf_argmax_float(const float *values, int n, float *confidence_out) {
    return argmax_with_softmax(values, n, confidence_out);
}

int sf_argmax_int8(
    const int8_t *values,
    int n,
    float scale,
    int zero_point,
    float *confidence_out
) {
    int i;
    int best = 0;
    float best_v;
    float stack_buf[64];

    if (!values || n <= 0) {
        return -1;
    }

    best_v = sf_dequantize_int8(values[0], scale, zero_point);
    for (i = 1; i < n; ++i) {
        const float v = sf_dequantize_int8(values[i], scale, zero_point);
        if (v > best_v) {
            best_v = v;
            best = i;
        }
    }

    if (!confidence_out) {
        return best;
    }
    if (n > (int)(sizeof(stack_buf) / sizeof(stack_buf[0]))) {
        *confidence_out = 0.0f;
        return best;
    }
    for (i = 0; i < n; ++i) {
        stack_buf[i] = sf_dequantize_int8(values[i], scale, zero_point);
    }
    return argmax_with_softmax(stack_buf, n, confidence_out);
}

int sf_argmax_int8_prob(
    const int8_t *values,
    int n,
    float scale,
    int zero_point,
    float *confidence_out
) {
    int i;
    int best = 0;
    float best_v;

    if (!values || n <= 0) {
        return -1;
    }

    best_v = sf_dequantize_int8(values[0], scale, zero_point);
    for (i = 1; i < n; ++i) {
        const float v = sf_dequantize_int8(values[i], scale, zero_point);
        if (v > best_v) {
            best_v = v;
            best = i;
        }
    }

    if (confidence_out) {
        /* Output is already a softmax probability; clamp to [0, 1]. */
        if (best_v < 0.0f) {
            best_v = 0.0f;
        } else if (best_v > 1.0f) {
            best_v = 1.0f;
        }
        *confidence_out = best_v;
    }
    return best;
}
