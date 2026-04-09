#!/bin/bash
# Script to copy prebuilt static libraries and headers after first successful build
# This allows faster incremental builds by reusing compiled dependencies

set -e

BUILD_DIR="build_hp_infrun"
PREBUILT_DIR="prebuilt_libs"
PREBUILT_INCLUDES_DIR="prebuilt_includes"

if [ ! -d "$BUILD_DIR" ]; then
    echo "Error: Build directory '$BUILD_DIR' not found!"
    echo "Please run a full build first before copying prebuilt libraries."
    exit 1
fi

echo "=== Copying Prebuilt Libraries ==="

# Create directories
mkdir -p "$PREBUILT_DIR"
mkdir -p "$PREBUILT_INCLUDES_DIR"

# Copy TensorFlow Lite Micro library (the heaviest dependency)
if [ -f "$BUILD_DIR/lib/libtensorflow-lite-micro.a" ]; then
    cp "$BUILD_DIR/lib/libtensorflow-lite-micro.a" "$PREBUILT_DIR/"
    echo "✓ Copied libtensorflow-lite-micro.a"
else
    echo "✗ Warning: libtensorflow-lite-micro.a not found"
fi

# Copy CMSIS-NN library
if [ -f "$BUILD_DIR/lib/libcmsisnn.a" ]; then
    cp "$BUILD_DIR/lib/libcmsisnn.a" "$PREBUILT_DIR/"
    echo "✓ Copied libcmsisnn.a"
elif [ -f "$BUILD_DIR/lib/libcmsis-nn.a" ]; then
    cp "$BUILD_DIR/lib/libcmsis-nn.a" "$PREBUILT_DIR/"
    echo "✓ Copied libcmsis-nn.a"
fi

# Copy CMSIS-DSP library
if [ -f "$BUILD_DIR/lib/libCMSISDSP.a" ]; then
    cp "$BUILD_DIR/lib/libCMSISDSP.a" "$PREBUILT_DIR/"
    echo "✓ Copied libCMSISDSP.a"
elif [ -f "$BUILD_DIR/lib/libcmsisdsp.a" ]; then
    cp "$BUILD_DIR/lib/libcmsisdsp.a" "$PREBUILT_DIR/"
    echo "✓ Copied libcmsisdsp.a"
fi

# Copy TensorFlow headers (needed for linking)
if [ -d "$BUILD_DIR/_deps/tensorflow-src" ]; then
    mkdir -p "$PREBUILT_INCLUDES_DIR/tensorflow"
    cp -r "$BUILD_DIR/_deps/tensorflow-src/tensorflow" "$PREBUILT_INCLUDES_DIR/" 2>/dev/null || true
    echo "✓ Copied TensorFlow headers"
fi

# Copy flatbuffers headers
if [ -d "$BUILD_DIR/_deps/flatbuffers-src/include" ]; then
    mkdir -p "$PREBUILT_INCLUDES_DIR/flatbuffers"
    cp -r "$BUILD_DIR/_deps/flatbuffers-src/include/"* "$PREBUILT_INCLUDES_DIR/flatbuffers/" 2>/dev/null || true
    echo "✓ Copied Flatbuffers headers"
fi

# Copy gemmlowp headers
if [ -d "$BUILD_DIR/_deps/gemmlowp-src" ]; then
    mkdir -p "$PREBUILT_INCLUDES_DIR/gemmlowp"
    cp -r "$BUILD_DIR/_deps/gemmlowp-src/"* "$PREBUILT_INCLUDES_DIR/gemmlowp/" 2>/dev/null || true
    echo "✓ Copied gemmlowp headers"
fi

# Copy ruy headers
if [ -d "$BUILD_DIR/_deps/ruy-src" ]; then
    mkdir -p "$PREBUILT_INCLUDES_DIR/ruy"
    cp -r "$BUILD_DIR/_deps/ruy-src/"* "$PREBUILT_INCLUDES_DIR/ruy/" 2>/dev/null || true
    echo "✓ Copied ruy headers"
fi

echo ""
echo "=== Prebuilt Libraries Summary ==="
echo "Libraries copied to: $PREBUILT_DIR"
echo "Headers copied to: $PREBUILT_INCLUDES_DIR"
echo ""
ls -lh "$PREBUILT_DIR"
echo ""
echo "To use prebuilt libraries in future builds, add:"
echo "  -DUSE_PREBUILT_LIBS=ON"
echo "to your cmake configuration."
