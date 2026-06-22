# VTA Driver/Runtime Experiments

Codice C che usa direttamente le API runtime.h / driver.h di TVM 0.18 per
programmare il simulatore VTA (Versatile Tensor Accelerator).

## Setup

```bash
git clone --recurse-submodules <url-di-questo-repo>
cd vta-project
./build.sh
```

## Esecuzione

```bash
LD_LIBRARY_PATH=3rdparty/tvm/build ./build/gemm_test
```
