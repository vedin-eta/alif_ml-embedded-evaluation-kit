# Build Optimization Guide

This guide explains how to use the build optimization features to significantly speed up compilation times.

## Overview

The build has been optimized to:
1. **Disable unused features** (Camera, LCD) - saves ~30-40% build time
2. **Use prebuilt libraries** - saves ~60-70% build time on incremental builds
3. **Enable ccache** - caches compiled objects for even faster rebuilds

## Quick Start

### First Build (Full)
```bash
./build_minimal.sh
```

This will:
- Build with Camera and LCD disabled
- Compile all dependencies from source
- Offer to copy prebuilt libraries after successful build

### Subsequent Builds (Fast)
```bash
./build_minimal.sh
```

If prebuilt libraries exist, this will:
- Reuse prebuilt TensorFlow Lite Micro, CMSIS-NN, CMSIS-DSP
- Only rebuild your application code and HAL
- Build 3-5x faster than full builds

## Manual Usage

### Option 1: Using the automated script (Recommended)
```bash
# First build
./build_minimal.sh

# After first build, copy libraries
./copy_prebuilt_libs.sh

# Subsequent fast builds
./build_minimal.sh

# Force a full rebuild
./build_minimal.sh --full

# Clean everything and start fresh
./build_minimal.sh --clean
```

### Option 2: Manual cmake commands

**First build:**
```bash
cmake -B build_hp_infrun \
    -C build_config_minimal.cmake \
    -DUSE_PREBUILT_LIBS=OFF \
    ...other flags...

make -C build_hp_infrun -j$(nproc)

# Copy prebuilt libraries
./copy_prebuilt_libs.sh
```

**Subsequent builds:**
```bash
cmake -B build_hp_infrun \
    -C build_config_minimal.cmake \
    -DUSE_PREBUILT_LIBS=ON \
    ...other flags...

make -C build_hp_infrun -j$(nproc)
```

## What Gets Disabled

The `build_config_minimal.cmake` disables:
- `GLCD_UI=OFF` - LCD/GLCD graphics interface
- `ALIF_CAMERA_ENABLED=OFF` - Camera drivers and image processing

What stays enabled:
- ✓ UART communication (needed for your inference runner)
- ✓ GPIO support (needed for timing and test routines)
- ✓ TensorFlow Lite Micro (needed for inference)
- ✓ CMSIS-NN and CMSIS-DSP (ML acceleration)

## Prebuilt Libraries

The following libraries are cached after the first build:
- `libtensorflow-lite-micro.a` (~10-15 MB, takes longest to compile)
- `libcmsisnn.a` / `libcmsis-nn.a`
- `libCMSISDSP.a` / `libcmsisdsp.a`

These libraries rarely change unless you:
- Update TensorFlow version
- Modify CMSIS library versions
- Change compiler flags significantly

## Installing ccache (Optional but Recommended)

Ccache caches compiled object files for even faster rebuilds.

**Ubuntu/Debian:**
```bash
sudo apt-get install ccache
```

**After installation:**
- Ccache is automatically detected and enabled
- First build populates the cache
- Subsequent builds reuse cached objects

## Build Time Comparison

Typical build times on a modern workstation:

| Build Type | Time | Description |
|------------|------|-------------|
| Full build (first time) | ~8-12 min | Compiles everything |
| Full build with ccache | ~5-7 min | Ccache speeds up TFLite |
| Incremental (prebuilt libs) | ~1-3 min | Only rebuilds your code |
| Incremental + ccache | ~30-90 sec | Fast iteration |

## Troubleshooting

### "Prebuilt libraries not found"
Run `./copy_prebuilt_libs.sh` after a successful full build.

### Build errors after using prebuilt libs
Try a clean full rebuild:
```bash
./build_minimal.sh --clean
```

### Want to include Camera/LCD back
Edit `build_config_minimal.cmake` and set:
```cmake
set(GLCD_UI ON CACHE BOOL "Enable GLCD UI" FORCE)
set(ALIF_CAMERA_ENABLED ON CACHE BOOL "Enable camera" FORCE)
```

Then rebuild with:
```bash
./build_minimal.sh --clean
```

## Files Created

- `build_config_minimal.cmake` - CMake configuration to disable unused features
- `UsePrebuiltLibs.cmake` - CMake module for prebuilt library support
- `copy_prebuilt_libs.sh` - Script to copy libraries after first build
- `build_minimal.sh` - Automated build script with all optimizations
- `CMakeLists.txt` - Modified to include ccache and prebuilt lib support

## Notes

- Prebuilt libraries are platform-specific (HP vs HE core)
- If you switch targets, run `--clean` to rebuild
- Prebuilt libs are safe to commit to git (add to `.gitignore` if you prefer)
- The `prebuilt_libs/` directory can be shared across your team
