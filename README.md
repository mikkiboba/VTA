<div align="center">

# VTA Project

### VTA assembler / disassembler

[![Build](...)](...)
[![License](...)](...)
[![Version](...)](...)

A C library for assembling and disassembling instructions for the Versatile Tensor Accelerator (VTA).
The project is built upon the VTA hardware specification distributed 

C-based project which uses the API of TVM v0.18.0 to program the VTA simulator.

</div>

## Setup

Change the line 35 of `build.sh` to add the name of the file to run.

```bash
"$SCRIPT_DIR/src/<file.c>" \
```

```bash
git clone --recurse-submodules <url-di-questo-repo>
cd vta-project
./build.sh
```

## Execution

```bash
LD_LIBRARY_PATH=3rdparty/tvm/build ./build/gemm_test
```
