#include <stdio.h>
#include <stdint.h>
#include <string.h>

#include <vta/hw_spec.h>
#include <vta/driver.h>


int main(void) {
    printf("Initialising instruction pipeline...\n");

    // * allocazione della memoria fisica
    int inpSize = 16 * VTA_BATCH * VTA_BLOCK_IN * sizeof(int8_t);
    int wgtSize = 16 * VTA_BLOCK_OUT * VTA_BLOCK_IN * sizeof(int8_t);
    int outSize = 16 * VTA_BATCH * VTA_BLOCK_OUT * sizeof(int8_t);

    int uopSize = 2 * sizeof(VTAUop);
    int insnSize = 10 * sizeof(VTAGenericInsn);

    int8_t* inputs = (int8_t*)VTAMemAlloc(inpSize, 0);
    int8_t* weights = (int8_t*)VTAMemAlloc(wgtSize, 0);
    int8_t* outputs = (int8_t*)VTAMemAlloc(outSize, 0);
    
    VTAUop* uops = (VTAUop*)VTAMemAlloc(uopSize, 0);
    VTAGenericInsn* insnQ = (VTAGenericInsn*)VTAMemAlloc(insnSize, 0);


    memset(insnQ, 0, insnSize);


    vta_phy_addr_t phyInp = VTAMemGetPhyAddr(inputs);
    vta_phy_addr_t phyWgt = VTAMemGetPhyAddr(weights);
    vta_phy_addr_t phyOut = VTAMemGetPhyAddr(outputs);
    vta_phy_addr_t phyUop = VTAMemGetPhyAddr(uops);
    vta_phy_addr_t phyInsn = VTAMemGetPhyAddr(insnQ);


    // * configurazione delle micro isturzioni (micro-ops (uops))
    // ? dovrebbero corrispondere al range (0,1) e (1,2) delle GEMM ops
    uops[0].dst_idx = 0; uops[0].src_idx = 0; uops[0].wgt_idx = 0;
    uops[1].dst_idx = 1; uops[1].src_idx = 0; uops[1].wgt_idx = 1;


    // * costruzione delle istruzioni
    // * Nota: ogni istruzione è da 128 bit

    // * i0: LOAD UOP (Carica ENTRAMBE le UOP in un solo colpo)
    VTAMemInsn* insn0 = (VTAMemInsn*)&insnQ[0];
    insn0->opcode = VTA_OPCODE_LOAD;
    insn0->memory_type = VTA_MEM_ID_UOP;
    insn0->sram_base = 0;
    insn0->dram_base = phyUop / VTA_UOP_ELEM_BYTES;
    insn0->y_size = 1;
    insn0->x_size = 2; // <-- Carica sia uops[0] che uops[1]
    insn0->x_stride = 2;

    // * i1: LOAD INPUT
    VTAMemInsn* insn1 = (VTAMemInsn*)&insnQ[1];
    insn1->opcode = VTA_OPCODE_LOAD;
    insn1->memory_type = VTA_MEM_ID_INP;
    insn1->sram_base = 0;
    insn1->dram_base = phyInp / VTA_INP_ELEM_BYTES;
    insn1->y_size = 1;
    insn1->x_size = 16;
    insn1->x_stride = 16;

    // * i2: LOAD WEIGHTS
    VTAMemInsn* insn2 = (VTAMemInsn*)&insnQ[2];
    insn2->opcode = VTA_OPCODE_LOAD;
    insn2->memory_type = VTA_MEM_ID_WGT;
    insn2->sram_base = 0;
    insn2->dram_base = phyWgt / VTA_WGT_ELEM_BYTES;
    insn2->y_size = 1;  // <-- IL BUG ERA QUI! Ora carica i 16 elementi corretti
    insn2->x_size = 16;
    insn2->x_stride = 16;
    insn2->push_next_dep = 1; // Sblocca la Compute!


    // * i3: GEMM 1
    VTAGemInsn* insn3 = (VTAGemInsn*)&insnQ[3];
    insn3->opcode = VTA_OPCODE_GEMM;
    insn3->reset_reg = 1;
    insn3->uop_bgn = 0;
    insn3->uop_end = 1;
    insn3->iter_out = 1;
    insn3->iter_in = 1;
    insn3->dst_factor_out = 1;
    insn3->src_factor_out = 1;
    insn3->wgt_factor_out = 1;
    insn3->pop_prev_dep = 1; // Aspetta i dati dal Load


    // * i4: GEMM 2
    VTAGemInsn* insn4 = (VTAGemInsn*)&insnQ[4];
    insn4->opcode = VTA_OPCODE_GEMM;
    insn4->reset_reg = 0;
    insn4->uop_bgn = 1;
    insn4->uop_end = 2;
    insn4->iter_out = 1;
    insn4->iter_in = 1;
    insn4->dst_factor_out = 1;
    insn4->src_factor_out = 1;
    insn4->wgt_factor_out = 1;
    insn4->push_next_dep = 1; // Sblocca la Store!


    // * i5: STORE
    VTAMemInsn* insn5 = (VTAMemInsn*)&insnQ[5];
    insn5->opcode = VTA_OPCODE_STORE;
    insn5->memory_type = VTA_MEM_ID_OUT;
    insn5->sram_base = 0;
    insn5->dram_base = phyOut / VTA_OUT_ELEM_BYTES;
    insn5->y_size = 1;
    insn5->x_size = 16;
    insn5->x_stride = 16;
    insn5->pop_prev_dep = 1;  // Aspetta i calcoli
    insn5->push_prev_dep = 1; // Conferma alla Compute


    // * i6: NOP 
    VTAGemInsn* insn6 = (VTAGemInsn*)&insnQ[6];
    insn6->opcode = VTA_OPCODE_GEMM;
    insn6->pop_next_dep = 1;  // Aspetta la conferma dalla Store


    // * i7: FINISH
    VTAMemInsn* insn7 = (VTAMemInsn*)&insnQ[7];
    insn7->opcode = VTA_OPCODE_FINISH;


    printf("Esecuzione della coda dei comandi bare-metal su vta...\n");
    VTADeviceHandle device = VTADeviceAlloc();

    VTADeviceRun(device, phyInsn, 8, 1000000);

    printf("Calcolo completato con successo! Risultati printi in memoria.\n");


    VTADeviceFree(device);
    VTAMemFree(inputs);
    VTAMemFree(weights);
    VTAMemFree(outputs);
    VTAMemFree(uops);
    VTAMemFree(insnQ);


    return 0;
}