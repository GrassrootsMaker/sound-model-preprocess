/* SPDX-License-Identifier: Apache-2.0 */

/**
 * Helpers to build sound_model_params_t from generated model_config.h
 */
#ifndef SOUND_MODEL_PARAMS_H
#define SOUND_MODEL_PARAMS_H

#include "model_config.h"
#include "sound_pipeline.h"

static inline sound_model_params_t sound_model_params_from_config(void) {
    sound_model_params_t p;
    p.feature.sample_rate = MODEL_SAMPLE_RATE;
    p.feature.duration_sec = MODEL_DURATION_SEC;
    p.feature.n_fft = MODEL_N_FFT;
    p.feature.hop_length = MODEL_HOP_LENGTH;
    p.feature.n_mels = MODEL_N_MELS;
    p.feature.fmin = MODEL_FMIN;
    p.feature.fmax = MODEL_FMAX;
    p.input_frames = MODEL_INPUT_FRAMES;
    p.input_mels = MODEL_INPUT_MELS;
    p.norm_mean = MODEL_NORM_MEAN;
    p.norm_std = MODEL_NORM_STD;
    p.input_scale = MODEL_INPUT_SCALE;
    p.input_zero_point = MODEL_INPUT_ZERO_POINT;
    p.output_scale = MODEL_OUTPUT_SCALE;
    p.output_zero_point = MODEL_OUTPUT_ZERO_POINT;
    p.num_labels = MODEL_NUM_LABELS;
#if defined(MODEL_NUM_TARGET_LABELS)
    p.num_target_labels = MODEL_NUM_TARGET_LABELS;
#else
    p.num_target_labels = 0;
#endif
#if defined(MODEL_OUTPUT_DIM)
    p.output_dim = MODEL_OUTPUT_DIM;
#else
    p.output_dim = MODEL_NUM_LABELS;
#endif
#if defined(MODEL_IS_TARGET_THRESHOLD)
    p.is_target_threshold = MODEL_IS_TARGET_THRESHOLD;
#else
    p.is_target_threshold = 0.5f;
#endif
#if defined(MODEL_EMB_DIM) && defined(MODEL_NUM_PROTOTYPES) && (MODEL_NUM_PROTOTYPES > 0)
    /* MODEL_PROTOTYPES is a static array in model_config.h (not a #define). */
    p.emb_dim = MODEL_EMB_DIM;
    p.num_prototypes = MODEL_NUM_PROTOTYPES;
    p.prototypes = &MODEL_PROTOTYPES[0][0];
#elif defined(MODEL_EMB_DIM)
    p.emb_dim = MODEL_EMB_DIM;
    p.num_prototypes = 0;
    p.prototypes = 0;
#else
    p.emb_dim = 0;
    p.num_prototypes = 0;
    p.prototypes = 0;
#endif
    return p;
}

#endif /* SOUND_MODEL_PARAMS_H */
