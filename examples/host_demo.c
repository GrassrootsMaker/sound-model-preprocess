/* SPDX-License-Identifier: Apache-2.0 */

/**
 * PC demo: synthetic PCM -> int8 model input (no TFLite required).
 * Build:
 *   cc -std=c99 -O2 -I../pipeline/include -I../features/include \
 *      host_demo.c ../pipeline/src/sound_pipeline.c \
 *      ../features/src/sound_features.c ../features/src/sf_fft_builtin.c \
 *      -o host_demo -lm
 */

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "sound_pipeline.h"

static sound_model_params_t demo_params(void) {
    sound_model_params_t p;
    memset(&p, 0, sizeof(p));
    p.feature.sample_rate = 16000;
    p.feature.duration_sec = 1.0f;
    p.feature.n_fft = 512;
    p.feature.hop_length = 160;
    p.feature.n_mels = 40;
    p.feature.fmin = 80.0f;
    p.feature.fmax = 7600.0f;
    p.input_frames = sf_n_frames(&p.feature);
    p.input_mels = p.feature.n_mels;
    p.norm_mean = 0.0f;
    p.norm_std = 1.0f;
    p.input_scale = 0.1f;
    p.input_zero_point = 0;
    p.output_scale = 0.1f;
    p.output_zero_point = 0;
    p.num_labels = 2;
    p.num_target_labels = 0;
    p.output_dim = 2;
    p.is_target_threshold = 0.0f;
    return p;
}

int main(void) {
    sound_model_params_t params = demo_params();
    sound_pipeline_t pipe;
    size_t ctx_bytes = sf_context_buffer_bytes(&params.feature);
    size_t pipe_bytes = sound_pipeline_buffer_bytes(&params);
    void *mem;
    float *pcm;
    int8_t *input;
    int n_pcm;
    int i;
    int nbytes;

    mem = malloc(ctx_bytes + pipe_bytes);
    if (!mem) {
        return 1;
    }

    if (sound_pipeline_init(&pipe, &params, mem, ctx_bytes + pipe_bytes) != 0) {
        fprintf(stderr, "pipeline init failed\n");
        free(mem);
        return 1;
    }

    n_pcm = sf_n_samples(&params.feature);
    pcm = (float *)malloc((size_t)n_pcm * sizeof(float));
    input = (int8_t *)malloc((size_t)params.input_frames * (size_t)params.input_mels);
    for (i = 0; i < n_pcm; ++i) {
        pcm[i] = sinf(2.0f * 3.1415926f * 440.0f * (float)i / (float)params.feature.sample_rate);
    }

    nbytes = sound_pipeline_pcm_float_to_input(&pipe, pcm, n_pcm, input, params.input_frames * params.input_mels);
    printf("host_demo: prepared %d int8 input values\n", nbytes);
    printf("first 8 int8: ");
    for (i = 0; i < 8 && i < nbytes; ++i) {
        printf("%d ", (int)input[i]);
    }
    printf("\n");

    sound_pipeline_deinit(&pipe);
    free(pcm);
    free(input);
    free(mem);
    return 0;
}
