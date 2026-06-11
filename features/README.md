# features — Log-Mel preprocessing (sound_features)

**Pure C99** Log-Mel front-end for sound AI. No dependency on a specific training framework or inference engine.

- **PC**: build `.dylib` / `.so`; call from Python `ctypes` or your training pipeline
- **Embedded**: add `include/sound_features.h` + `src/sound_features.c` to firmware or Linux BSP projects

## Algorithm (training / inference parity)

1. `pad_or_trim` to a fixed sample count  
2. Hann-window STFT, power spectrum (magnitude²)  
3. Slaney mel filterbank  
4. `10*log10(power)` with reference = global maximum (peak = 0 dB)

Time frames: `n_frames = 1 + (n_samples - n_fft) / hop_length`

## PC library: prebuilt or build

Check **`prebuilt/<platform>/`** first (see [`prebuilt/README.md`](prebuilt/README.md)). Sound Model Studio loads prebuilt binaries when available.

To compile locally (or refresh prebuilt):

```bash
cd sound-model-preprocess/features
chmod +x build.sh package_prebuilt.sh
./build.sh
```

Outputs:

- `build/libsound_features.dylib` (macOS) or `build/libsound_features.so` (Linux)
- Runs C unit tests automatically

Package for GitHub:

```bash
./package_prebuilt.sh    # copies build/ → prebuilt/<this-machine-platform>/
```

Or use CMake:

```bash
cmake -S . -B build-cmake
cmake --build build-cmake
ctest --test-dir build-cmake
```

### FFT backend (compile option)

STFT uses a pluggable FFT implementation. Link **one** of `src/sf_fft_*.c` with `sound_features.c`:

| Backend | Flag / env | When to use |
|---------|------------|-------------|
| **builtin** (default) | — | PC, any target without DSP libs |
| **CMSIS-DSP** | `SF_FFT_BACKEND=cmsis` | ARM Cortex-M (STM32, nRF, …) |
| **esp-dsp** | `SF_FFT_BACKEND=esp` | ESP32 / ESP-IDF |
| **ARM NEON** | `SF_FFT_BACKEND=neon` | Cortex-A / RV1106 Linux (`armv7-a` + NEON) |

**build.sh (PC, builtin):**

```bash
./build.sh
SF_FFT_BACKEND=cmsis CMSIS_DSP_INCLUDE=/path/to/CMSIS-DSP/Include \
  CMSIS_DSP_LIB="-L/path/to/Lib/GCC -larm_cortexM4lf_math" ./build.sh
```

**CMake:**

```bash
cmake -S . -B build-cmsis \
  -DSF_FFT_BACKEND=cmsis \
  -DCMSIS_DSP_INCLUDE=/path/to/CMSIS-DSP/Include \
  -DCMSIS_DSP_LIB=/path/to/libarm_cortexM4lf_math.a
cmake --build build-cmsis
```

**ESP-IDF component:** add `sound_features.c` + `sf_fft_esp.c`, depend on `esp-dsp`, compile with `-DSF_FFT_ESP_DSP`.

**Rockchip RV1106 (ARMv7 + NEON):**

```bash
export CC=arm-rockchip830-linux-uclibcgnueabihf-gcc   # your SDK toolchain
chmod +x build-armv7-neon.sh
./build-armv7-neon.sh
# -> build-armv7/libsound_features.a
```

Link the static library into your app, compute Log-Mel in C, normalize with exported mean/std, then pass the float feature map to your on-device model. NEON also accelerates Hann multiply and mel dot products (`-DSF_USE_NEON`).

At runtime, `sf_fft_backend_name()` returns `"builtin"`, `"cmsis"`, `"esp-dsp"`, or `"neon"`.

## Integration example

```c
#include "sound_features.h"

static uint8_t sf_buffer[/* sf_context_buffer_bytes(&cfg) */];

sf_config_t cfg = {
    .sample_rate = 16000,
    .duration_sec = 5.0f,
    .n_fft = 512,
    .hop_length = 160,
    .n_mels = 40,
    .fmin = 80.0f,
    .fmax = 7600.0f,
};

sf_context_t ctx;
sf_context_init(&ctx, &cfg, sf_buffer, sizeof sf_buffer);

float mel[40 * 497];  /* n_mels * n_frames */
sf_compute_log_mel(&ctx, pcm_float, pcm_len, mel);

/* Normalize with mean/std from training metadata, then inference */
sf_normalize_mel(mel, 40 * 497, mean, std);
```

Cross-compile example (GNU Arm Embedded + CMSIS):

```bash
arm-none-eabi-gcc -std=c99 -O2 -DSF_FFT_CMSIS -Iinclude -I/path/to/CMSIS-DSP/Include \
  -c src/sound_features.c -c src/sf_fft_cmsis.c
```

## Why C?

| Language | Fit | Notes |
|----------|-----|-------|
| **C** | Best | Embedded Linux, MCUs, Python ctypes, cross-platform training parity |
| C++ | OK | Needs `extern "C"`; slightly more ABI friction on embedded |
| Rust | OK | `no_std` + bindings; heavier on some Arduino paths |

One C implementation for Mel features on PC and on device.
