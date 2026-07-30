/* SPDX-License-Identifier: Apache-2.0 */

#include "sound_pipeline.h"

#include <math.h>
#include <stdint.h>
#include <string.h>

/* Stack buffer for dequantized embedding (prototype decode). */
#ifndef SP_PROTO_EMB_MAX
#define SP_PROTO_EMB_MAX 64
#endif

#define SP_ERR_NULL (-3)
#define SP_ERR_SIZE (-4)

size_t sound_pipeline_buffer_bytes(const sound_model_params_t *params) {
    const int frames = params ? params->input_frames : 0;
    const int mels = params ? params->input_mels : 0;
    const size_t mel_bytes = (size_t)mels * (size_t)frames * sizeof(float);
#ifdef SF_EMBEDDED
    return mel_bytes;
#else
    const size_t float_bytes = mel_bytes;
    const size_t int8_bytes = mel_bytes;
    return mel_bytes + float_bytes + int8_bytes;
#endif
}

static void *align_ptr(void *ptr, size_t alignment) {
    uintptr_t p = (uintptr_t)ptr;
    const uintptr_t mask = alignment - 1U;
    return (void *)((p + mask) & ~mask);
}

int sound_pipeline_init(
    sound_pipeline_t *pipe,
    const sound_model_params_t *params,
    void *feature_mem,
    size_t feature_mem_bytes
) {
    size_t ctx_bytes;
    size_t pipe_bytes;
    size_t total;
    uint8_t *cursor;
    int err;

    if (!pipe || !params || !feature_mem) {
        return SF_ERR_NULL_PTR;
    }

    memset(pipe, 0, sizeof(*pipe));
    pipe->params = *params;

    err = sf_config_validate(&params->feature);
    if (err != 0) {
        return err;
    }

    ctx_bytes = sf_context_buffer_bytes(&params->feature);
    pipe_bytes = sound_pipeline_buffer_bytes(params);
    total = ctx_bytes + pipe_bytes;
    if (feature_mem_bytes < total) {
        return SF_ERR_BUFFER_TOO_SMALL;
    }

    err = sf_context_init(
        &pipe->feature_ctx,
        &params->feature,
        feature_mem,
        ctx_bytes
    );
    if (err != 0) {
        return err;
    }

    cursor = (uint8_t *)feature_mem + ctx_bytes;
    cursor = (uint8_t *)align_ptr(cursor, sizeof(float));

    pipe->mel_buf = (float *)cursor;
    cursor += (size_t)params->input_mels * (size_t)params->input_frames * sizeof(float);

#ifndef SF_EMBEDDED
    pipe->model_float_buf = (float *)cursor;
    cursor += (size_t)params->input_mels * (size_t)params->input_frames * sizeof(float);

    pipe->model_int8_buf = (int8_t *)cursor;
#else
    pipe->model_float_buf = NULL;
    pipe->model_int8_buf = NULL;
#endif
    return 0;
}

void sound_pipeline_deinit(sound_pipeline_t *pipe) {
    if (!pipe) {
        return;
    }
    sf_context_deinit(&pipe->feature_ctx);
    memset(pipe, 0, sizeof(*pipe));
}

static int pipeline_prepare_float(
    sound_pipeline_t *pipe,
    const float *pcm,
    int n_pcm
) {
    const sound_model_params_t *p = &pipe->params;
    const int n_elements = p->input_frames * p->input_mels;
    int err;

    err = sf_compute_log_mel(&pipe->feature_ctx, pcm, n_pcm, pipe->mel_buf);
    if (err < 0) {
        return err;
    }

    sf_pack_model_input(
        pipe->mel_buf,
        p->input_mels,
        pipe->feature_ctx.n_frames,
        p->input_frames,
        pipe->model_float_buf
    );
    sf_normalize_mel(pipe->model_float_buf, n_elements, p->norm_mean, p->norm_std);
  return 0;
}

int sound_pipeline_pcm_float_to_input(
    sound_pipeline_t *pipe,
    const float *pcm,
    int n_pcm,
    int8_t *tflite_input,
    int tflite_input_bytes
) {
    const int needed = pipe->params.input_frames * pipe->params.input_mels;
    int err;

    if (!pipe || !pcm || !tflite_input) {
        return SF_ERR_NULL_PTR;
    }
    if (tflite_input_bytes < needed) {
        return SP_ERR_SIZE;
    }

    err = pipeline_prepare_float(pipe, pcm, n_pcm);
    if (err < 0) {
        return err;
    }

    sf_quantize_int8(
        pipe->model_float_buf,
        tflite_input,
        needed,
        pipe->params.input_scale,
        pipe->params.input_zero_point
    );
    return needed;
}

int sound_pipeline_pcm16_to_input(
    sound_pipeline_t *pipe,
    const int16_t *pcm,
    int n_pcm,
    int8_t *tflite_input,
    int tflite_input_bytes
) {
    const sound_model_params_t *p = &pipe->params;
    const int needed = p->input_frames * p->input_mels;
    int err;

    if (!pipe || !pcm || !tflite_input) {
        return SF_ERR_NULL_PTR;
    }
    if (tflite_input_bytes < needed) {
        return SP_ERR_SIZE;
    }

#ifdef SF_EMBEDDED
    err = sf_compute_log_mel_pcm16(&pipe->feature_ctx, pcm, n_pcm, pipe->mel_buf);
    if (err < 0) {
        return err;
    }

    sf_pack_normalize_quantize_int8(
        pipe->mel_buf,
        p->input_mels,
        pipe->feature_ctx.n_frames,
        p->input_frames,
        tflite_input,
        p->norm_mean,
        p->norm_std,
        p->input_scale,
        p->input_zero_point
    );
    return needed;
#else
    float *pcm_f;

    if (!pipe->feature_ctx.waveform_buf) {
        return SF_ERR_INVALID_CONFIG;
    }

    pcm_f = pipe->feature_ctx.waveform_buf;
    sf_pcm16_to_float(pcm, n_pcm, pcm_f);
    return sound_pipeline_pcm_float_to_input(
        pipe,
        pcm_f,
        n_pcm,
        tflite_input,
        tflite_input_bytes
    );
#endif
}

