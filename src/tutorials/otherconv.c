#include <stdio.h>
#include <stdint.h>
#include <string.h>

#include <vta/hw_spec.h>
#include <vta/driver.h>

#include "insnMaker.h"


#define HEIGHT 4
#define WIDTH 4
#define TILES (HEIGHT * WIDTH)

#define N_UOPS 2
#define INSN_QUEUE_SIZE 1024


int main(void) {
    int inpSize, wgtSize, outSize;
    inpSize = TILES * VTA_BATCH * VTA_BLOCK_IN * sizeof(int8_t);
    wgtSize = VTA_BLOCK_OUT * VTA_BLOCK_IN * sizeof(int8_t);
    outSize = TILES * VTA_BATCH * VTA_BLOCK_OUT * sizeof(int8_t);

    int uopSize, insnSize;
    uopSize  = N_UOPS * sizeof(VTAUop);
    insnSize = INSN_QUEUE_SIZE * sizeof(VTAGenericInsn);
    
    int8_t *inputs, *weights, *outputs;
    inputs  = (int8_t*)VTAMemAlloc(inpSize, 0);
    weights = (int8_t*)VTAMemAlloc(wgtSize, 0);
    outputs = (int8_t*)VTAMemAlloc(outSize, 0);

    VTAUop* uops          = (VTAUop*)VTAMemAlloc(uopSize, 0);
    VTAGenericInsn* insnQ = (VTAGenericInsn*)VTAMemAlloc(insnSize, 0);
    memset(insnQ, 0, insnSize);


    for (int i = 0; i < TILES * VTA_BATCH * VTA_BLOCK_IN; i++) inputs[i] = 2;
    for (int i = 0; i < VTA_BLOCK_OUT * VTA_BLOCK_IN; i++) weights[i] = 1;
    for (int i = 0; i < TILES * VTA_BATCH * VTA_BLOCK_OUT; i++) outputs[i] = 0;


    vta_phy_addr_t phyInp, phyWgt, phyOut, phyUop, phyInsn;
    phyInp  = VTAMemGetPhyAddr(inputs);
    phyWgt  = VTAMemGetPhyAddr(weights);
    phyOut  = VTAMemGetPhyAddr(outputs);
    phyUop  = VTAMemGetPhyAddr(uops);
    phyInsn = VTAMemGetPhyAddr(insnQ);


    uops[0].dst_idx = 0; uops[0].src_idx = 0; uops[0].wgt_idx = 0;
    uops[1].dst_idx = 0; uops[1].src_idx = 1; uops[1].wgt_idx = 1;


    int IC = 0;
    memInsn(insnQ, IC++, VTA_OPCODE_LOAD, VTA_MEM_ID_UOP, 0, phyUop/VTA_UOP_ELEM_BYTES,
        1, 1, 1, 0, 0, 0, 0);

    for (int i = 0; i < TILES; i++) {
        int isFirst = (i == 0);
        int isLast  = (i == TILES - 1);
        int loadPop = isFirst ? 0 : 1;
        int nopPush = isLast ? 0 : 1;

        memInsn(insnQ, IC++, VTA_OPCODE_LOAD, VTA_MEM_ID_INP, 0, (phyInp / VTA_INP_ELEM_BYTES) + i,
            1, 1, 1, 0, loadPop, 0, 0);
        memInsn(insnQ, IC++, VTA_OPCODE_LOAD, VTA_MEM_ID_WGT, 0, (phyWgt/VTA_WGT_ELEM_BYTES),
            1, 1, 1, 0, 0, 0, 1);

        // 1) reset dell'accumulatore per QUESTO tile (nessun MAC)
        gemmInsn(insnQ, IC++, VTA_OPCODE_GEMM, /*reset=*/1, 0, 1, 1, 1, 0, 0, 0,
            /*pop_prev*/0, /*pop_next*/0, /*push_prev*/0, /*push_next*/0);

        // 2) compute effettivo
        gemmInsn(insnQ, IC++, VTA_OPCODE_GEMM, /*reset=*/0, 0, 2, 1, 1, 0, 0, 0,
            /*pop_prev*/1, /*pop_next*/0, /*push_prev*/0, /*push_next*/1);

        memInsn(insnQ, IC++, VTA_OPCODE_STORE, VTA_MEM_ID_OUT, 0, (phyOut/VTA_OUT_ELEM_BYTES) + i,
            1, 1, 1, 1, 0, 1, 0);
        gemmInsn(insnQ, IC++, VTA_OPCODE_GEMM, 0, 0, 0, 1, 1, 0, 0, 0, 0, 1, nopPush, 0);
    }

    memInsn(insnQ, IC++, VTA_OPCODE_FINISH, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0);


    printf("Generazione istruzioni completata. Istruzioni totali: %d\n", IC);
    printf("Esecuzione sul simulatore VTA...\n");
    
    VTADeviceHandle device = VTADeviceAlloc();
    VTADeviceRun(device, phyInsn, IC, 1000000);


    printf("Esecuzione terminata. Verifica del tensore di output (valore atteso: 32):\n");
    for (int i = 0; i < outSize; i++) {
        if (i > 0 && i % 16 == 0) printf("\n");
        printf("%3d ", outputs[i]);
    }
    printf("\n");

    VTADeviceFree(device);
    VTAMemFree(inputs);
    VTAMemFree(weights);
    VTAMemFree(outputs);
    VTAMemFree(uops);
    VTAMemFree(insnQ);

    return 0;
}

