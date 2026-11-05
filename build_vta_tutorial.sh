#!/bin/bash
set -e

ROOT_DIR="$(cd "$(dirname "$0")" && pwd)"
BUILD_DIR="$ROOT_DIR/build"
DIAG_BUILD_DIR="$BUILD_DIR/diag"

usage() {
    echo "Usage:"
    echo "  $0 <source.c>"
    echo "  $0 <source.c> --run"
    echo
    echo "Examples:"
    echo "  $0 src/tutorials/depthwise_separable_baseline.c"
    echo "  $0 src/tutorials/depthwise_separable_diag_sram.c"
    echo "  $0 src/tutorials/depthwise_separable_fused_onchip.c --run"
    echo
    echo "You can also pass only the filename if it is inside src/tutorials/:"
    echo "  $0 depthwise_separable_offline_fused.c"
}

if [ $# -lt 1 ] || [ $# -gt 2 ]; then
    usage
    exit 1
fi

SOURCE="$1"
RUN_AFTER_BUILD=0

if [ $# -eq 2 ]; then
    if [ "$2" = "--run" ]; then
        RUN_AFTER_BUILD=1
    else
        echo "ERROR: unknown option '$2'"
        usage
        exit 1
    fi
fi

# If only a filename was provided, try src/tutorials/.
if [ ! -f "$SOURCE" ]; then
    if [ -f "$ROOT_DIR/src/tutorials/$SOURCE" ]; then
        SOURCE="$ROOT_DIR/src/tutorials/$SOURCE"
    else
        echo "ERROR: source file not found: $1"
        exit 1
    fi
fi

# Convert relative path to absolute path.
SOURCE="$(cd "$(dirname "$SOURCE")" && pwd)/$(basename "$SOURCE")"

if [[ "$SOURCE" != *.c ]]; then
    echo "ERROR: expected a .c source file."
    exit 1
fi

NAME="$(basename "$SOURCE" .c)"
OUTPUT="$BUILD_DIR/$NAME"

mkdir -p "$BUILD_DIR"

echo "============================================================"
echo " VTA tutorial build"
echo "============================================================"
echo "Source : $SOURCE"
echo "Output : $OUTPUT"
echo

echo ">>> Building experimental VTA simulator..."
"$ROOT_DIR/experimental/vta_diag/build_diag.sh"

echo
echo ">>> Building tutorial..."

gcc \
    -I"$ROOT_DIR/experimental/vta_diag/runtime" \
    -I"$ROOT_DIR/experimental/vta_diag/vta-hw/include" \
    -I"$ROOT_DIR/3rdparty/tvm/vta/include" \
    -I"$ROOT_DIR/3rdparty/tvm" \
    -I"$ROOT_DIR/3rdparty/tvm/include" \
    -I"$ROOT_DIR/3rdparty/tvm/3rdparty/dlpack/include" \
    "$SOURCE" \
    -L"$DIAG_BUILD_DIR" \
    -lvta_fsim_diag \
    -L"$ROOT_DIR/3rdparty/tvm/build" \
    -ltvm \
    -lstdc++ \
    -o "$OUTPUT"

echo
echo ">>> Build completed successfully."
echo
echo "Run with:"
echo
echo "LD_LIBRARY_PATH=./build/diag:./3rdparty/tvm/build \\"
echo "    ./build/$NAME"
echo

if [ "$RUN_AFTER_BUILD" -eq 1 ]; then
    echo "============================================================"
    echo " Running $NAME"
    echo "============================================================"
    echo

    LD_LIBRARY_PATH="$DIAG_BUILD_DIR:$ROOT_DIR/3rdparty/tvm/build${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}" \
        "$OUTPUT"
fi
