#!/usr/bin/env bash

set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
BUILD="$ROOT/build"

usage() {
    echo "Usage: $0 <file.asm> [config.json] [--dry-run]"
    exit 1
}

[[ $# -ge 1 && $# -le 3 ]] || usage

ASM="$1"
shift

CONFIG_ARG=""
MODE=""

for arg in "$@"; do
    case "$arg" in
        --dry-run)
            [[ -z "$MODE" ]] || usage
            MODE="--dry-run"
            ;;
        *.json)
            [[ -z "$CONFIG_ARG" ]] || usage
            CONFIG_ARG="$arg"
            ;;
        *)
            usage
            ;;
    esac
done

# Find the assembly file.
if [[ ! -f "$ASM" ]]; then
    ASM="$ROOT/src/assembly/$ASM"
fi

if [[ ! -f "$ASM" ]]; then
    echo "ERROR: Assembly file not found: $ASM"
    exit 1
fi

ASM="$(realpath "$ASM")"

if [[ "$ASM" != *.asm ]]; then
    echo "ERROR: Expected a .asm file"
    exit 1
fi

NAME="$(basename "$ASM" .asm)"

if [[ -n "$CONFIG_ARG" ]]; then
    CONFIG="$CONFIG_ARG"
else
    CONFIG="$ROOT/experiments/$NAME/config.json"
fi

if [[ ! -f "$CONFIG" ]]; then
    echo "ERROR: Configuration not found: $CONFIG"
    exit 1
fi

CONFIG="$(realpath "$CONFIG")"

RUNNER="$ROOT/src/tools/vta_run.py"

if [[ ! -f "$RUNNER" ]]; then
    echo "ERROR: Missing Python runner:"
    echo "  $RUNNER"
    exit 1
fi

mkdir -p "$BUILD"

echo "========================================"
echo " VTA Assembly Runner"
echo "========================================"
echo "Program: $ASM"
echo "Config:  $CONFIG"
echo

# Build the experimental simulator if missing or outdated.
SIM_LIB="$BUILD/diag/libvta_fsim_diag.so"

if [[ ! -f "$SIM_LIB" ]] || \
   [[ -n "$(find \
      "$ROOT/experimental/vta_diag/vta-hw/src" \
      "$ROOT/experimental/vta_diag/vta-hw/include" \
      -type f -newer "$SIM_LIB" -print -quit)" ]]; then

    echo ">>> Building experimental simulator..."

    "$ROOT/experimental/vta_diag/build_diag.sh"
fi

# Build shared assembler library if missing or outdated.
ASM_LIB="$BUILD/libvta_asm.so"

if [[ ! -f "$ASM_LIB" || \
      "$ROOT/src/assembler.c" -nt "$ASM_LIB" || \
      "$ROOT/src/disassembler.c" -nt "$ASM_LIB" || \
      "$ROOT/src/vtaError.c" -nt "$ASM_LIB" || \
      "$ROOT/include/vta.h" -nt "$ASM_LIB" ]]; then

    echo ">>> Building assembler library..."

    VTA_DEFS=(
        $(python3 \
            "$ROOT/experimental/vta_diag/vta-hw/config/vta_config.py" \
            --defs)
    )

    gcc -shared -fPIC -g -O0 \
        "${VTA_DEFS[@]}" \
        -I"$ROOT/include" \
        -I"$ROOT/experimental/vta_diag/vta-hw/include" \
        -I"$ROOT/3rdparty/tvm" \
        -I"$ROOT/3rdparty/tvm/include" \
        -I"$ROOT/3rdparty/tvm/3rdparty/dlpack/include" \
        "$ROOT/src/assembler.c" \
        "$ROOT/src/disassembler.c" \
        "$ROOT/src/vtaError.c" \
        -o "$ASM_LIB"
fi

echo ">>> Running assembly..."
echo

export LD_LIBRARY_PATH="$BUILD/diag:$ROOT/3rdparty/tvm/build${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}"

if [[ "$MODE" == "--dry-run" ]]; then
    python3 "$RUNNER" "$ASM" \
        --config "$CONFIG" \
        --dry-run
else
    python3 "$RUNNER" "$ASM" \
        --config "$CONFIG"
fi

echo
echo ">>> Done."
