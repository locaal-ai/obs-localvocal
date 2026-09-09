# Flatpak Build Configuration - OBS LocalVocal

Flatpak manifest for the OBS LocalVocal plugin using system dependencies where possible to reduce build time.

## 📁 Files

### Flatpak Configuration

| File                                                   | Description                               |
| ------------------------------------------------------ | ----------------------------------------- |
| `com.obsproject.Studio.Plugin.LocalVocal.yaml`         | ⚙️ Main Flatpak manifest                  |
| `com.obsproject.Studio.Plugin.LocalVocal.metainfo.xml` | 📋 AppStream metadata                     |
| `cargo-sources.json`                                   | 📦 Vendored Rust dependencies             |
| `build.sh`                                             | 🔨 Build script                           |

## 🎯 Quick Start

### Build the plugin

```sh
# Using build script (default: generic CPU build)
./flatpak/build.sh build-dir

# With GPU acceleration
ACCELERATION=nvidia ./flatpak/build.sh build-dir # CUDA
ACCELERATION=amd    ./flatpak/build.sh build-dir # ROCm/HIP

# Or manually with flatpak-builder
flatpak-builder --force-clean --repo=repo build \
  flatpak/com.obsproject.Studio.Plugin.LocalVocal.yaml
```

## 📊 Key Features

### SDK and Dependencies

| Component | Version                    | Notes                      |
| --------- | -------------------------- | -------------------------- |
| **SDK**   | org.freedesktop.Sdk//25.08 | Same as OBS Studio         |
| **Rust**  | SDK extension (1.94.0)     | Saves 15-20 min build time |
| **ICU**   | SDK system library         | Saves 8-10 min build time  |

### Library Versions

All libraries use versions with native CMake 3.x/4.x support:

- **OpenBLAS** v0.3.32 (CMake 3.16+)
- **OpenCL-Headers** v2025.07.22 (CMake 3.16+)
- **CTranslate2** v4.7.1 (CMake 3.7+)
  - cpu_features v0.10.1 (CMake 3.13+)
  - spdlog v1.17.0 (CMake 3.10+)
- **whisper.cpp** v1.8.4
- **SentencePiece** v0.2.1

## 🔍 Technical Details

### Build Architecture

```text
com.obsproject.Studio (runtime)
├── org.freedesktop.Sdk//25.08
│   ├── sdk-extensions
│   │   └── rust-stable (1.94.0)
│   └── System libraries
│       └── ICU (system)
└── LocalVocal Extension
    ├── Compiled modules
    │   ├── OpenBLAS
    │   ├── whisper.cpp
    │   ├── CTranslate2
    │   ├── SentencePiece
    │   └── OpenCL-Headers
    └── Prebuilt binaries
        ├── whispercpp-prebuilt
        └── onnxruntime-prebuilt
```

## 🧪 Testing

### Verify build environment

```sh
# Check Flatpak configuration
flatpak remotes --show-details
flatpak list --runtime

# Verify SDK extension availability
flatpak search org.freedesktop.Sdk.Extension.rust-stable
```

### Test build with timing

```sh
# Full build with timing
time flatpak-builder --force-clean build \
  flatpak/com.obsproject.Studio.Plugin.LocalVocal.yaml

# Partial build (stop at specific module)
flatpak-builder --force-clean --stop-at=obs-localvocal build \
  flatpak/com.obsproject.Studio.Plugin.LocalVocal.yaml
```

## 🐛 Troubleshooting

### Common Issues

| Error                    | Solution                                                                   |
| ------------------------ | -------------------------------------------------------------------------- |
| Rust extension not found | `flatpak install flathub org.freedesktop.Sdk.Extension.rust-stable//25.08` |
| CMake version errors     | Update library versions in manifest (already done)                         |
| UIC wrapper fails        | Check Qt installation in SDK with `flatpak-builder --run ... which uic`    |

### Debug build

```sh
# Verbose build
flatpak-builder -v --force-clean build manifest.yaml

# Debug specific module
flatpak-builder --force-clean --stop-at=MODULE build manifest.yaml
flatpak-builder --run build manifest.yaml bash
```

## 📜 License

See the `LICENSE` file in the project root.

---

**Last updated:** May 5, 2026
