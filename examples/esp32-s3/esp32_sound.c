/* SPDX-License-Identifier: Apache-2.0 */

/**
 * ESP32-S3 sound classification template (ESP-IDF, pure C).
 *
 * Add this file to your ESP-IDF project together with:
 *   - features/src/sound_features.c + sf_fft_esp.c  (-DSF_FFT_ESP_DSP)
 *   - pipeline/src/sound_pipeline.c
 *   - esp-dsp component
 *
 * Copy model_config.h from your training export into the project include path.
 * Link your model runtime in platform_model_invoke().
 */

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "model_config.h"
#include "sound_model_params.h"
#include "sound_pipeline.h"

#ifndef SOUND_MODEL_DATA_DECLARED
#define SOUND_MODEL_DATA_DECLARED 1
extern const unsigned char sound_model_data[];
extern const unsigned int sound_model_len;
#endif

/* -------- Platform hooks (implement for ESP32-S3) -------- */

static int platform_record_pcm16(int16_t *buf, int n_samples, int sample_rate) {
    (void)buf;
    (void)n_samples;
    (void)sample_rate;
    /* TODO: I2S PDM / ADC microphone read (e.g. esp_codec_dev, driver/i2s) */
    return -1;
}

static int platform_model_invoke(
    const int8_t *input,
    int input_bytes,
    int8_t *output,
    int output_bytes
) {
    (void)input;
    (void)input_bytes;
    (void)output;
    (void)output_bytes;
    /* TODO: run your on-device model (interpreter, custom runtime, etc.) */
    return -1;
}

static void platform_log(const char *msg) {
    printf("%s\n", msg);
}

/* -------- Application -------- */

typedef struct {
    sound_pipeline_t pipe;
    sound_model_params_t params;
    uint8_t *mem;
    int16_t *pcm16;
    int8_t *model_input;
    int8_t *model_output;
} sound_app_t;

static int sound_app_init(sound_app_t *app) {
    size_t ctx_bytes;
    size_t pipe_bytes;
    size_t total;
    int n_pcm;

    memset(app, 0, sizeof(*app));
    app->params = sound_model_params_from_config();

    ctx_bytes = sf_context_buffer_bytes(&app->params.feature);
    pipe_bytes = sound_pipeline_buffer_bytes(&app->params);
    total = ctx_bytes + pipe_bytes;

    app->mem = (uint8_t *)malloc(total);
    if (!app->mem) {
        return -1;
    }
    if (sound_pipeline_init(&app->pipe, &app->params, app->mem, total) != 0) {
        return -2;
    }

    n_pcm = sf_n_samples(&app->params.feature);
    app->pcm16 = (int16_t *)malloc((size_t)n_pcm * sizeof(int16_t));
    app->model_input = (int8_t *)malloc((size_t)MODEL_INPUT_SIZE);
    app->model_output = (int8_t *)malloc((size_t)MODEL_NUM_LABELS);
    if (!app->pcm16 || !app->model_input || !app->model_output) {
        return -3;
    }
    return 0;
}

static void sound_app_deinit(sound_app_t *app) {
    if (!app) {
        return;
    }
    sound_pipeline_deinit(&app->pipe);
    free(app->pcm16);
    free(app->model_input);
    free(app->model_output);
    free(app->mem);
    memset(app, 0, sizeof(*app));
}

static int sound_app_classify_once(sound_app_t *app, int *label_index, float *confidence) {
    const int n_pcm = sf_n_samples(&app->params.feature);
    int err;

    if (platform_record_pcm16(app->pcm16, n_pcm, MODEL_SAMPLE_RATE) != 0) {
        platform_log("record failed");
        return -1;
    }

    err = sound_pipeline_pcm16_to_input(
        &app->pipe,
        app->pcm16,
        n_pcm,
        app->model_input,
        MODEL_INPUT_SIZE
    );
    if (err < 0) {
        platform_log("feature pipeline failed");
        return -2;
    }

    if (platform_model_invoke(
            app->model_input,
            MODEL_INPUT_SIZE,
            app->model_output,
            MODEL_NUM_LABELS) != 0) {
        platform_log("model invoke failed");
        return -3;
    }

    return sound_pipeline_decode_output(
        &app->params,
        app->model_output,
        MODEL_NUM_LABELS,
        label_index,
        confidence
    );
}

int esp32_sound_init(void) {
    platform_log("ESP32-S3 sound template ready.");
    (void)sound_model_data;
    (void)sound_model_len;
    return 0;
}

int esp32_sound_classify_once(int *label_index, float *confidence) {
    static sound_app_t app;
    static int initialized = 0;
    char line[96];
    int label = 0;
    float conf = 0.0f;

    if (!initialized) {
        if (sound_app_init(&app) != 0) {
            platform_log("sound_app_init failed");
            return -1;
        }
        initialized = 1;
    }

    if (sound_app_classify_once(&app, &label, &conf) != 0) {
        return -2;
    }

    if (label_index) {
        *label_index = label;
    }
    if (confidence) {
        *confidence = conf;
    }

    snprintf(
        line,
        sizeof(line),
        "label=%s index=%d confidence=%.2f",
        MODEL_LABELS[label],
        label,
        conf
    );
    platform_log(line);
    return 0;
}

#ifdef HOST_TEST
int main(void) {
    esp32_sound_init();
    esp32_sound_classify_once(NULL, NULL);
    return 0;
}
#endif
