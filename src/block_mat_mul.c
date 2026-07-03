#include <stdio.h>
#include <stdint.h>
#include <string.h>

#include <vta/hw_spec.h>
#include <vta/driver.h>

#include "insnMaker.h"

#define M_TILES 4
#define N_TILES 4
#define K_TILES 4

#define N_UOPS 1
#define INSN_QUEUE_SIZE 1024

int main(void) {
    printf("Inizializzazione della pipeline VTA per MatMul Blocking...\n");
 
    int inpSize, wgtSize, outSize;
    inpSize = M_TILES * K_TILES * VTA_BATCH * VTA_BLOCK_IN      * sizeof(int8_t);
    wgtSize = N_TILES * K_TILES * VTA_BLOCK_IN * VTA_BLOCK_OUT  * sizeof(int8_t);
    outSize = M_TILES * N_TILES * VTA_BATCH * VTA_BLOCK_OUT     * sizeof(int8_t);

    int uopSize, insnSize;
    uopSize  = N_UOPS * sizeof(VTAUop);
    insnSize = INSN_QUEUE_SIZE * sizeof(VTAGenericInsn);

    int8_t *inputs, *weights, *outputs;
    inputs  = (int8_t*)VTAMemAlloc(inpSize, 0);
    weights = (int8_t*)VTAMemAlloc(wgtSize, 0);
    outputs = (int8_t*)VTAMemAlloc(outSize, 0);

    VTAUop* uops;
    uops = (VTAUop*)VTAMemAlloc(uopSize, 0);

    VTAGenericInsn* insnsQ;
    insnsQ = (VTAGenericInsn*)VTAMemAlloc(insnSize, 0);
    memset(insnsQ, 0, insnSize);

    
    // * data init -> for testing
    for (int i = 0; i < (inpSize/sizeof(int8_t)); i++) inputs[i] = 1;
    for (int i = 0; i < (wgtSize/sizeof(int8_t)); i++) weights[i] = 1;
    for (int i = 0; i < (outSize/sizeof(int8_t)); i++) outputs[i] = 0;

    vta_phy_addr_t phyInp, phyWgt, phyOut, phyUop, phyInsn;
    phyInp  = VTAMemGetPhyAddr(inputs);
    phyWgt  = VTAMemGetPhyAddr(weights);
    phyOut  = VTAMemGetPhyAddr(outputs);
    phyUop  = VTAMemGetPhyAddr(uops);
    phyInsn = VTAMemGetPhyAddr(insnsQ);


    // * uop config
    uops[0].dst_idx = 0; uops[0].src_idx = 0; uops[0].wgt_idx = 0;

    int IC = 0;
    memInsn(insnsQ, IC++, VTA_OPCODE_LOAD, VTA_MEM_ID_UOP, 0, phyUop / VTA_UOP_ELEM_BYTES, 
        1, 1, 1, 0, 0, 0, 0);

    for (int i = 0; i < M_TILES; i++) {
        for (int j = 0; j < N_TILES; j++) {
            for (int k = 0; k < K_TILES; k++) {
                int isFirst, isLast;
                isFirst = (k == 0);
                isLast  = (k == K_TILES - 1);
                
                int loadPop;
                loadPop = (i == 0 && j == 0 && k == 0) ? 0 : 1;
                
                int inpIdx, wgtIdx, outIdx;
                inpIdx = i * K_TILES + k;
                wgtIdx = j * K_TILES + k;
                outIdx = i * N_TILES + j;

                memInsn(insnsQ, IC++, VTA_OPCODE_LOAD, VTA_MEM_ID_INP, 0, 
                    (phyInp / VTA_INP_ELEM_BYTES) + inpIdx, 1, 1, 1, 0, loadPop, 0, 0);

                memInsn(insnsQ, IC++, VTA_OPCODE_LOAD, VTA_MEM_ID_WGT, 0, 
                    (phyWgt / VTA_WGT_ELEM_BYTES) + wgtIdx, 1, 1, 1, 0, 0, 0, 1);

                int resetAcc, pushNext, pushPrev;
                resetAcc = isFirst ? 1 : 0;
                pushNext = isLast  ? 1 : 0;
                pushPrev = isLast  ? 0 : 1;
                
                gemmInsn(insnsQ, IC++, VTA_OPCODE_GEMM, resetAcc, 0, 1, 
                    1, 1, 0, 0, 0, 1, 0, pushPrev, pushNext);

                if (isLast) {
                    memInsn(insnsQ, IC++, VTA_OPCODE_STORE, VTA_MEM_ID_OUT, 0, 
                        (phyOut / VTA_OUT_ELEM_BYTES) + outIdx, 1, 1, 1, 1, 0, 1, 0);

                    gemmInsn(insnsQ, IC++, VTA_OPCODE_GEMM, 0, 0, 0, 1, 1, 0, 0, 0, 0, 1, 1, 0);
                }
            }
        }
    }

    memInsn(insnsQ, IC++, VTA_OPCODE_FINISH, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0);

    printf("Generazione completata. Totale istruzioni: %d\n", IC);
    printf("Esecuzione sul simulatore VTA...\n");
    
    VTADeviceHandle device = VTADeviceAlloc();
    VTADeviceRun(device, phyInsn, IC, 1000000);

    printf("Esecuzione terminata. Verifica dell'intero tensore di output (%d blocchi calcolati):\n", M_TILES * N_TILES);

    int n_el;
    n_el = M_TILES * N_TILES * VTA_BATCH * VTA_BLOCK_OUT;
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