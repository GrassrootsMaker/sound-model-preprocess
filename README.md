# sound-model-preprocess

Portable **C99 preprocessing** for **sound AI**: turn PCM audio into model-ready features on PC and on embedded devices. Use the same implementation during **training** and **inference** so spectrograms stay consistent—regardless of which ML stack you deploy later.

Suitable for classification, event detection, bioacoustics, industrial sound monitoring, and other audio ML tasks.

```
sound-model-preprocess/
├── features/      # Log-Mel spectrogram core (sound_features)
├── pipeline/      # Optional PCM → normalized / packed model input
└── examples/      # Board integration templates
```

### features vs pipeline

| Layer | Output | Role |
|-------|--------|------|
| **features/** | Float Log-Mel (`n_mels × n_frames`) | Core preprocessing—feed any training or inference pipeline |
| **pipeline/** | Normalized or quantized input buffer | Convenience layer when your device expects a fixed input layout |

Most integrations use **`features/`** directly. Add **`pipeline/`** when you want a single C call from PCM to a packed tensor.

## PC: feature library (prebuilt or build)

**Prebuilt** shared libraries live under `features/prebuilt/<platform>/` (e.g. `darwin-arm64` for macOS Apple Silicon). Clone the repo and use them directly—no compiler required on supported platforms.

If your platform is not listed, build locally:

```bash
cd sound-model-preprocess/features
./build.sh
```

Maintainers: `./package_prebuilt.sh` rebuilds and copies into `prebuilt/` for commit.

Training tools load the library via Python `ctypes` so PC feature extraction matches the device.

## PC: full pipeline demo

```bash
cd sound-model-preprocess/examples
cc -std=c99 -O2 \
  -I../pipeline/include -I../features/include \
  host_demo.c ../pipeline/src/sound_pipeline.c ../features/src/sound_features.c \
  ../features/src/sf_fft_builtin.c -o host_demo -lm
./host_demo
```

Or run `./build.sh` in `features/` (builds the library, runs tests, and `host_demo`).

## Embedded integration

| Platform | FFT backend | Docs |
|----------|-------------|------|
| RV1106 / ARMv7 NEON | `sf_fft_neon.c` | `examples/rv1106/` |
| ESP32-S3 | `sf_fft_esp.c` | `examples/esp32-s3/` |

Minimum steps (any platform):

1. Add `features/src/sound_features.c` + one `sf_fft_*.c`
2. Match `sf_config_t` to your training metadata (`model_config.h` or equivalent)
3. `sf_compute_log_mel()` → normalize → hand off to your model runtime

Optional: `pipeline/src/sound_pipeline.c` for the bundled PCM → tensor path.

See [`features/README.md`](features/README.md) for FFT backends (builtin, CMSIS-DSP, esp-dsp, NEON).

## Metadata to keep in sync (train ↔ device)

| Artifact | Purpose |
|----------|---------|
| `model_config.h` | Sample rate, clip length, Mel bands, normalization mean/std |
| `model_metadata.json` | Same parameters in JSON |
| `labels.txt` | Class or event names |

Preprocessing parameters must match between training and deployment. Model weights and runtime are outside this repository.

## License

This repository is licensed under the [Apache License, Version 2.0](LICENSE).

Optional FFT backends (CMSIS-DSP, esp-dsp) are **not** part of this repo; if you enable them, comply with their respective licenses (see [NOTICE](NOTICE)).

> **Note:** Proprietary algorithms (e.g. custom filters) distributed separately under a commercial license are **not** covered by this open-source release.
