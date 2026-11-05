
/*
 * TUTORIAL A
 *
 * A1: compact depthwise weights in DRAM
 * A2: GEMM_DIAG to avoid dense 16x16 computation
 *
 * Historical simulator behavior:
 * compact 16-byte diagonal vectors were expanded on load into the
 * original dense WGT SRAM. Therefore:
 *
 *   DRAM depthwise weights: 2304 B -> 144 B
 *   on-chip depthwise WGT:  2304 B (still dense)
 *   depthwise MACs:         9216 -> 576
 *   DW->PW DRAM traffic:    128 B (unchanged)
 *
 * Exact historical simulator checkpoint:
 * 0d23eba7d93d09dff877b70767da0d4a9e8f8d12
 *
 * NOTE: if this tutorial is run with the later B/C simulator,
 * the functional output is still valid, but the on-chip weight
 * representation is already compact and therefore no longer reproduces
 * A's historical dense-SRAM footprint.
 */


#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include "runtime.h"
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
static void *DEPTH_cache_handle = NULL;
static void *POINT_cache_handle = NULL;

static void printArray(int8_t array[], int rows, int cols)
{
    for (int r = 0; r < rows; r++) {
        for (int c = 0; c < cols; c++)
            printf("%2d ", array[cols * r + c]);
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

static int depthwise_init(void *signature)
{
    VTAUopLoopBegin(OUTPUT_PIXELS, 1, KERNEL_TAPS, 0);
        VTAUopLoopBegin(KERNEL_TAPS, 0, 1, 1);
            VTAUopPush(0, 0, 0, 0, 0, 0, 0, 0);
        VTAUopLoopEnd();
    VTAUopLoopEnd();
    return 0;
}

static int pointwise_init(void *signature)
{
    VTAUopLoopBegin(OUTPUT_PIXELS, 1, 1, 0);
        VTAUopPush(0, 0, 0, 0, 0, 0, 0, 0);
    VTAUopLoopEnd();
    return 0;
}

static void buildInput(int8_t *input, int8_t *image)
{
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
}

static void buildDepthwiseWeights(int8_t *weight)
{
    for (int tap = 0; tap < KERNEL_TAPS; tap++)
        for (int c = 0; c < BLOCK; c++)
            weight[tap * BLOCK + c] = 1;
}

static void buildPointwiseWeights(int8_t *weight)
{
    memset(weight, 0, BLOCK * BLOCK * sizeof(int8_t));

    for (int c = 0; c < BLOCK; c += 2) {
        weight[c * BLOCK + c] = 1;
        weight[c * BLOCK + c + 1] = -1;
        weight[(c + 1) * BLOCK + c] = -1;
        weight[(c + 1) * BLOCK + c + 1] = 1;
    }
}

static int validateDepthwise(int8_t *depthOutput, int8_t *image)
{
    int errors = 0;

    for (int oy = 0; oy < OUTPUT_H; oy++)
        for (int ox = 0; ox < OUTPUT_W; ox++) {
            int p = oy * OUTPUT_W + ox;
            int expected = 0;

            for (int ky = 0; ky < KERNEL; ky++)
                for (int kx = 0; kx < KERNEL; kx++) {
                    int inY = oy * STRIDE_H + ky - PAD_TOP;
                    int inX = ox * STRIDE_W + kx - PAD_LEFT;

                    if (inY >= 0 && inY < INPUT_H &&
                        inX >= 0 && inX < INPUT_W)
                        expected += image[inY * INPUT_W + inX];
                }

            printf("Output pixel %d, expected %d:\n", p, expected);

            for (int c = 0; c < BLOCK; c++) {
                int got = depthOutput[p * BLOCK + c];
                int expectedChannel =
                    expected + ((c % 2) * countValidTaps(p));

                printf("%2d ", got);
                if (got != expectedChannel)
                    errors++;
            }
            printf("\n");
        }

    if (errors == 0)
        printf("PASS: depthwise convolution works.\n");
    else
        printf("FAIL: %d incorrect depthwise values.\n", errors);

    return errors;
}

static int validatePointwise(int8_t *finalOutput)
{
    int errors = 0;

    printf("Pointwise output:\n");

    for (int p = 0; p < OUTPUT_PIXELS; p++) {
        printf("Output pixel %d:\n", p);

        for (int c = 0; c < BLOCK; c++) {
            int got = finalOutput[p * BLOCK + c];
            int expected =
                (c % 2 == 0) ? -countValidTaps(p) : countValidTaps(p);

            printf("%2d ", got);
            if (got != expected)
                errors++;
        }
        printf("\n");
    }

    if (errors == 0)
        printf("PASS: depthwise separable convolution works.\n");
    else
        printf("FAIL: %d incorrect values after pointwise.\n", errors);

    return errors;
}


static void printStats(void)
{
    int inputOriginalBytes =
        INPUT_H * INPUT_W * BATCH * BLOCK * sizeof(int8_t);
    int inputPackedBytes =
        OUTPUT_PIXELS * KERNEL_TAPS * BATCH * BLOCK * sizeof(int8_t);
    int idealWeights = KERNEL_TAPS * BLOCK * sizeof(int8_t);
    int baselineWeights =
        KERNEL_TAPS * BLOCK * BLOCK * sizeof(int8_t);
    int intermediate =
        OUTPUT_PIXELS * BATCH * BLOCK * sizeof(int8_t);
    int baselineMACs =
        OUTPUT_PIXELS * KERNEL_TAPS * BLOCK * BLOCK;
    int diagMACs =
        OUTPUT_PIXELS * KERNEL_TAPS * BLOCK;

    printf("\nA statistics:\n");
    printf("Original input:                 %d bytes\n", inputOriginalBytes);
    printf("Packed depthwise input:         %d bytes\n", inputPackedBytes);
    printf("Packed/original ratio:          %.2fx\n",
           (double)inputPackedBytes / inputOriginalBytes);

    printf("\nDepthwise weights:\n");
    printf("Baseline dense DRAM weights:    %d bytes\n", baselineWeights);
    printf("A compact DRAM weights:         %d bytes\n", idealWeights);
    printf("DRAM weight reduction:          %.2fx\n",
           (double)baselineWeights / idealWeights);
    printf("A expanded WGT SRAM footprint:  %d bytes\n", baselineWeights);

    printf("\nDepthwise computation:\n");
    printf("Baseline dense GEMM MACs:       %d\n", baselineMACs);
    printf("A diagonal GEMM MACs:           %d\n", diagMACs);
    printf("MAC reduction:                  %.2fx\n",
           (double)baselineMACs / diagMACs);

    printf("\nDepthwise -> pointwise:\n");
    printf("Intermediate STORE:             %d bytes\n", intermediate);
    printf("Intermediate LOAD:              %d bytes\n", intermediate);
    printf("Intermediate DRAM traffic:      %d bytes\n", intermediate * 2);
}

int main(void)
{
    void *inputBuffer =
        VTABufferAlloc(OUTPUT_PIXELS * KERNEL_TAPS * BATCH * BLOCK);
    void *weightBuffer =
        VTABufferAlloc(KERNEL_TAPS * BLOCK);
    void *intermediateBuffer =
        VTABufferAlloc(OUTPUT_PIXELS * BATCH * BLOCK);
    void *pointwiseWeightBuffer =
        VTABufferAlloc(BLOCK * BLOCK);
    void *finalOutputBuffer =
        VTABufferAlloc(OUTPUT_PIXELS * BATCH * BLOCK);

    int8_t image[INPUT_H * INPUT_W];
    int8_t input[OUTPUT_PIXELS * KERNEL_TAPS * BLOCK];
    int8_t weight[KERNEL_TAPS * BLOCK];
    int8_t pointwiseWeight[BLOCK * BLOCK];

    for (int i = 0; i < INPUT_H * INPUT_W; i++)
        image[i] = i + 1;

    buildInput(input, image);
    buildDepthwiseWeights(weight);
    buildPointwiseWeights(pointwiseWeight);

    VTABufferCopy(input, 0, inputBuffer, 0, sizeof(input), VTA_MEMCPY_H2D);
    VTABufferCopy(weight, 0, weightBuffer, 0, sizeof(weight), VTA_MEMCPY_H2D);
    VTABufferCopy(pointwiseWeight, 0, pointwiseWeightBuffer, 0,
                  sizeof(pointwiseWeight), VTA_MEMCPY_H2D);

    VTACommandHandle handle = VTATLSCommandHandle();

    VTALoadBuffer2D(handle, inputBuffer, 0, 1,
                    OUTPUT_PIXELS * KERNEL_TAPS, 1,
                    0, 0, 0, 0, 0, VTA_MEM_ID_INP);

    VTALoadBuffer2D(handle, weightBuffer, 0, 1,
                    KERNEL_TAPS, 1,
                    0, 0, 0, 0, 0, VTA_MEM_ID_WGT_DIAG);

    VTADepPush(handle, LOAD, COMPUTE);
    VTADepPop(handle, LOAD, COMPUTE);

    VTAPushGEMMOp(&RESET_cache_handle, reset_init, NULL, 0);
    VTAPushGEMMDiagOp(&DEPTH_cache_handle, depthwise_init, NULL, 0);

    VTADepPush(handle, COMPUTE, STORE);
    VTADepPop(handle, COMPUTE, STORE);

    VTAStoreBuffer2D(handle, 0, VTA_MEM_ID_OUT,
                     intermediateBuffer, 0,
                     1, OUTPUT_PIXELS, 1);

    VTASynchronize(handle, 1000000);

    int8_t depthOutput[OUTPUT_PIXELS * BLOCK];
    VTABufferCopy(intermediateBuffer, 0, depthOutput, 0,
                  sizeof(depthOutput), VTA_MEMCPY_D2H);

    validateDepthwise(depthOutput, image);

    VTALoadBuffer2D(handle, intermediateBuffer, 0, 1,
                    OUTPUT_PIXELS, 1,
                    0, 0, 0, 0, 0, VTA_MEM_ID_INP);

    VTASynchronize(handle, 1000000);

    VTALoadBuffer2D(handle, pointwiseWeightBuffer, 0, 1, 1, 1,
                    0, 0, 0, 0, 0, VTA_MEM_ID_WGT);

    VTADepPush(handle, LOAD, COMPUTE);
    VTADepPop(handle, LOAD, COMPUTE);

    VTAPushGEMMOp(&RESET_cache_handle, reset_init, NULL, 0);
    VTAPushGEMMOp(&POINT_cache_handle, pointwise_init, NULL, 0);

    VTADepPush(handle, COMPUTE, STORE);
    VTADepPop(handle, COMPUTE, STORE);

    VTAStoreBuffer2D(handle, 0, VTA_MEM_ID_OUT,
                     finalOutputBuffer, 0,
                     1, OUTPUT_PIXELS, 1);

    VTASynchronize(handle, 1000000);

    int8_t finalOutput[OUTPUT_PIXELS * BLOCK];
    VTABufferCopy(finalOutputBuffer, 0, finalOutput, 0,
                  sizeof(finalOutput), VTA_MEMCPY_D2H);

    validatePointwise(finalOutput);
    printStats();

    VTABufferFree(inputBuffer);
    VTABufferFree(weightBuffer);
    VTABufferFree(intermediateBuffer);
    VTABufferFree(pointwiseWeightBuffer);
    VTABufferFree(finalOutputBuffer);

    return 0;
}
