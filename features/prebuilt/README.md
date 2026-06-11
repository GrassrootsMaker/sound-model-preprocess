# Prebuilt host libraries

Shared libraries for **PC / training** so you can skip `./build.sh` when a binary exists for your platform.

## Layout

```
prebuilt/
├── manifest.json
├── darwin-arm64/libsound_features.dylib   # macOS Apple Silicon
├── darwin-x86_64/libsound_features.dylib  # macOS Intel (build locally)
└── linux-x86_64/libsound_features.so      # Linux x86_64 (build locally)
```

Sound Model Studio (and other ctypes clients) load from `prebuilt/<platform>/` first, then fall back to `features/build/`.

## Currently shipped

| Platform ID | OS | Arch | FFT |
|-------------|-----|------|-----|
| `darwin-arm64` | macOS | Apple Silicon | builtin |

Other platforms: run `./package_prebuilt.sh` on that machine and open a PR, or use `./build.sh` locally.

## Refresh binaries (maintainers)

```bash
cd sound-model-preprocess/features
./package_prebuilt.sh          # current machine only
# or: ./package_prebuilt.sh darwin-arm64
```

Then commit the updated files under `prebuilt/` and push to GitHub.
