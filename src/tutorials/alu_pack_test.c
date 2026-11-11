#include <stdio.h>
#include <stdint.h>

#include "vta/runtime/runtime.h"
#include "vta/hw_spec_const.h"


#define BLOCK 16

#define LOAD    1
#define COMPUTE 2
#define STORE   3

static void *PACK_cache_handle = NULL;


static int packInit(void *signature)
{
    /* in place pack
     *
     * dst_idx = 0
     * src_idx = 0
     *
     * [1, 2, 3, ...] -> [0, 1, 2, ...]
     */
    VTAUopPush(1, 0, 0, 0, 0, VTA_ALU_OPCODE_SHI, 0, 0);

    return 0;
}


int main(void)
{
    void *srcBuffer, *outputBuffer;
    srcBuffer       = VTABufferAlloc(BLOCK * sizeof(int8_t));
    outputBuffer    = VTABufferAlloc(BLOCK * sizeof(int8_t));

    int8_t src[BLOCK], output[BLOCK];

    for (int i = 0; i < BLOCK; ++i)
        src[i] = i + 1;

    VTABufferCopy(src, 0, srcBuffer, 0, sizeof(src), VTA_MEMCPY_H2D);

    VTACommandHandle handle;
    handle = VTATLSCommandHandle();

    // load int8 vec into ACC SRAM idx 0
    VTALoadBuffer2D(handle, srcBuffer, 0, 1, 1, 1, 0, 0, 0, 0, 0, VTA_MEM_ID_ACC_8BIT);

    VTADepPush(handle, LOAD, COMPUTE);
    VTADepPop(handle, LOAD, COMPUTE);

    VTAPushALUOp(PACK_cache_handle, packInit, NULL, 0);

    VTADepPush(handle, COMPUTE, STORE);
    VTADepPop(handle, COMPUTE, STORE);

    VTAStoreBuffer2D(handle, 0, VTA_MEM_ID_OUT, outputBuffer, 0, 1, 1, 1);

    VTASynchronize(handle, 1000000);

    VTABufferCopy(outputBuffer, 0, output, 0, sizeof(output), VTA_MEMCPY_D2H);

    printf("ALU.SHI output:\n");

    for (int i = 0; i < BLOCK; ++i)
        printf("%d ", output[i]);

    printf("\n");

    int errors;
    errors = 0;

    if (output[0] != 0)
        errors++;

    for (int i = 0; i < BLOCK; ++i)
        if (output[i] != i)
            errors++;

    if (errors == 0)
        printf("PASS: ALU.PACK works.\n");
    else
        printf("FAIL: ALU.PACK produced %d incorrect values.\n",
               errors);

    VTABufferFree(srcBuffer);
    VTABufferFree(outputBuffer);

    return 0;
}
