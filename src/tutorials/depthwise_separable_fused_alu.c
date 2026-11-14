/*
 * TUTORIAL C - ALU FUSION
 *
 * This experiment keeps the compact diagonal depthwise representation
 * and removes the depthwise -> pointwise DRAM round trip.
 *
 * The depthwise result remains in ACC SRAM and the pointwise stage is
 * computed directly through experimental ALU operations:
 *
 *   GEMM_DIAG -> ACC SRAM -> ALU.DOT + ALU.SHI + ALU.ADD -> output
 *
 * Results for the current test:
 *
 *   DRAM depthwise weights: 2304 B -> 144 B
 *   on-chip depthwise WGT:   2304 B -> 144 B
 *   depthwise MACs:          9216 -> 576
 *   DW->PW DRAM traffic:     128 B -> 0 B
 *
 * Pointwise computation:
 *
 *   16 DOT operations per output pixel
 *   16 ADD operations per output pixel
 *   15 SHI operations per output pixel
 *
 * The depthwise ACC values are consumed directly by ALU.DOT.
 */


#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include "runtime.h"
#include "vta/hw_spec_const.h"
#include "vta/runtime/runtime.h"

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

static void *DOT_cache_handle = NULL;
static void *ADD_cache_handle = NULL;
static void *SHI_cache_handle = NULL;

static void *PACK_RESET_cache_handle = NULL;


typedef struct
{
    uint32_t key;
} DotSignature;

typedef struct
{
    uint32_t dstAcc;
} PackSignature;


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


static int packedResetInit(void *signature)
{
    VTAUopLoopBegin(OUTPUT_PIXELS, 1, 0, 0);
        VTAUopPush(0, 1, 4, 0, 0, 0, 0, 0);
    VTAUopLoopEnd();

    return 0;
}


static int dotInit(void *signature)
{
    DotSignature *sig;
    sig = (DotSignature *)signature;

    uint32_t srcAcc, wgtRow;
    srcAcc = sig->key / BLOCK;
    wgtRow = sig->key % BLOCK;

    // ACC[8] = DOT(ACC[srcAcc], WGT[wgtSrc])
    VTAUopPush(1, 0, 8, srcAcc, wgtRow, VTA_ALU_OPCODE_DOT, 0, 0);

    return 0;
}


static int addInit(void *signature)
{
    PackSignature *sig;
    sig = (PackSignature *)signature;

    // ACC[dstAcc] += ACC[8]
    VTAUopPush(1, 0, sig->dstAcc, 8, 0, VTA_ALU_OPCODE_ADD, 0, 0);

    return 0;
}


static int shiInit(void *signature)
{
    PackSignature *sig;
    sig = (PackSignature *)signature;

    // ACC[dstAcc] = SHI(ACC[dstAcc])
    VTAUopPush(1, 0, sig->dstAcc, sig->dstAcc, 0, VTA_ALU_OPCODE_SHI, 0, 0);

    return 0;
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
    int compactWeights = KERNEL_TAPS * BLOCK * sizeof(int8_t);
    int denseWeights =
        KERNEL_TAPS * BLOCK * BLOCK * sizeof(int8_t);
    int intermediate =
        OUTPUT_PIXELS * BATCH * BLOCK * sizeof(int8_t);
    int denseMACs =
        OUTPUT_PIXELS * KERNEL_TAPS * BLOCK * BLOCK;
    int diagMACs =
        OUTPUT_PIXELS * KERNEL_TAPS * BLOCK;

    printf("\nC statistics:\n");
    printf("Baseline dense DRAM weights:    %d bytes\n", denseWeights);
    printf("C compact DRAM weights:         %d bytes\n", compactWeights);
    printf("Baseline dense SRAM footprint:  %d bytes\n", denseWeights);
    printf("C compact SRAM footprint:       %d bytes\n", compactWeights);

    printf("\nDepthwise computation:\n");
    printf("Baseline dense GEMM MACs:       %d\n", denseMACs);
    printf("C diagonal GEMM MACs:           %d\n", diagMACs);
    printf("MAC reduction:                  %.2fx\n",
           (double)denseMACs / diagMACs);

    printf("\nDepthwise -> pointwise:\n");
    printf("Logical intermediate size:      %d bytes\n", intermediate);
    printf("Baseline intermediate STORE:    %d bytes\n", intermediate);
    printf("Baseline intermediate LOAD:     %d bytes\n", intermediate);
    printf("Baseline DRAM traffic:          %d bytes\n", intermediate * 2);
    printf("C intermediate STORE:           0 bytes\n");
    printf("C intermediate LOAD:            0 bytes\n");
    printf("C intermediate DRAM traffic:    0 bytes\n");
    printf("DRAM traffic reduction:         100.00%%\n");

    printf("\nSimulator measured MACs:\n");
    printf("Dense scalar MACs:              %llu\n",
           (unsigned long long)VTAGetDenseMACCounter());
    printf("Diagonal scalar MACs:           %llu\n",
           (unsigned long long)VTAGetDiagMACCounter());
}

int main(void)
{
    void *inputBuffer =
        VTABufferAlloc(OUTPUT_PIXELS * KERNEL_TAPS * BATCH * BLOCK);
    void *weightBuffer =
        VTABufferAlloc(KERNEL_TAPS * BLOCK);
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

    VTASynchronize(handle, 1000000);

    printf("Depthwise output kept in ACC SRAM for alu pointwise op.\n");

    VTALoadBuffer2D(handle, pointwiseWeightBuffer, 0, 1, 1, 1,
                    0, 0, 0, 0, 0, VTA_MEM_ID_WGT);

    VTADepPush(handle, LOAD, COMPUTE);
    VTADepPop(handle, LOAD, COMPUTE);

    VTAPushGEMMOp(&PACK_RESET_cache_handle, packedResetInit, NULL, 0);

    for (int pixel = 0; pixel < OUTPUT_PIXELS; ++pixel)
    {
        PackSignature pack;
        pack.dstAcc = 4 + pixel;

        for (int row = BLOCK - 1; row >= 0; --row)
        {
            DotSignature dot;
            dot.key = pixel * BLOCK + row;

            VTAPushALUOp(&DOT_cache_handle, dotInit, &dot, sizeof(dot));
            VTAPushALUOp(&ADD_cache_handle, addInit, &pack, sizeof(pack));

            if (row != 0)
                VTAPushALUOp(&SHI_cache_handle, shiInit, &pack, sizeof(pack));
        }
    }

    VTADepPop(handle, COMPUTE, STORE);
    VTADepPush(handle, COMPUTE, STORE);

    VTAStoreBuffer2D(handle, 4, VTA_MEM_ID_OUT, finalOutputBuffer, 0, 1, OUTPUT_PIXELS, 1);

    VTASynchronize(handle, 1000000);

    int8_t finalOutput[OUTPUT_PIXELS * BLOCK];
    VTABufferCopy(finalOutputBuffer, 0, finalOutput, 0, sizeof(finalOutput), VTA_MEMCPY_D2H);

    validatePointwise(finalOutput);

    printf("\nALU statistics:\n");
    printf("ALU.DOT operations:             %llu\n", (unsigned long long)VTAGetALUDotCounter());
    printf("ALU.SHI operations:             %llu\n", (unsigned long long)VTAGetALUSHICounter());

    VTABufferFree(inputBuffer);
    VTABufferFree(weightBuffer);
    VTABufferFree(pointwiseWeightBuffer);
    VTABufferFree(finalOutputBuffer);

    return 0;
}
