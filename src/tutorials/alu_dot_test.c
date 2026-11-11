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


static int dotInit(void *signature)
{
    /* ALU DOT
     *
     * dst_idx = 1 -> destination ACC vec
     * src_idx = 0 -> source ACC vec
     * wgt_idx = 0 -> first pointwise weight row
     */
    VTAUopPush(1, 0, 1, 0, 0, VTA_ALU_OPCODE_DOT, 0, 0);

    return 0;
}


int main(void)
{
    void *srcBuffer, *weightBuffer, *outputBuffer;

    srcBuffer       = VTABufferAlloc(BLOCK * sizeof(int8_t));
    weightBuffer    = VTABufferAlloc(BLOCK * BLOCK * sizeof(int8_t));
    outputBuffer    = VTABufferAlloc(BLOCK * sizeof(int8_t));

    int8_t src[BLOCK], weights[BLOCK * BLOCK], output[BLOCK];

    // source: 1 2 3 4 1 2 3 4 1 2 3 4 1...
    // SUM = 4 * (1 + 2 + 3 + 4) = 40
    for (int i = 0; i < BLOCK; i++)
        src[i] = (i % 4) + 1;

    // only row 0 matters for the test
    memset(weights, 0, sizeof(weights));
    for (int i = 0; i < BLOCK; i++)
        weights[i] = 1;

    VTABufferCopy(src, 0, srcBuffer, 0, sizeof(src), VTA_MEMCPY_H2D);
    VTABufferCopy(weights, 0, weightBuffer, 0, sizeof(weights), VTA_MEMCPY_H2D);

    VTACommandHandle handle;
    handle = VTATLSCommandHandle();

    // load src directly into ACC SRAM idx 0
    VTALoadBuffer2D(handle, srcBuffer, 0, 1, 1, 1, 0, 0, 0, 0, 0, VTA_MEM_ID_ACC_8BIT);

    // load a 16x16 weight tile into WGT SRAM
    VTALoadBuffer2D(handle, weightBuffer, 0, 1, 1, 1, 0, 0, 0, 0, 0, VTA_MEM_ID_WGT);

    VTADepPush(handle, LOAD, COMPUTE);
    VTADepPop(handle, LOAD, COMPUTE);

    VTAPushALUOp(&DOT_cache_handle, dotInit, NULL, 0);

    VTADepPush(handle, COMPUTE, STORE);
    VTADepPop(handle, COMPUTE, STORE);

    VTAStoreBuffer2D(handle, 1, VTA_MEM_ID_OUT, outputBuffer, 0, 1, 1, 1);

    VTASynchronize(handle, 1000000);

    VTABufferCopy(outputBuffer, 0, output, 0, sizeof(output), VTA_MEMCPY_D2H);

    printf("ALU.DOT output: ");

    for (int i = 0; i < BLOCK; i++)
        printf("%d ", output[i]);

    printf("\n");

    int errors;
    errors = 0;

    if (output[0] != 40)
        errors++;

    for (int i = 0; i < BLOCK; ++i)
        if (output[0] != 40)
            errors++;

    if (errors == 0)
        printf("PASS: ALU.DOT works.\n");
    else
        printf("FAIL: ALU.DOT produced %d incorrect values.\n",
               errors);

    VTABufferFree(srcBuffer);
    VTABufferFree(weightBuffer);
    VTABufferFree(outputBuffer);

    return 0;
}
