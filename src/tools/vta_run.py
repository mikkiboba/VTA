
#!/usr/bin/env python3
"""Execute VTA assembly using a JSON buffer manifest."""

import argparse
import ctypes as C
import json
import re
import struct
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]

MEM_TYPES = {
    "INP", "WGT", "WGT_DIAG",
    "ACC", "ACC_8BIT", "OUT"
}


def vta_sizes():
    """Read the same hardware configuration used by the simulator."""
    tool = (
        ROOT
        / "experimental/vta_diag/vta-hw/config/vta_config.py"
    )

    flags = subprocess.check_output(
        [sys.executable, str(tool), "--defs"],
        text=True
    ).split()

    defs = {}

    for flag in flags:
        match = re.fullmatch(
            r"-D(VTA_LOG_\w+)=(\d+)", flag
        )
        if match:
            defs[match.group(1)] = int(match.group(2))

    def power(name):
        return 1 << defs[f"VTA_LOG_{name}"]

    batch = power("BATCH")
    block_in = power("BLOCK_IN")
    block_out = power("BLOCK_OUT")

    sizes = {
        "INP": (
            power("INP_WIDTH") * batch * block_in // 8
        ),
        "WGT": (
            power("WGT_WIDTH") * block_in * block_out // 8
        ),
        "WGT_DIAG": (
            power("WGT_WIDTH") * block_out // 8
        ),
        "ACC": (
            power("ACC_WIDTH") * batch * block_out // 8
        ),
        "ACC_8BIT": (
            power("ACC_WIDTH") * batch * block_out // 32
        ),
        "OUT": (
            power("OUT_WIDTH") * batch * block_out // 8
        )
    }

    uop_depth = power("UOP_BUFF_SIZE") // 4

    return sizes, uop_depth


def declare(lib, name, result_type, *arguments):
    function = getattr(lib, name)
    function.restype = result_type
    function.argtypes = list(arguments)
    return function


