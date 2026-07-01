#include <stdio.h>
#include <stdint.h>
#include <string.h>

#include <vta/hw_spec.h>
#include <vta/driver.h>

#include "insnMaker.h"

#define N_INSTRUCTIONS 8
#define N_TILES 16
#define N_UOPS 2
#define INSN_QUEUE_SIZE 10


int main(void) {
    printf("Initialization of the instruction pipeline (%d instructions)\n", N_INSTRUCTIONS);

    // * allocamento
    int inpSize = N_TILES * VTA_BATCH * VTA_BLOCK_IN * sizeof(int8_t);
    int wgtSize = N_TILES * VTA_BLOCK_IN * VTA_BLOCK_OUT * sizeof(int8_t);
    int outSize = N_TILES * VTA_BATCH * VTA_BLOCK_OUT * sizeof(int8_t);

    int uopSize = N_UOPS * sizeof(VTAUop);
    int insnSize = INSN_QUEUE_SIZE * sizeof(VTAGenericInsn);

    
    int8_t* inputs = (int8_t*)VTAMemAlloc(inpSize, 0);
    int8_t* weights = (int8_t*)VTAMemAlloc(wgtSize, 0);
    int8_t* outputs = (int8_t*)VTAMemAlloc(outSize, 0);

    VTAUop* uops = (VTAUop*)VTAMemAlloc(uopSize, 0);
    VTAGenericInsn* insnsQ = (VTAGenericInsn*)VTAMemAlloc(insnSize, 0);

    memset(insnsQ, 0, insnSize);


    // * init dei dati
    int inpElements = N_TILES * VTA_BATCH * VTA_BLOCK_IN;
    printf("%d x %d x %d = %d\n",N_TILES, VTA_BATCH, VTA_BLOCK_IN, inpElements);
    for (int i = 0; i < inpElements; i++) {
        inputs[i] = 1;
    }
    
    int wgtElements = N_TILES * VTA_BLOCK_IN * VTA_BLOCK_OUT;
    printf("%d x %d x %d = %d\n ",N_TILES, VTA_BLOCK_IN, VTA_BLOCK_OUT, wgtElements);
    for (int i = 0; i < wgtElements; i++) {
        weights[i] = 2;
    }

    int outElements = N_TILES * VTA_BATCH * VTA_BLOCK_OUT;
    for (int i = 0; i < outElements; i++) {
        outputs[i] = 0;
    }


    vta_phy_addr_t phyInp = VTAMemGetPhyAddr(inputs);
    vta_phy_addr_t phyWgt = VTAMemGetPhyAddr(weights);
    vta_phy_addr_t phyOut = VTAMemGetPhyAddr(outputs);
    vta_phy_addr_t phyUop = VTAMemGetPhyAddr(uops);
    vta_phy_addr_t phyInsn = VTAMemGetPhyAddr(insnsQ);


    // * configurazione uop
    uops[0].dst_idx = 0; uops[0].src_idx = 0; uops[0].wgt_idx = 0;
    uops[1].dst_idx = 1; uops[1].src_idx = 0; uops[1].wgt_idx = 1;


    int insnCount = 0;


    // * i0: load uops
    memInsn(insnsQ, insnCount++, VTA_OPCODE_LOAD, VTA_MEM_ID_UOP, 0, phyUop / VTA_UOP_ELEM_BYTES, 
        N_INSTRUCTIONS, 1, N_INSTRUCTIONS, 0, 0, 0, 0);

    // * i1: load inp
    memInsn(insnsQ, insnCount++, VTA_OPCODE_LOAD, VTA_MEM_ID_INP, 0, phyInp / VTA_INP_ELEM_BYTES, 
        N_TILES, 1, N_TILES, 0, 0, 0, 0);

    // * i2: load wgt
    memInsn(insnsQ, insnCount++, VTA_OPCODE_LOAD, VTA_MEM_ID_WGT, 0, phyWgt / VTA_WGT_ELEM_BYTES, 
        N_TILES, N_TILES, N_TILES, 0, 0, 0, 1);

    // * i3: gemm reset 1
    gemmInsn(insnsQ, insnCount++, VTA_OPCODE_GEMM, 1, 0, 2, 1, 1, 1, 1, 1, 1, 0, 0, 0);

    // * i4: gemm reset 0
    gemmInsn(insnsQ, insnCount++, VTA_OPCODE_GEMM, 0, 0, 2, 1, 1, 1, 1, 1, 0, 0, 0, 1);

    // * i5: store
    memInsn(insnsQ, insnCount++, VTA_OPCODE_STORE, VTA_MEM_ID_OUT, 0, phyOut / VTA_OUT_ELEM_BYTES, N_TILES, 1, N_TILES, 1, 0, 1, 0);

    // * i6: nop synch
    gemmInsn(insnsQ, insnCount++, VTA_OPCODE_GEMM, 0, 0, 0, 1, 1, 1, 1, 1, 0, 1, 0, 0);

    // * i7: finish
    memInsn(insnsQ, insnCount++, VTA_OPCODE_FINISH, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0);


    printf("Execution of commands in the VTA simuator...\n");
    VTADeviceHandle device = VTADeviceAlloc();
    VTADeviceRun(device, phyInsn, N_INSTRUCTIONS, 1000000);

    printf("Finished the execution of commands. The results are waiting in memory.\n");

    int n_el = 32;
    printf("First %d of the OUTPUT buffer loaded: \n", n_el);
    for (int i = 0; i < n_el; i++) {
        if (i > 0 && i % 16 == 0) printf("\n");
        printf("%3d ", outputs[i]);
    }
    printf("\n");

    VTADeviceFree(device);
    VTAMemFree(inputs);
    VTAMemFree(weights);
    VTAMemFree(outputs);
    VTAMemFree(uops);
    VTAMemFree(insnsQ);

    return 0;

}