#include <stdio.h>
#include <stdint.h>
#include <string.h>

#include "vta/runtime/runtime.h"
#include "vta/hw_spec_const.h"


#define BLOCK 16

#define LOAD    1
#define COMPUTE 2
#define STORE   3


static void *DOT_cache_handle = NULL;
static void *ADD_cache_handle = NULL;
static void *SHI_cache_handle = NULL;


/*
 * Signature used to select the pointwise weight row
 * for ALU.DOT.
 */
typedef struct
{
    uint32_t wgtRow;
} DotSignature;


/*
 * ACC[0] = source vector
 * ACC[1] = temporary DOT result
 * ACC[2] = packed output vector
 */


/*
 * ACC[1] = DOT(ACC[0], WGT[wgtRow])
 *
 * The scalar result is stored in lane 0 of ACC[1].
 */
static int dotInit(void *signature)
{
    DotSignature *sig;
    sig = (DotSignature*)signature;

    VTAUopPush(1, 0, 1, 0, sig->wgtRow, VTA_ALU_OPCODE_DOT, 0, 0);

    return 0;
}


/*
 * ACC[2] += ACC[1]
 */
static int addInit(void *signature)
{
    VTAUopPush(1, 0, 2, 1, 0, VTA_ALU_OPCODE_ADD, 0, 0);

    return 0;
}


/*
 * In-place lane shift:
 *
 * ACC[2] = SHI(ACC[2])
 *
 * [a, b, c, ...] -> [0, a, b, ...]
 */
static int shiInit(void *signature)
{
    VTAUopPush(1, 0, 2, 2, 0, VTA_ALU_OPCODE_SHI, 0, 0);

    return 0;
}


int main(void)
{
    void *srcBuffer;
    void *zeroBuffer;
    void *weightBuffer;
    void *outputBuffer;

    srcBuffer = VTABufferAlloc(BLOCK * sizeof(int8_t));
    zeroBuffer = VTABufferAlloc(BLOCK * sizeof(int8_t));
    weightBuffer = VTABufferAlloc(BLOCK * BLOCK * sizeof(int8_t));
    outputBuffer = VTABufferAlloc(BLOCK * sizeof(int8_t));

    int8_t src[BLOCK];
    int8_t zero[BLOCK];
    int8_t weights[BLOCK * BLOCK];
    int8_t output[BLOCK];


    /*
     * Source vector:
     *
     * [1, 2, 3, ..., 16]
     *
     * Since the pointwise matrix is the identity matrix,
     * the expected output is the same vector.
     */
    for (int i = 0; i < BLOCK; ++i)
        src[i] = i + 1;


    memset(zero, 0, sizeof(zero));
    memset(weights, 0, sizeof(weights));


    /*
     * 16x16 identity pointwise matrix.
     *
     * row 0  = [1, 0, 0, ...]
     * row 1  = [0, 1, 0, ...]
     * ...
     * row 15 = [..., 0, 1]
     */
    for (int i = 0; i < BLOCK; ++i)
        weights[i * BLOCK + i] = 1;


    /*
     * Copy test data to DRAM buffers.
     */
    VTABufferCopy(src, 0, srcBuffer, 0, sizeof(src), VTA_MEMCPY_H2D);
    VTABufferCopy(zero, 0, zeroBuffer, 0, sizeof(zero), VTA_MEMCPY_H2D);
    VTABufferCopy(weights, 0, weightBuffer, 0, sizeof(weights), VTA_MEMCPY_H2D);


    VTACommandHandle handle;
    handle = VTATLSCommandHandle();


    /*
     * ACC[0] = source vector.
     *
     * VTA_MEM_ID_ACC_8BIT loads int8 values from DRAM
     * and expands them to int32 inside ACC SRAM.
     */
    VTALoadBuffer2D(handle, srcBuffer, 0, 1, 1, 1, 0, 0, 0, 0, 0, VTA_MEM_ID_ACC_8BIT);


    /*
     * ACC[2] = zero vector.
     *
     * This is the destination vector in which the
     * pointwise scalar results are progressively packed.
     */
    VTALoadBuffer2D(handle, zeroBuffer, 0, 1, 1, 1, 0, 0, 0, 0, 2, VTA_MEM_ID_ACC_8BIT);


    /*
     * Load the complete 16x16 pointwise matrix into
     * the standard WGT SRAM.
     */
    VTALoadBuffer2D(handle, weightBuffer, 0, 1, 1, 1, 0, 0, 0, 0, 0, VTA_MEM_ID_WGT);


    /*
     * Ensure LOAD completes before computation.
     */
    VTADepPush(handle, LOAD, COMPUTE);
    VTADepPop(handle, LOAD, COMPUTE);


    /*
     * Build the complete 16-channel pointwise output.
     *
     * SHI moves existing results toward higher lanes:
     *
     * [a, b, c, ...] -> [0, a, b, ...]
     *
     * Therefore pointwise rows are processed in reverse order:
     *
     * row 15, row 14, ..., row 0.
     */
    for (int row = BLOCK - 1; row >= 0; --row)
    {
        DotSignature dot;
        dot.wgtRow = row;

        /*
         * ACC[1] = DOT(ACC[0], WGT[row])
         *
         * Result:
         *
         * ACC[1] = [scalar, 0, 0, ...]
         */
        VTAPushALUOp(&DOT_cache_handle, dotInit, &dot, sizeof(dot));


        /*
         * Insert the new scalar in lane 0:
         *
         * ACC[2] += ACC[1]
         */
        VTAPushALUOp(&ADD_cache_handle, addInit, NULL, 0);


        /*
         * Move the current packed results one lane to the right
         * to make space for the next scalar.
         *
         * No shift after row 0, otherwise the completed output
         * would be displaced by one lane.
         */
        if (row != 0)
            VTAPushALUOp(&SHI_cache_handle, shiInit, NULL, 0);
    }


    /*
     * Ensure computation completes before STORE.
     */
    VTADepPush(handle, COMPUTE, STORE);
    VTADepPop(handle, COMPUTE, STORE);


    /*
     * Store ACC[2].
     */
    VTAStoreBuffer2D(handle, 2, VTA_MEM_ID_OUT, outputBuffer, 0, 1, 1, 1);


    VTASynchronize(handle, 1000000);


    /*
     * Copy result back to CPU.
     */
    VTABufferCopy(outputBuffer, 0, output, 0, sizeof(output), VTA_MEMCPY_D2H);


    printf("DOT + SHI + ADD output:\n");

    for (int i = 0; i < BLOCK; ++i)
        printf("%d ", output[i]);

    printf("\n");


    /*
     * Identity pointwise matrix:
     *
     * expected = [1, 2, 3, ..., 16]
     */
    int errors;
    errors = 0;

    for (int i = 0; i < BLOCK; ++i)
    {
        if (output[i] != i + 1)
            errors++;
    }


    if (errors == 0)
    {
        printf("PASS: 16-channel DOT + SHI + ADD packing works.\n");
    }
    else
    {
        printf("FAIL: 16-channel DOT + SHI + ADD produced %d incorrect values.\n", errors);
    }


    VTABufferFree(srcBuffer);
    VTABufferFree(zeroBuffer);
    VTABufferFree(weightBuffer);
    VTABufferFree(outputBuffer);

    return 0;
}
