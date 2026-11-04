#!/bin/bash
set -e

ROOT_DIR="$(cd "$(dirname "$0")/../.." && pwd)"

TVM_DIR="$ROOT_DIR/3rdparty/tvm"
DIAG_DIR="$ROOT_DIR/experimental/vta_diag"
VTA_HW_DIR="$DIAG_DIR/vta-hw"
BUILD_DIR="$ROOT_DIR/build/diag"

mkdir -p "$BUILD_DIR"

VTA_DEFS=(
    $(python3 "$VTA_HW_DIR/config/vta_config.py" --defs)
)

echo ">>> Building experimental VTA functional simulator..."

g++ \
    -std=c++17 \
    -shared \
    -fPIC \
    -g \
    -O0 \
    "${VTA_DEFS[@]}" \
    '-DDMLC_USE_LOGGING_LIBRARY=<tvm/runtime/logging.h>' \
    -DUSE_FSIM_TLPP \
    -I"$VTA_HW_DIR/include" \
    -I"$TVM_DIR" \
    -I"$TVM_DIR/include" \
    -I"$TVM_DIR/3rdparty/dmlc-core/include" \
    -I"$TVM_DIR/3rdparty/dlpack/include" \
    -I"$DIAG_DIR/runtime" \
    "$DIAG_DIR/runtime/runtime.cc" \
    "$VTA_HW_DIR/src/sim/sim_driver.cc" \
    "$VTA_HW_DIR/src/sim/sim_tlpp.cc" \
    "$VTA_HW_DIR/src/vmem/virtual_memory.cc" \
    -L"$TVM_DIR/build" \
    -ltvm \
    -o "$BUILD_DIR/libvta_fsim_diag.so"

echo ">>> Experimental simulator built:"
echo "    $BUILD_DIR/libvta_fsim_diag.so"

echo ">>> Run with:"
echo "LD_LIBRARY_PATH=./build/diag:./3rdparty/tvm/build \
    ./build/depthwise_separable_diag"