int sound_pipeline_decode_output(
    const sound_model_params_t *params,
    const int8_t *tflite_output,
    int tflite_output_bytes,
    int *label_index,
    float *confidence
) {
    int n_out;
    int n_tgt;
    int openset_proto;
    int openset_sigmoid;
    int openset_dual;
    int i;
    int k;
    int best;
    float best_v;
    float is_target;
    float v;
    float emb[SP_PROTO_EMB_MAX];
    float norm;
    float inv_norm;
    float sim;
    int emb_dim;
    int n_proto;
    const float *proto;

    if (!params || !tflite_output || !label_index) {
        return SF_ERR_NULL_PTR;
    }

    n_out = params->output_dim > 0 ? params->output_dim : params->num_labels;
    if (tflite_output_bytes < n_out) {
        return SP_ERR_SIZE;
    }

    n_tgt = params->num_target_labels;
    emb_dim = params->emb_dim;
    n_proto = params->num_prototypes;
    proto = params->prototypes;

    /* Prototype: embedding length == emb_dim, class means provided. */
    openset_proto = (n_tgt > 0 &&
                     proto != 0 &&
                     emb_dim > 0 &&
                     n_proto > 0 &&
                     n_out == emb_dim);
    /* Legacy: N independent sigmoids. Dual-head: N softmax + is_target. */
    openset_sigmoid = (!openset_proto && n_tgt > 0 && n_out == n_tgt);
    openset_dual = (!openset_proto && n_tgt > 0 && n_out == n_tgt + 1);

    if (openset_proto) {
        if (emb_dim > SP_PROTO_EMB_MAX) {
            return SF_ERR_INVALID_CONFIG;
        }
        if (n_proto > n_tgt && n_tgt > 0) {
            n_proto = n_tgt;
        }

        norm = 0.0f;
        for (i = 0; i < emb_dim; i++) {
            emb[i] = sf_dequantize_int8(
                tflite_output[i],
                params->output_scale,
                params->output_zero_point
            );
            norm += emb[i] * emb[i];
        }
        norm = sqrtf(norm);
        if (norm < 1e-8f) {
            *label_index = n_tgt; /* empty embedding -> other */
            if (confidence) {
                *confidence = 0.0f;
            }
            return 0;
        }
        inv_norm = 1.0f / norm;
        for (i = 0; i < emb_dim; i++) {
            emb[i] *= inv_norm;
        }

        best = 0;
        best_v = -2.0f;
        for (k = 0; k < n_proto; k++) {
            sim = 0.0f;
            for (i = 0; i < emb_dim; i++) {
                sim += emb[i] * proto[k * emb_dim + i];
            }
            if (sim > best_v) {
                best_v = sim;
                best = k;
            }
        }

        if (params->is_target_threshold > 0.0f &&
            best_v < params->is_target_threshold) {
            *label_index = n_tgt; /* synthetic "other" */
            if (confidence) {
                *confidence = best_v;
            }
            return 0;
        }
        *label_index = best;
        if (confidence) {
            *confidence = best_v;
        }
        return 0;
    }

    if (!openset_sigmoid && !openset_dual) {
        /* Legacy closed-set softmax over num_labels. */
        *label_index = sf_argmax_int8_prob(
            tflite_output,
            params->num_labels,
            params->output_scale,
            params->output_zero_point,
            confidence
        );
        if (*label_index < 0) {
            return SF_ERR_INVALID_CONFIG;
        }
        return 0;
    }

    best = 0;
    best_v = sf_dequantize_int8(
        tflite_output[0],
        params->output_scale,
        params->output_zero_point
    );
    for (i = 1; i < n_tgt; i++) {
        v = sf_dequantize_int8(
            tflite_output[i],
            params->output_scale,
            params->output_zero_point
        );
        if (v > best_v) {
            best_v = v;
            best = i;
        }
    }

    if (openset_sigmoid) {
        /* One-vs-rest: reject when no sigmoid clears the threshold. */
        if (params->is_target_threshold > 0.0f &&
            best_v < params->is_target_threshold) {
            *label_index = n_tgt; /* synthetic "other" */
            if (confidence) {
                *confidence = best_v;
            }
            return 0;
        }
        *label_index = best;
        if (confidence) {
            *confidence = best_v;
        }
        return 0;
    }

    /* Legacy dual-head: last output is P(is_target). */
    is_target = sf_dequantize_int8(
        tflite_output[n_tgt],
        params->output_scale,
        params->output_zero_point
    );
    if (params->is_target_threshold > 0.0f &&
        is_target < params->is_target_threshold) {
        *label_index = n_tgt;
        if (confidence) {
            *confidence = is_target;
        }
        return 0;
    }
    *label_index = best;
    if (confidence) {
        *confidence = best_v;
    }
    return 0;
}
