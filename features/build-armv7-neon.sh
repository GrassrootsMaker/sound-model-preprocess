#!/usr/bin/env bash
# Cross-compile static lib for ARMv7-A + NEON (Rockchip RV1106, etc.).
#
# Example (Rockchip / Buildroot SDK):
#   export CC=arm-rockchip830-linux-uclibcgnueabihf-gcc
#   ./build-armv7-neon.sh
#
# Or generic arm-linux-gnueabihf:
#   CC=arm-linux-gnueabihf-gcc ./build-armv7-neon.sh
set -euo pipefail

ROOT="$(cd "$(dirname "$0")" && pwd)"
BUILD_DIR="${ROOT}/build-armv7"
mkdir -p "${BUILD_DIR}"

CC="${CC:-arm-linux-gnueabihf-gcc}"
SF_FFT_BACKEND=neon
export SF_FFT_BACKEND

ARM_NEON_CFLAGS="${ARM_NEON_CFLAGS:--march=armv7-a -mfpu=neon-vfpv4 -mfloat-abi=hard -O2}"
CFLAGS="-std=c99 -Wall -Wextra -I${ROOT}/include -DSF_USE_NEON ${ARM_NEON_CFLAGS}"
SRC_CORE="${ROOT}/src/sound_features.c"
FFT_SRC="${ROOT}/src/sf_fft_neon.c"

echo "Cross CC: ${CC}"
echo "FFT backend: neon"
echo "CFLAGS: ${CFLAGS}"

"${CC}" ${CFLAGS} -c "${SRC_CORE}" -o "${BUILD_DIR}/sound_features.o"
"${CC}" ${CFLAGS} -c "${FFT_SRC}" -o "${BUILD_DIR}/sf_fft_neon.o"
ar rcs "${BUILD_DIR}/libsound_features.a" \
  "${BUILD_DIR}/sound_features.o" "${BUILD_DIR}/sf_fft_neon.o"

"${CC}" ${CFLAGS} \
  "${ROOT}/tests/test_sound_features.c" \
  "${BUILD_DIR}/sound_features.o" "${BUILD_DIR}/sf_fft_neon.o" \
  -o "${BUILD_DIR}/test_sound_features" -lm
"${BUILD_DIR}/test_sound_features"
echo "ARMv7 NEON tests passed."

echo "Built ${BUILD_DIR}/libsound_features.a"
