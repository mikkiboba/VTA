/*  
    qui ci sono le informazioni sulle dimensioni richeiste dal codice, 
    trovate in /tvm/3rdparty/vta-hw/config/vta_config.json

    {
        "TARGET" : "sim",
        "HW_VER" : "0.0.2",
        "LOG_INP_WIDTH" : 3,
        "LOG_WGT_WIDTH" : 3,
        "LOG_ACC_WIDTH" : 5,
        "LOG_BATCH" : 0,
        "LOG_BLOCK" : 4,
        "LOG_UOP_BUFF_SIZE" : 15,
        "LOG_INP_BUFF_SIZE" : 15,
        "LOG_WGT_BUFF_SIZE" : 18,
        "LOG_ACC_BUFF_SIZE" : 17
    }

    le misure sono in LOG2 -> quindi per esempio per gli input richiede valori a 2^3 = 8 bit

*/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include "vta/runtime/runtime.h"
#include "vta/hw_spec_const.h"

#define BLOCK 16
#define BATCH 1

#define STAGE_LOAD 1
#define STAGE_COMPUTE 2
#define STAGE_STORE 3

static void* gemm_cache_handle = NULL;

// Signature per il kernel GEMM
typedef struct {
    int reset;
} GEMMSignature;

// Funzione di inizializzazione del kernel GEMM (versione compressa)
int gemm_init(void* signature) {
    GEMMSignature* sig = (GEMMSignature*)signature;

    // Outer loop: itera su j (asse input/in_feat), avanza src_idx e wgt_idx di 1
    VTAUopLoopBegin(BLOCK, 0, 1, 1);
        // Inner loop: itera su k (asse output/out_feat), avanza dst_idx di 1 e wgt_idx di BLOCK
        VTAUopLoopBegin(BLOCK, 1, 0, BLOCK);
            VTAUopPush(
                0,          // mode: 0 = GEMM
                sig->reset, // reset accumulatore
                0,          // dst_idx base
                0,          // src_idx base
                0,          // wgt_idx base
                0, 0, 0
            );
        VTAUopLoopEnd();
    VTAUopLoopEnd();

    return 0;
}

int main() {
    // Alloca input (1x16 di int8)
    void* input_buf = VTABufferAlloc(BATCH * BLOCK * sizeof(int8_t));
    // Alloca pesi (16x16 di int8)
    void* weight_buf = VTABufferAlloc(BLOCK * BLOCK * sizeof(int8_t));
    // Alloca output (1x16 di int8, NON int32 - l'output "OUT" usa la stessa larghezza dell'input)
    void* output_buf = VTABufferAlloc(BATCH * BLOCK * sizeof(int8_t));

    // Riempi input: tutti 1
    int8_t input[BATCH * BLOCK];
    memset(input, 1, sizeof(input));

    // Riempi pesi: tutti 1 (risultato atteso: ogni elemento = 16)
    int8_t weight[BLOCK * BLOCK];
    memset(weight, 2, sizeof(weight));

    // Copia dati host -> buffer VTA
    VTABufferCopy(input, 0, input_buf, 0, sizeof(input), VTA_MEMCPY_H2D);
    VTABufferCopy(weight, 0, weight_buf, 0, sizeof(weight), VTA_MEMCPY_H2D);

    // Ottieni command handle
    VTACommandHandle cmd = VTATLSCommandHandle();

    // Debug: stampa istruzioni e micro-op
    VTASetDebugMode(cmd, VTA_DEBUG_DUMP_INSN | VTA_DEBUG_DUMP_UOP);

    // Carica input dalla DRAM alla SRAM interna
    VTALoadBuffer2D(cmd, input_buf, 0, BLOCK, BATCH, BLOCK, 0, 0, 0, 0, 0, VTA_MEM_ID_INP);

    // Carica pesi dalla DRAM alla SRAM interna
    VTALoadBuffer2D(cmd, weight_buf, 0, BLOCK, BLOCK, BLOCK, 0, 0, 0, 0, 0, VTA_MEM_ID_WGT);

    // Dipendenza: compute aspetta che il load finisca
    VTADepPush(cmd, STAGE_LOAD, STAGE_COMPUTE);
    VTADepPop(cmd, STAGE_LOAD, STAGE_COMPUTE);

    // Esegui GEMM
    GEMMSignature sig = {1}; // reset=1 ore prima
    VTAPushGEMMOp(&gemm_cache_handle, gemm_init, &sig, sizeof(sig));

    sig.reset = 0; // ora accumula
    VTAPushGEMMOp(&gemm_cache_handle, gemm_init, &sig, sizeof(sig));

    // Dipendenza: store aspetta che compute finisca
    VTADepPush(cmd, STAGE_COMPUTE, STAGE_STORE);
    VTADepPop(cmd, STAGE_COMPUTE, STAGE_STORE);

    // Salva risultato dalla SRAM alla DRAM
    VTAStoreBuffer2D(cmd, 0, VTA_MEM_ID_OUT, output_buf, 0, BLOCK, BATCH, BLOCK);

    // Esegui e aspetta
    VTASynchronize(cmd, 1000000);

    // Leggi risultati
    int8_t output[BATCH * BLOCK];
    VTABufferCopy(output_buf, 0, output, 0, sizeof(output), VTA_MEMCPY_D2H);

    // Stampa risultati (atteso: tutti 16)
    printf("Output (atteso: tutti 16):\n");
    for (int i = 0; i < BLOCK; i++) {
        printf("%d ", output[i]);
    }
    printf("\n");

    // Libera memoria
    VTABufferFree(input_buf);
    VTABufferFree(weight_buf);
    VTABufferFree(output_buf);

    return 0;
}