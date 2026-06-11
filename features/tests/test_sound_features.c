/* SPDX-License-Identifier: Apache-2.0 */

#include "sound_features.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int g_failures = 0;

static void expect_true(int condition, const char *message) {
    if (!condition) {
        fprintf(stderr, "FAIL: %s\n", message);
        g_failures += 1;
    }
}

static void expect_near(float actual, float expected, float tol, const char *message) {
    if (fabsf(actual - expected) > tol) {
        fprintf(stderr, "FAIL: %s (got %f, expected %f)\n", message, actual, expected);
        g_failures += 1;
    }
}

static void test_pad_or_trim(void) {
    const float in[] = {1.0f, 2.0f, 3.0f, 4.0f, 5.0f};
    float out[5];
    float out_short[3];

    sf_pad_or_trim(in, 5, out_short, 3);
    expect_near(out_short[0], 2.0f, 1e-6f, "trim center start");
    expect_near(out_short[1], 3.0f, 1e-6f, "trim center middle");
    expect_near(out_short[2], 4.0f, 1e-6f, "trim center end");

    sf_pad_or_trim(in, 3, out, 5);
    expect_near(out[0], 0.0f, 1e-6f, "pad left");
    expect_near(out[1], 1.0f, 1e-6f, "pad copy start");
    expect_near(out[4], 0.0f, 1e-6f, "pad right");
}

static void test_log_mel_shape(void) {
    sf_config_t cfg = {
        .sample_rate = 16000,
        .duration_sec = 1.0f,
        .n_fft = 512,
        .hop_length = 160,
        .n_mels = 40,
        .fmin = 80.0f,
        .fmax = 7600.0f,
    };
    sf_context_t ctx;
    void *buffer;
    size_t nbytes;
    float *waveform;
    float *mel;
    int i;
    int n_frames;
    float peak;

    expect_true(sf_config_validate(&cfg) == 0, "config valid");
    expect_true(sf_n_samples(&cfg) == 16000, "n_samples");
    expect_true(sf_n_frames(&cfg) == 97, "n_frames");

    nbytes = sf_context_buffer_bytes(&cfg);
    buffer = malloc(nbytes);
    expect_true(buffer != NULL, "malloc buffer");
    expect_true(sf_context_init(&ctx, &cfg, buffer, nbytes) == 0, "context init");

    waveform = (float *)malloc((size_t)sf_n_samples(&cfg) * sizeof(float));
    mel = (float *)malloc((size_t)cfg.n_mels * (size_t)sf_n_frames(&cfg) * sizeof(float));
    for (i = 0; i < sf_n_samples(&cfg); ++i) {
        waveform[i] = sinf(2.0f * 3.1415926f * 440.0f * (float)i / (float)cfg.sample_rate);
    }

    n_frames = sf_compute_log_mel(&ctx, waveform, sf_n_samples(&cfg), mel);
    expect_true(n_frames == sf_n_frames(&cfg), "compute returns n_frames");

    peak = mel[0];
    for (i = 1; i < cfg.n_mels * n_frames; ++i) {
        if (mel[i] > peak) {
            peak = mel[i];
        }
    }
    expect_near(peak, 0.0f, 1e-4f, "max mel is 0 dB");

    sf_context_deinit(&ctx);
    free(waveform);
    free(mel);
    free(buffer);
}

static void test_pack_and_quantize(void) {
    float mel[2 * 3] = {
        0.0f, 1.0f, 2.0f,
        3.0f, 4.0f, 5.0f,
    };
    float model[3 * 2];
    int8_t quant[6];

    sf_pack_model_input(mel, 2, 3, 3, model);
    expect_near(model[0], 0.0f, 1e-6f, "pack [0,0]");
    expect_near(model[1], 3.0f, 1e-6f, "pack [0,1]");
    expect_near(model[5], 5.0f, 1e-6f, "pack [2,1]");

    sf_quantize_int8(model, quant, 6, 0.1f, 0);
    expect_true(quant[0] == 0, "quant zero");
}

int main(void) {
    test_pad_or_trim();
    test_log_mel_shape();
    test_pack_and_quantize();
    if (g_failures != 0) {
        fprintf(stderr, "%d test(s) failed.\n", g_failures);
        return 1;
    }
    printf("All sound_features tests passed.\n");
    return 0;
}
