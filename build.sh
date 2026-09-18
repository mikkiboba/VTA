#!/bin/bash
set -e

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"

TVM_DIR="$SCRIPT_DIR/3rdparty/tvm"
TVM_BUILD_DIR="$TVM_DIR/build"
BUILD_DIR="$SCRIPT_DIR/build"


if [ ! -f "$TVM_BUILD_DIR/libvta_fsim.so" ]; then
    echo ">>> Building TVM (con VTA simulator)..."

    mkdir -p "$TVM_BUILD_DIR"

    cd "$TVM_BUILD_DIR"

    cp ../cmake/config.cmake .

    sed -i \
        's/set(USE_LLVM OFF)/set(USE_LLVM \/usr\/bin\/llvm-config-14)/' \
        config.cmake

    sed -i \
        's/set(USE_VTA_FSIM OFF)/set(USE_VTA_FSIM ON)/' \
        config.cmake

    cmake .. -G Ninja
    ninja -j1

    cd "$SCRIPT_DIR"
fi


mkdir -p "$BUILD_DIR"

# * Remove objects/library/executable from previous builds.
rm -f \
    "$BUILD_DIR/vtaError.o" \
    "$BUILD_DIR/assembler.o" \
    "$BUILD_DIR/disassembler.o" \
    "$BUILD_DIR/test_runner.o" \
    "$BUILD_DIR/libvta.a" \
    "$BUILD_DIR/vtaBuild"


VTA_CFLAGS=$(
    python3 \
    "$TVM_DIR/3rdparty/vta-hw/config/vta_config.py" \
    --cflags
)

COMMON_CFLAGS=(
    $VTA_CFLAGS
    -g
    -O0
    -I"$TVM_DIR"
    -I"$TVM_DIR/include"
    -I"$TVM_DIR/3rdparty/vta-hw/include"
    -I"$TVM_DIR/3rdparty/dlpack/include"
    -I"$SCRIPT_DIR/include"
)


echo ">>> Compiling VTA library sources..."

gcc "${COMMON_CFLAGS[@]}" \
    -c "$SCRIPT_DIR/src/vtaError.c" \
    -o "$BUILD_DIR/vtaError.o"

gcc "${COMMON_CFLAGS[@]}" \
    -c "$SCRIPT_DIR/src/assembler.c" \
    -o "$BUILD_DIR/assembler.o"

gcc "${COMMON_CFLAGS[@]}" \
    -c "$SCRIPT_DIR/src/disassembler.c" \
    -o "$BUILD_DIR/disassembler.o"


echo ">>> Creating static library build/libvta.a..."

ar rcs \
    "$BUILD_DIR/libvta.a" \
    "$BUILD_DIR/vtaError.o" \
    "$BUILD_DIR/assembler.o" \
    "$BUILD_DIR/disassembler.o"


echo ">>> Compiling test runner..."

gcc "${COMMON_CFLAGS[@]}" \
    -c "$SCRIPT_DIR/tests/test_runner.c" \
    -o "$BUILD_DIR/test_runner.o"


echo ">>> Linking test runner with libvta.a..."

gcc \
    "$BUILD_DIR/test_runner.o" \
    -L"$BUILD_DIR" \
    -lvta \
    -L"$TVM_BUILD_DIR" \
    -lvta_fsim \
    -ltvm \
    -o "$BUILD_DIR/vtaBuild"


echo ">>> Build completed."
echo ">>> Static library: build/libvta.a"
echo ">>> Test executable: build/vtaBuild"
echo ""
echo ">>> Run with:"
echo "    LD_LIBRARY_PATH=$TVM_BUILD_DIR ./build/vtaBuild"
echo ""
echo ">>> For debugging:"
echo "    LD_LIBRARY_PATH=$TVM_BUILD_DIR gdb ./build/vtaBuild"