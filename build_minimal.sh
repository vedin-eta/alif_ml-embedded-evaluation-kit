#!/bin/bash
# Optimized build script for inference runner with minimal dependencies
# First build: Full build with camera/LCD disabled
# Subsequent builds: Use prebuilt libraries for faster compilation

set -e

# Configuration
TARGET="hp"
USE_CASE="inference_runner"
BUILD_DIR="build_hp_infrun"
PREBUILT_DIR="prebuilt_libs"

# Parse command line arguments
CLEAN_BUILD=false
FORCE_FULL_BUILD=false

while [[ $# -gt 0 ]]; do
    case $1 in
        --clean)
            CLEAN_BUILD=true
            shift
            ;;
        --full)
            FORCE_FULL_BUILD=true
            shift
            ;;
        --help)
            echo "Usage: $0 [OPTIONS]"
            echo ""
            echo "Options:"
            echo "  --clean    Remove build directory and prebuilt libraries before building"
            echo "  --full     Force full build (don't use prebuilt libraries)"
            echo "  --help     Show this help message"
            echo ""
            echo "First run will do a full build. Subsequent runs use prebuilt libraries."
            exit 0
            ;;
        *)
            echo "Unknown option: $1"
            echo "Use --help for usage information"
            exit 1
            ;;
    esac
done

# Clean if requested
if [ "$CLEAN_BUILD" = true ]; then
    echo "=== Cleaning build and prebuilt libraries ==="
    rm -rf "$BUILD_DIR" "$PREBUILT_DIR" prebuilt_includes
    echo "Clean complete"
    echo ""
fi

# Check if this is first build or forced full build
USE_PREBUILT="OFF"
if [ -d "$PREBUILT_DIR" ] && [ "$FORCE_FULL_BUILD" = false ]; then
    echo "=== Prebuilt libraries found - using incremental build ==="
    USE_PREBUILT="ON"
else
    echo "=== First build or forced full build - building all dependencies ==="
    USE_PREBUILT="OFF"
fi

# Find the build script
BUILD_SCRIPT="./build_inference_runner.sh"
if [ ! -f "$BUILD_SCRIPT" ]; then
    BUILD_SCRIPT="./scripts/build_inference_runner.sh"
fi

if [ ! -f "$BUILD_SCRIPT" ]; then
    echo "Error: Could not find build_inference_runner.sh"
    exit 1
fi

echo ""
echo "=== Building inference runner (target: $TARGET) ==="
echo "Configuration:"
echo "  - Camera: DISABLED"
echo "  - LCD/GLCD: DISABLED"
echo "  - UART: ENABLED"
echo "  - GPIO: ENABLED"
echo "  - TFLite: ENABLED"
echo "  - Prebuilt libs: $USE_PREBUILT"
echo ""

# Run the build with minimal configuration
$BUILD_SCRIPT \
    --target $TARGET \
    --use-case $USE_CASE \
    --optimize size \
    -C build_config_minimal.cmake \
    -DUSE_PREBUILT_LIBS=$USE_PREBUILT

BUILD_RESULT=$?

if [ $BUILD_RESULT -eq 0 ]; then
    echo ""
    echo "=== Build Successful ==="

    # If this was a full build and prebuilt libraries don't exist, offer to create them
    if [ "$USE_PREBUILT" = "OFF" ] && [ ! -d "$PREBUILT_DIR" ]; then
        echo ""
        echo "This was a full build. To speed up future builds, you can copy the"
        echo "compiled libraries by running:"
        echo "  ./copy_prebuilt_libs.sh"
        echo ""
        read -p "Copy prebuilt libraries now? (y/n) " -n 1 -r
        echo
        if [[ $REPLY =~ ^[Yy]$ ]]; then
            ./copy_prebuilt_libs.sh
            echo ""
            echo "Prebuilt libraries ready. Next build will be much faster!"
        fi
    fi

    echo ""
    echo "Output binary:"
    ls -lh "$BUILD_DIR/bin/ethos-u-$USE_CASE.elf" 2>/dev/null || ls -lh "$BUILD_DIR/bin/"*"$USE_CASE"* 2>/dev/null || echo "Binary not found in expected location"
else
    echo ""
    echo "=== Build Failed ==="
    exit $BUILD_RESULT
fi
