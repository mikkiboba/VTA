/*
 * depthwise_separable_offline_fused.c
 *
 * Mathematical/offline fusion of:
 *
 *   depthwise 3x3 -> pointwise 1x1
 *
 * into 9 dense matrices:
 *
 *   M_r = P * D_r
 *
 * where D_r is the diagonal depthwise matrix for tap r and P is the
 * pointwise matrix. VTA then executes only standard dense GEMMs:
 *
 *   y = sum_r M_r * x_r
 *
 * No GEMM_DIAG, ACC_TO_INP or intermediate depthwise tensor is required.
 *
 * IMPORTANT: exact algebraic fusion assumes no nonlinear operation between
 * depthwise and pointwise (e.g. ReLU, clamp or intermediate requantization).
 * In this synthetic test all fused weights remain in int8. In a general
 * quantized network, offline requantization of the fused kernel may be needed.
 */

#include <stdio.h>
#include <stdint.h>
#include <string.h>

#include "vta/runtime/runtime.h"
#include "vta/hw_spec_const.h"

#define BLOCK 16
#define BATCH 1

#define INPUT_H 4
#define INPUT_W 4

#define KERNEL 3
#define KERNEL_TAPS (KERNEL * KERNEL)

#define STRIDE_H 1
#define STRIDE_W 1

#define PAD_TOP 0
#define PAD_BOTTOM 0
#define PAD_LEFT 0
#define PAD_RIGHT 0

#define OUTPUT_H (((INPUT_H + PAD_TOP + PAD_BOTTOM - KERNEL) / STRIDE_H) + 1)
#define OUTPUT_W (((INPUT_W + PAD_LEFT + PAD_RIGHT - KERNEL) / STRIDE_W) + 1)
#define OUTPUT_PIXELS (OUTPUT_H * OUTPUT_W)

#define LOAD 1
#define COMPUTE 2
#define STORE 3

static void *RESET_cache_handle = NULL;
static void *FUSED_cache_handle = NULL;

static void printArray(int8_t array[], int rows, int cols)
{
    for (int r = 0; r < rows; r++) {
        for (int c = 0; c < cols; c++)
            printf("%2d ", array[r * cols + c]);
        printf("\n");
    }
}

static int countValidTaps(int outPixel)
{
    int oy = outPixel / OUTPUT_W;
    int ox = outPixel % OUTPUT_W;
    int validTaps = 0;

    for (int ky = 0; ky < KERNEL; ky++)
        for (int kx = 0; kx < KERNEL; kx++) {
            int inY = oy * STRIDE_H + ky - PAD_TOP;
            int inX = ox * STRIDE_W + kx - PAD_LEFT;

            if (inY >= 0 && inY < INPUT_H &&
                inX >= 0 && inX < INPUT_W)
                validTaps++;
        }

    return validTaps;
}

static int reset_init(void *signature)
{
    VTAUopLoopBegin(OUTPUT_PIXELS, 1, 0, 0);
        VTAUopPush(0, 1, 0, 0, 0, 0, 0, 0);
    VTAUopLoopEnd();

    return 0;
}

/*
 * For each output pixel, scan the 9 spatial taps.
 * dst += 1 per output pixel
 * src += 9 per output pixel, then +1 per tap
 * wgt += 1 per tap
 * dst stays fixed inside the inner loop so all 9 GEMMs accumulate together.
 */
static int fused_init(void *signature)
{
    VTAUopLoopBegin(OUTPUT_PIXELS, 1, KERNEL_TAPS, 0);
        VTAUopLoopBegin(KERNEL_TAPS, 0, 1, 1);
            VTAUopPush(0, 0, 0, 0, 0, 0, 0, 0);
        VTAUopLoopEnd();
    VTAUopLoopEnd();

    return 0;
}

