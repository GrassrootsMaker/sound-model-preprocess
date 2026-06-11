# ESP32-S3 (ESP-IDF)

Integration template for ESP32-S3: **C preprocessing** (`features/` + `pipeline/`) + your on-device model runtime.

## Sources to add

| Path | Notes |
|------|--------|
| `features/src/sound_features.c` | Log-Mel core |
| `features/src/sf_fft_esp.c` | FFT via esp-dsp (`-DSF_FFT_ESP_DSP`) |
| `pipeline/src/sound_pipeline.c` | PCM → model input |
| `examples/esp32-s3/esp32_sound.c` | App template (this folder) |

## ESP-IDF setup

1. Add **esp-dsp** to your project (`idf.py add-dependency` or `EXTRA_COMPONENT_DIRS`).
2. Compile `sound_features.c` and `sf_fft_esp.c` with `-DSF_FFT_ESP_DSP`.
3. Copy **`model_config.h`** from your training export into `main/` or `include/`.
4. Implement in `esp32_sound.c`:
   - `platform_record_pcm16()` — I2S / PDM microphone
   - `platform_model_invoke()` — your inference runtime

## Build flags (example)

```text
-DSF_FFT_ESP_DSP
-I path/to/sound-model-preprocess/features/include
-I path/to/sound-model-preprocess/pipeline/include
```

Link **esp-dsp** and **m**.

## Runtime

```c
esp32_sound_init();
for (;;) {
    int label;
    float confidence;
    esp32_sound_classify_once(&label, &confidence);
    vTaskDelay(pdMS_TO_TICKS(1000));
}
```

Or call `esp32_sound_classify_once()` from your `app_main` task after wiring audio and model hooks.
