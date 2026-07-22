#!/usr/bin/env bash
# Build sound_features and copy the shared library into features/prebuilt/<platform>/.
set -euo pipefail

ROOT="$(cd "$(dirname "$0")" && pwd)"
cd "${ROOT}"

platform_id() {
  local os arch
  os="$(uname -s 2>/dev/null || echo "${OS:-unknown}")"
  arch="$(uname -m 2>/dev/null || echo x86_64)"
  case "${os}" in
    Darwin)
      case "${arch}" in
        arm64) echo "darwin-arm64" ;;
        x86_64) echo "darwin-x86_64" ;;
        *) echo "unsupported-darwin-${arch}" >&2; return 1 ;;
      esac
      ;;
    Linux)
      case "${arch}" in
        x86_64) echo "linux-x86_64" ;;
        aarch64|arm64) echo "linux-arm64" ;;
        armv7l|armv6l) echo "linux-armv7" ;;
        *) echo "unsupported-linux-${arch}" >&2; return 1 ;;
      esac
      ;;
    Windows_NT|MINGW*|MSYS*|CYGWIN*)
      echo "windows-x86_64"
      ;;
    *)
      echo "Unsupported OS: ${os}" >&2
      return 1
      ;;
  esac
}

TARGET_PLATFORM="${1:-$(platform_id)}"
echo "Building for platform id: ${TARGET_PLATFORM}"
./build.sh

case "${TARGET_PLATFORM}" in
  darwin-*)
    LIB_NAME="libsound_features.dylib"
    ;;
  linux-*)
    LIB_NAME="libsound_features.so"
    ;;
  windows-*)
    LIB_NAME="libsound_features.dll"
    ;;
  *)
    echo "Unknown platform id: ${TARGET_PLATFORM}" >&2
    exit 1
    ;;
esac

DEST="${ROOT}/prebuilt/${TARGET_PLATFORM}"
mkdir -p "${DEST}"
cp "${ROOT}/build/${LIB_NAME}" "${DEST}/${LIB_NAME}"
echo "Installed ${DEST}/${LIB_NAME}"
ls -la "${DEST}/${LIB_NAME}"
