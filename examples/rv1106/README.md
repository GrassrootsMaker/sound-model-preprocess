# RV1106 (Rockchip, ARMv7-A + NEON)

Cross-compile the feature library with NEON-accelerated FFT for Linux BSPs on RV1106.

## 1. Build

```bash
cd ../../features
export CC=arm-rockchip830-linux-uclibcgnueabihf-gcc   # your SDK toolchain
./build-armv7-neon.sh
# -> build-armv7/libsound_features.a
```

## 2. Integrate

1. Link `libsound_features.a` (or compile `sound_features.c` + `sf_fft_neon.c` in-tree).
2. Match `sf_config_t` to your training metadata (`model_config.h` / `model_metadata.json`).
3. Capture PCM → `sf_compute_log_mel()` → `sf_normalize_mel()`.
4. Pass the float feature tensor to your on-device inference stack.

For float-input models, **`features/`** alone is usually enough. Use **`pipeline/`** only if you need the bundled PCM → packed-buffer path.

## 3. Platform code

Implement in your application (see `examples/esp32-s3/esp32_sound.c` for a similar hook layout):

- Audio capture (ALSA, Rockchip MPI, etc.)
- Model load and inference (your chosen runtime)

C preprocessing keeps Mel settings identical to training; model execution is separate from this library.