static void printStats(void)
{
    int depthwiseWeightBytes =
        KERNEL_TAPS * BLOCK * sizeof(int8_t);

    int pointwiseWeightBytes =
        BLOCK * BLOCK * sizeof(int8_t);

    int separableWeightBytes =
        depthwiseWeightBytes + pointwiseWeightBytes;

    int fusedWeightBytes =
        KERNEL_TAPS * BLOCK * BLOCK * sizeof(int8_t);

    int depthwiseMACs =
        OUTPUT_PIXELS * KERNEL_TAPS * BLOCK;

    int pointwiseMACs =
        OUTPUT_PIXELS * BLOCK * BLOCK;

    int separableMACs =
        depthwiseMACs + pointwiseMACs;

    int fusedMACs =
        OUTPUT_PIXELS * KERNEL_TAPS * BLOCK * BLOCK;

    int originalVTABaselineMACs =
        fusedMACs + pointwiseMACs;

    int intermediateBytes =
        OUTPUT_PIXELS * BATCH * BLOCK * sizeof(int8_t);

    printf("\nOffline mathematical fusion statistics:\n");

    printf("\nWeights:\n");
    printf("Depthwise weights:              %d bytes\n", depthwiseWeightBytes);
    printf("Pointwise weights:              %d bytes\n", pointwiseWeightBytes);
    printf("Separable total weights:        %d bytes\n", separableWeightBytes);
    printf("Offline fused dense weights:    %d bytes\n", fusedWeightBytes);
    printf("Fused/separable weight ratio:   %.2fx\n",
           (double)fusedWeightBytes / separableWeightBytes);

    printf("\nComputation:\n");
    printf("Efficient depthwise MACs:       %d\n", depthwiseMACs);
    printf("Pointwise MACs:                 %d\n", pointwiseMACs);
    printf("Efficient separable MACs:       %d\n", separableMACs);
    printf("Offline fused dense MACs:       %d\n", fusedMACs);
    printf("Fused/separable MAC ratio:      %.2fx\n",
           (double)fusedMACs / separableMACs);

    printf("\nComparison with original VTA dense baseline:\n");
    printf("Original VTA two-stage MACs:    %d\n", originalVTABaselineMACs);
    printf("Offline fused MACs:             %d\n", fusedMACs);
    printf("MAC reduction vs VTA baseline:  %.2f%%\n",
           100.0 *
           (double)(originalVTABaselineMACs - fusedMACs) /
           originalVTABaselineMACs);

    printf("\nDepthwise -> pointwise intermediate:\n");
    printf("Logical intermediate size:      %d bytes\n", intermediateBytes);
    printf("Baseline DRAM traffic:          %d bytes\n", intermediateBytes * 2);
    printf("Offline fused DRAM traffic:     0 bytes\n");
    printf("DRAM traffic reduction:         100.00%%\n");
}