def preview(data, dtype):
    formats = {
        "int8": "b",
        "uint8": "B",
        "int32": "i"
    }

    if dtype not in formats:
        raise ValueError(f"Unsupported dtype: {dtype}")

    fmt = "<" + formats[dtype]
    width = struct.calcsize(fmt)

    if len(data) % width:
        raise ValueError("Invalid output size for dtype")

    count = min(len(data) // width, 32)

    values = [
        struct.unpack_from(fmt, data, i * width)[0]
        for i in range(count)
    ]

    return values


def run(asm_path, config_path, dry_run=False):
    with config_path.open(encoding="utf-8") as file:
        config = json.load(file)

    buffers = config["buffers"]

    if not isinstance(buffers, dict) or not 1 <= len(buffers) <= 64:
        raise ValueError("Expected 1 to 64 named buffers")

    sizes, hw_uop_depth = vta_sizes()

    max_insn = int(config.get("max_instructions", 256))
    max_uop = int(config.get("max_uops", hw_uop_depth))

    if not 1 <= max_insn <= 65536:
        raise ValueError("Invalid instruction capacity")

    if not 1 <= max_uop <= hw_uop_depth:
        raise ValueError("Invalid UOP capacity")

    sim = C.CDLL(
        str(ROOT / "build/diag/libvta_fsim_diag.so"),
        mode=C.RTLD_GLOBAL
    )

    assembler = C.CDLL(
        str(ROOT / "build/libvta_asm.so")
    )

    mem_alloc = declare(
        sim, "VTAMemAlloc", C.c_void_p,
        C.c_size_t, C.c_int
    )

    mem_free = declare(
        sim, "VTAMemFree", None, C.c_void_p
    )

    phy_addr = declare(
        sim, "VTAMemGetPhyAddr", C.c_uint32,
        C.c_void_p
    )

    device_alloc = declare(
        sim, "VTADeviceAlloc", C.c_void_p
    )

    device_free = declare(
        sim, "VTADeviceFree", None, C.c_void_p
    )

    device_run = declare(
        sim, "VTADeviceRun", C.c_int,
        C.c_void_p, C.c_uint32,
        C.c_uint32, C.c_uint32
    )

    assemble = declare(
        assembler, "vtaAssemble", C.c_int,
        C.c_char_p,
        C.c_void_p, C.c_int, C.POINTER(C.c_int),
        C.c_void_p, C.c_int, C.POINTER(C.c_int)
    )

    print_error = declare(
        assembler, "vtaErrorPrint", None,
        C.c_int, C.c_int
    )

    owned = []
    device = None

    try:
        def allocate(size):
            pointer = mem_alloc(size, 0)

            if not pointer:
                raise MemoryError(
                    f"VTAMemAlloc({size}) failed"
                )

            owned.append(pointer)
            C.memset(pointer, 0, size)
            return pointer

        # These two buffers are managed automatically.
        uop_mem = allocate(max_uop * 4)
        insn_mem = allocate(max_insn * 16)

        symbols = {
            "UOP_ADDR": phy_addr(uop_mem) // 4
        }

        entries = {}

        # Allocate user-defined DRAM buffers.
        for name, spec in buffers.items():

            if not re.fullmatch(
                r"[A-Za-z_][A-Za-z_0-9]*", name
            ):
                raise ValueError(
                    f"Invalid symbol name: {name}"
                )

            if name in symbols:
                raise ValueError(
                    f"Reserved symbol: {name}"
                )

            memory = spec["memory"].upper()

            if memory not in MEM_TYPES:
                raise ValueError(
                    f"Unsupported memory type: {memory}"
                )

            input_file = spec.get("file")

            content = None

            if input_file:
                content = (
                    config_path.parent / input_file
                ).read_bytes()

            size = int(
                spec.get(
                    "size",
                    len(content) if content is not None else 0
                )
            )

            if size <= 0:
                raise ValueError(
                    f"Invalid buffer size: {name}"
                )

            if content is not None and len(content) != size:
                raise ValueError(
                    f"File size mismatch: {name}"
                )

            element_bytes = sizes[memory]

            if size % element_bytes:
                raise ValueError(
                    f"{name}: buffer size must be a multiple "
                    f"of {element_bytes} bytes"
                )

            pointer = allocate(size)

            if content is not None:
                C.memmove(pointer, content, size)

            address = phy_addr(pointer)

            if address % element_bytes:
                raise ValueError(
                    f"Unaligned DRAM address: {name}"
                )

            symbols[name] = address // element_bytes

            entries[name] = (
                pointer, size, spec
            )

            print(
                f"BUFFER {name}: {size} bytes, "
                f"{memory}, address={symbols[name]}"
            )

        # Load external assembly.
        source = asm_path.read_text(encoding="utf-8")

        def resolve(match):
            name = match.group(1)

            if name not in symbols:
                raise ValueError(
                    f"Unknown assembly symbol: @{name}@"
                )

            return str(symbols[name])

        source = re.sub(
            r"@([A-Za-z_][A-Za-z_0-9]*)@",
            resolve,
            source
        )

        # Assemble.
        num_insn = C.c_int()
        num_uop = C.c_int()

        status = assemble(
            source.encode("utf-8"),
            insn_mem,
            max_insn,
            C.byref(num_insn),
            uop_mem,
            max_uop,
            C.byref(num_uop)
        )

        if status != 0:
            print_error(status, -1)
            raise RuntimeError(
                f"vtaAssemble failed: {status}"
            )

        if num_insn.value == 0:
            raise ValueError("Empty assembly program")

        print(
            f"\nASSEMBLED: {num_insn.value} instructions, "
            f"{num_uop.value} UOPs"
        )

        if dry_run:
            print("DRY RUN COMPLETED")
            return

        # Current GCC/little-endian VTA instruction layout.
        last = C.string_at(
            insn_mem + (num_insn.value - 1) * 16,
            1
        )[0]

        if (last & 0x07) != 3:
            raise ValueError(
                "The last instruction must be FINISH"
            )

        # Execute.
        device = device_alloc()

        if not device:
            raise MemoryError("VTADeviceAlloc failed")

        print("\nEXECUTING...")

        status = device_run(
            device,
            phy_addr(insn_mem),
            num_insn.value,
            1000000
        )

        if status != 0:
            raise RuntimeError(
                f"VTADeviceRun returned {status}"
            )

        # Retrieve output buffers.
        for name, (pointer, size, spec) in entries.items():

            if not (
                spec.get("save") or spec.get("expect")
            ):
                continue

            data = C.string_at(pointer, size)

            print(
                f"OUTPUT {name}: "
                f"{preview(data, spec.get('dtype', 'int8'))}"
            )

            save = spec.get("save")

            if save:
                path = config_path.parent / save
                path.parent.mkdir(
                    parents=True, exist_ok=True
                )
                path.write_bytes(data)
                print(f"SAVED: {path}")

            expected = spec.get("expect")

            if expected:
                reference = (
                    config_path.parent / expected
                ).read_bytes()

                if data != reference:
                    raise ValueError(
                        f"FAIL: output {name} differs "
                        f"from {expected}"
                    )

                print(
                    f"PASS: {name} matches "
                    f"expected output ({size} bytes)"
                )

        for name in (
            "VTAGetDenseMACCounter",
            "VTAGetDiagMACCounter"
        ):
            counter = declare(
                sim, name, C.c_uint64
            )
            print(f"{name}: {counter()}")

        print("\nEXECUTION COMPLETED")

    finally:
        if device:
            device_free(device)

        for pointer in reversed(owned):
            mem_free(pointer)


def main():
    parser = argparse.ArgumentParser(
        description=__doc__
    )

    parser.add_argument("asm", type=Path)

    parser.add_argument(
        "--config",
        required=True,
        type=Path
    )

    parser.add_argument(
        "--dry-run",
        action="store_true"
    )

    args = parser.parse_args()

    try:
        run(
            args.asm.resolve(),
            args.config.resolve(),
            args.dry_run
        )

    except (
        OSError,
        ValueError,
        KeyError,
        RuntimeError,
        subprocess.CalledProcessError
    ) as exc:
        print(f"ERROR: {exc}", file=sys.stderr)
        sys.exit(1)


if __name__ == "__main__":
    main()
