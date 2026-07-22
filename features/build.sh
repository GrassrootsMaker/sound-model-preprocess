#!/usr/bin/env bash
# Build shared library for Python on the host (macOS / Linux / Windows).
# Optional: SF_FFT_BACKEND=builtin|cmsis|esp|neon (default: builtin)
# Windows: run from Git Bash / MSYS2 / w64devkit with gcc in PATH.
set -euo pipefail

ROOT="$(cd "$(dirname "$0")" && pwd)"
BUILD_DIR="${ROOT}/build"
mkdir -p "${BUILD_DIR}"

if [[ -z "${CC:-}" ]]; then
  if command -v cc >/dev/null 2>&1; then
    CC=cc
  elif command -v gcc >/dev/null 2>&1; then
    CC=gcc
  elif command -v clang >/dev/null 2>&1; then
    CC=clang
  else
    echo "No C compiler found (cc/gcc/clang). On Windows install w64devkit or MSYS2 MinGW." >&2
    exit 1
  fi
fi

SF_FFT_BACKEND="${SF_FFT_BACKEND:-builtin}"
CFLAGS="-O2 -std=c99 -Wall -Wextra -I${ROOT}/include"
SRC_CORE="${ROOT}/src/sound_features.c"

case "${SF_FFT_BACKEND}" in
  builtin)
    FFT_SRC="${ROOT}/src/sf_fft_builtin.c"
    ;;
  cmsis)
    FFT_SRC="${ROOT}/src/sf_fft_cmsis.c"
    CFLAGS="${CFLAGS} -DSF_FFT_CMSIS"
    if [[ -z "${CMSIS_DSP_INCLUDE:-}" ]]; then
      echo "CMSIS_DSP_INCLUDE is required for SF_FFT_BACKEND=cmsis" >&2
      exit 1
    fi
    CFLAGS="${CFLAGS} -I${CMSIS_DSP_INCLUDE}"
    if [[ -n "${CMSIS_DSP_LIB:-}" ]]; then
      LDFLAGS="${LDFLAGS:-} ${CMSIS_DSP_LIB}"
    fi
    ;;
  esp)
    FFT_SRC="${ROOT}/src/sf_fft_esp.c"
    CFLAGS="${CFLAGS} -DSF_FFT_ESP_DSP"
    if [[ -n "${ESP_DSP_INCLUDE:-}" ]]; then
      CFLAGS="${CFLAGS} -I${ESP_DSP_INCLUDE}"
    fi
    if [[ -n "${ESP_DSP_LIB:-}" ]]; then
      LDFLAGS="${LDFLAGS:-} ${ESP_DSP_LIB}"
    fi
    ;;
  neon)
    FFT_SRC="${ROOT}/src/sf_fft_neon.c"
    CFLAGS="${CFLAGS} -DSF_USE_NEON"
    ARM_NEON_CFLAGS="${ARM_NEON_CFLAGS:--march=armv7-a -mfpu=neon-vfpv4 -mfloat-abi=hard}"
    CFLAGS="${CFLAGS} ${ARM_NEON_CFLAGS}"
    ;;
  *)
    echo "Unknown SF_FFT_BACKEND=${SF_FFT_BACKEND} (use builtin, cmsis, esp, neon)" >&2
    exit 1
    ;;
esac

echo "FFT backend: ${SF_FFT_BACKEND}"
echo "Compiler: ${CC}"

OS="$(uname -s 2>/dev/null || echo "${OS:-unknown}")"
case "${OS}" in
  Darwin)
    CFLAGS="${CFLAGS} -fPIC"
    OUT="${BUILD_DIR}/libsound_features.dylib"
    "${CC}" ${CFLAGS} -dynamiclib "${SRC_CORE}" "${FFT_SRC}" -o "${OUT}" -lm ${LDFLAGS:-}
    TEST_BIN="${BUILD_DIR}/test_sound_features"
    DEMO_BIN="${BUILD_DIR}/host_demo"
    ;;
  Linux)
    CFLAGS="${CFLAGS} -fPIC"
    OUT="${BUILD_DIR}/libsound_features.so"
    "${CC}" ${CFLAGS} -shared "${SRC_CORE}" "${FFT_SRC}" -o "${OUT}" -lm ${LDFLAGS:-}
    TEST_BIN="${BUILD_DIR}/test_sound_features"
    DEMO_BIN="${BUILD_DIR}/host_demo"
    ;;
  Windows_NT|MINGW*|MSYS*|CYGWIN*)
    OUT="${BUILD_DIR}/libsound_features.dll"
    "${CC}" ${CFLAGS} -shared "${SRC_CORE}" "${FFT_SRC}" -o "${OUT}" -lm ${LDFLAGS:-}
    TEST_BIN="${BUILD_DIR}/test_sound_features.exe"
    DEMO_BIN="${BUILD_DIR}/host_demo.exe"
    ;;
  *)
    echo "Unsupported OS: ${OS}. Use CMake or compile manually." >&2
    echo "Hint (Windows MinGW): gcc -O2 -std=c99 -Iinclude -shared src/sound_features.c src/sf_fft_builtin.c -o build/libsound_features.dll -lm" >&2
    exit 1
    ;;
esac

echo "Built ${OUT}"

"${CC}" -O2 -std=c99 -Wall -Wextra -I"${ROOT}/include" \
  "${ROOT}/tests/test_sound_features.c" "${SRC_CORE}" "${FFT_SRC}" \
  -o "${TEST_BIN}" -lm ${LDFLAGS:-}
"${TEST_BIN}"
echo "C tests passed."

HOST_DEMO="${ROOT}/../examples/host_demo.c"
PIPELINE_SRC="${ROOT}/../pipeline/src/sound_pipeline.c"
if [[ -f "${HOST_DEMO}" ]]; then
  "${CC}" -O2 -std=c99 -Wall -Wextra \
    -I"${ROOT}/include" -I"${ROOT}/../pipeline/include" \
    "${HOST_DEMO}" "${PIPELINE_SRC}" "${SRC_CORE}" "${FFT_SRC}" \
    -o "${DEMO_BIN}" -lm ${LDFLAGS:-}
  "${DEMO_BIN}"
  echo "host_demo passed."
fi
