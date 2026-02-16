#!/bin/bash
set -e  # Exit on any error

USE_CASE_NAME=alif_object_detection

PROJECT_DIR=/home/eta_lab/Project/alif_ml-embedded-evaluation-kit
BUILD_DIR=$PROJECT_DIR/build_hp
BINARY_PATH=$BUILD_DIR/bin/sectors/$USE_CASE_NAME/mram.bin

SETOOLS_ROOT=/home/eta_lab/Project/app-release-exec-linux
NEW_BINARY_PATH=$SETOOLS_ROOT/build/images/mram.bin
FLASH_CONFIG_PATH=$SETOOLS_ROOT/build/config/alif_ew_demo.json

cd $BUILD_DIR

echo "=== Building project ==="
cmake -DTARGET_PLATFORM=alif \
 -DTARGET_SUBSYSTEM=RTSS-HP \
 -DTARGET_BOARD=AppKit-e7 \
 -DUSE_CASE_BUILD=$USE_CASE_NAME \
 -DCMAKE_TOOLCHAIN_FILE=scripts/cmake/toolchains/bare-metal-gcc.cmake \
 -DCONSOLE_UART=2 \
 -DCMAKE_BUILD_TYPE=Release \
 -DROTATE_DISPLAY=180 \
 -Dalif_object_detection_MODEL_TYPE=SSD \
 -DLOG_LEVEL=LOG_LEVEL_DEBUG ..

echo "=== Compiling ==="
cd $BUILD_DIR
make ethos-u-$USE_CASE_NAME -j4

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