int main(void)
{
    void *inputBuffer;
    inputBuffer = VTABufferAlloc(
        OUTPUT_PIXELS * KERNEL_TAPS * BATCH * BLOCK * sizeof(int8_t));

    void *fusedWeightBuffer;
    fusedWeightBuffer = VTABufferAlloc(
        KERNEL_TAPS * BLOCK * BLOCK * sizeof(int8_t));

    void *finalOutputBuffer;
    finalOutputBuffer = VTABufferAlloc(
        OUTPUT_PIXELS * BATCH * BLOCK * sizeof(int8_t));

    /* Input image: 1..16 */
    int8_t image[INPUT_H * INPUT_W];

    for (int y = 0; y < INPUT_H; y++)
        for (int x = 0; x < INPUT_W; x++)
            image[y * INPUT_W + x] = y * INPUT_W + x + 1;

    printf("Input image:\n");
    printArray(image, INPUT_H, INPUT_W);

    /* Same im2col-style packing used by the other tutorials. */
    int8_t input[OUTPUT_PIXELS * KERNEL_TAPS * BATCH * BLOCK];

    for (int oy = 0; oy < OUTPUT_H; oy++)
        for (int ox = 0; ox < OUTPUT_W; ox++) {
            int outPixel = oy * OUTPUT_W + ox;

            for (int ky = 0; ky < KERNEL; ky++)
                for (int kx = 0; kx < KERNEL; kx++) {
                    int tap = ky * KERNEL + kx;
                    int inY = oy * STRIDE_H + ky - PAD_TOP;
                    int inX = ox * STRIDE_W + kx - PAD_LEFT;
                    int value = 0;

                    if (inY >= 0 && inY < INPUT_H &&
                        inX >= 0 && inX < INPUT_W)
                        value = image[inY * INPUT_W + inX];

                    for (int c = 0; c < BLOCK; c++) {
                        int index =
                            outPixel * KERNEL_TAPS * BLOCK +
                            tap * BLOCK + c;

                        if (inY < 0 || inY >= INPUT_H ||
                            inX < 0 || inX >= INPUT_W)
                            input[index] = 0;
                        else
                            input[index] = value + (c % 2);
                    }
                }
        }

    VTABufferCopy(input, 0, inputBuffer, 0,
                  sizeof(input), VTA_MEMCPY_H2D);

    printf("First input tile:\n");
    printArray(input, BATCH, BLOCK);

    /* Compact depthwise weights D_r[c], used only offline on the CPU. */
    int8_t depthwiseWeight[KERNEL_TAPS * BLOCK];

    for (int tap = 0; tap < KERNEL_TAPS; tap++)
        for (int c = 0; c < BLOCK; c++)
            depthwiseWeight[tap * BLOCK + c] = 1;

    /* Pointwise matrix P[o,c]. */
    int8_t pointwiseWeight[BLOCK * BLOCK];
    memset(pointwiseWeight, 0, sizeof(pointwiseWeight));

    for (int c = 0; c < BLOCK; c += 2) {
        pointwiseWeight[c * BLOCK + c] = 1;
        pointwiseWeight[c * BLOCK + c + 1] = -1;
        pointwiseWeight[(c + 1) * BLOCK + c] = -1;
        pointwiseWeight[(c + 1) * BLOCK + c + 1] = 1;
    }

    printf("Pointwise matrix P:\n");
    printArray(pointwiseWeight, BLOCK, BLOCK);

    /*
     * Offline fusion:
     *
     *   M_r = P * D_r
     *
     * Since D_r is diagonal:
     *
     *   M_r[o,c] = P[o,c] * D_r[c]
     */
    int8_t fusedWeight[KERNEL_TAPS * BLOCK * BLOCK];
    int overflow = 0;

    for (int tap = 0; tap < KERNEL_TAPS; tap++)
        for (int out = 0; out < BLOCK; out++)
            for (int in = 0; in < BLOCK; in++) {
                int product =
                    (int)pointwiseWeight[out * BLOCK + in] *
                    (int)depthwiseWeight[tap * BLOCK + in];

                if (product < -128 || product > 127)
                    overflow++;

                fusedWeight[
                    tap * BLOCK * BLOCK +
                    out * BLOCK +
                    in
                ] = (int8_t)product;
            }

    if (overflow != 0) {
        printf("ERROR: %d fused weights do not fit in int8.\n", overflow);
        printf("A real quantized model needs offline requantization of the fused kernel.\n");

        VTABufferFree(inputBuffer);
        VTABufferFree(fusedWeightBuffer);
        VTABufferFree(finalOutputBuffer);
        return 1;
    }

    printf("First fused matrix M_0 = P * D_0:\n");
    printArray(fusedWeight, BLOCK, BLOCK);

    VTABufferCopy(fusedWeight, 0, fusedWeightBuffer, 0,
                  sizeof(fusedWeight), VTA_MEMCPY_H2D);

    VTACommandHandle handle;
    handle = VTATLSCommandHandle();

    VTALoadBuffer2D(handle, inputBuffer, 0,
                    1, OUTPUT_PIXELS * KERNEL_TAPS, 1,
                    0, 0, 0, 0,
                    0, VTA_MEM_ID_INP);

    VTALoadBuffer2D(handle, fusedWeightBuffer, 0,
                    1, KERNEL_TAPS, 1,
                    0, 0, 0, 0,
                    0, VTA_MEM_ID_WGT);

    VTADepPush(handle, LOAD, COMPUTE);
    VTADepPop(handle, LOAD, COMPUTE);

    VTAPushGEMMOp(&RESET_cache_handle, reset_init, NULL, 0);
    VTAPushGEMMOp(&FUSED_cache_handle, fused_init, NULL, 0);

    VTADepPush(handle, COMPUTE, STORE);
    VTADepPop(handle, COMPUTE, STORE);

    VTAStoreBuffer2D(handle, 0, VTA_MEM_ID_OUT,
                     finalOutputBuffer, 0,
                     1, OUTPUT_PIXELS, 1);

    VTASynchronize(handle, 1000000);

    int8_t finalOutput[OUTPUT_PIXELS * BATCH * BLOCK];
    VTABufferCopy(finalOutputBuffer, 0, finalOutput, 0,
                  sizeof(finalOutput), VTA_MEMCPY_D2H);

    int errors = 0;

    printf("Offline fused output:\n");

    for (int p = 0; p < OUTPUT_PIXELS; p++) {
        printf("Output pixel %d:\n", p);

        for (int c = 0; c < BLOCK; c++) {
            int got = finalOutput[p * BLOCK + c];
            int expected =
                (c % 2 == 0)
                ? -countValidTaps(p)
                :  countValidTaps(p);

            printf("%2d ", got);

            if (got != expected)
                errors++;
        }

        printf("\n");
    }

    if (errors == 0)
        printf("PASS: offline mathematical fusion works.\n");
    else
        printf("FAIL: %d incorrect fused output values.\n", errors);

    printStats();

    VTABufferFree(inputBuffer);
    VTABufferFree(fusedWeightBuffer);
    VTABufferFree(finalOutputBuffer);

    return errors == 0 ? 0 : 1;
}
