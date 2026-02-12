#!/bin/bash
set -e  # Exit on any error

PROJECT_DIR=/home/eta_lab/Project/alif_ml-embedded-evaluation-kit
BUILD_DIR=/home/eta_lab/Project/alif_ml-embedded-evaluation-kit/cmake-build-release-arm-gcc
BINARY_PATH=$BUILD_DIR/bin/sectors/alif_object_detection/mram.bin
SETOOLS_ROOT=/home/eta_lab/Project/app-release-exec-linux
NEW_BINARY_PATH=$SETOOLS_ROOT/build/images/mram.bin
FLASH_CONFIG_PATH=$SETOOLS_ROOT/build/config/alif_ew_demo.json

echo "=== Building project ==="
cmake -Wno-dev \
    -S $PROJECT_DIR \
    -B $BUILD_DIR \
    -DUSE_CASE_BUILD=alif_object_detection \
    -DTARGET_PLATFORM=alif \
    -DTARGET_SUBSYSTEM=RTSS-HP \
    -DTARGET_BOARD=AppKit-e7 \
    -DLINKER_SCRIPT_NAME=RTSS-HP-merged-SRAM \
    -DCMAKE_TOOLCHAIN_FILE=scripts/cmake/toolchains/bare-metal-gcc.cmake \
    -DCMAKE_BUILD_TYPE=Release \
    -DLOG_LEVEL=LOG_LEVEL_DEBUG \
    -DGLCD_UI=NO \
    -DTENSORFLOW_LITE_MICRO_CLEAN_BUILD=OFF

echo "=== Compiling ==="
cd $BUILD_DIR
make -j$(nproc)

echo "=== Copying binary ==="
if [ ! -f "$BINARY_PATH" ]; then
    echo "Error: Binary not found at $BINARY_PATH"
    exit 1
fi
cp $BINARY_PATH $NEW_BINARY_PATH

echo "=== Generating TOC ==="
cd $SETOOLS_ROOT
./app-gen-toc -f $FLASH_CONFIG_PATH

echo "=== Flashing to device ==="
./app-write-mram -p

echo "=== Done! ==="