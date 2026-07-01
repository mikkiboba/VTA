#!/bin/bash
set -e

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
TVM_DIR="$SCRIPT_DIR/3rdparty/tvm"
TVM_BUILD_DIR="$TVM_DIR/build"

# Step 1: build di TVM con supporto VTA simulator, solo se non già fatto
if [ ! -f "$TVM_BUILD_DIR/libvta_fsim.so" ]; then
    echo ">>> Building TVM (con VTA simulator)..."
    mkdir -p "$TVM_BUILD_DIR"
    cd "$TVM_BUILD_DIR"
    cp ../cmake/config.cmake .
    sed -i 's/set(USE_LLVM OFF)/set(USE_LLVM \/usr\/bin\/llvm-config-14)/' config.cmake
    sed -i 's/set(USE_VTA_FSIM OFF)/set(USE_VTA_FSIM ON)/' config.cmake
    cmake .. -G Ninja
    ninja -j1
    cd "$SCRIPT_DIR"
fi

# Step 2: compila il nostro codice
mkdir -p "$SCRIPT_DIR/build"

# 🌟 CORREZIONE: Estrae i flag hardware corretti dalla posizione reale di vta-hw
VTA_CFLAGS=$(python3 "$TVM_DIR/3rdparty/vta-hw/config/vta_config.py" --cflags)

gcc \
    $VTA_CFLAGS \
    -g -O0 \
    -I"$TVM_DIR" \
    -I"$TVM_DIR/include" \
    -I"$TVM_DIR/3rdparty/vta-hw/include" \
    -I"$TVM_DIR/3rdparty/dlpack/include" \
    "$SCRIPT_DIR/src/insnMaker.c" \
    "$SCRIPT_DIR/src/my_mat_mul.c" \
    -L"$TVM_BUILD_DIR" -lvta_fsim -ltvm \
    -o "$SCRIPT_DIR/build/gemm_test"

echo ">>> Build completata: build/gemm_test"
echo ">>> Esegui con:"
echo "    LD_LIBRARY_PATH=$TVM_BUILD_DIR ./build/gemm_test"