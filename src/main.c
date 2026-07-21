#include <stdio.h>
#include <string.h>
#include <vta/hw_spec.h>
#include "insnMaker.h"

#define NUM_INSN 5
#define NUM_UOPS 10

/*! \brief Disassembler test. It must be in `disassembler.c`*/
void disassemble(const VTAGenericInsn* insn_buf, int num_insn, const VTAUop* uop_buf);

int main(void) {
    VTAGenericInsn insn_stream[NUM_INSN];
    VTAUop uop_stream[NUM_UOPS];

    memset(insn_stream, 0, sizeof(insn_stream));
    memset(uop_stream, 0, sizeof(uop_stream));

    // dummy insn
    uop_stream[0].dst_idx = 1; uop_stream[0].src_idx = 2; uop_stream[0].wgt_idx = 3;
    uop_stream[1].dst_idx = 4; uop_stream[1].src_idx = 5; uop_stream[1].wgt_idx = 6;
    uop_stream[2].dst_idx = 7; uop_stream[2].src_idx = 8; uop_stream[2].wgt_idx = 9;

    memInsn(insn_stream, 0, VTA_OPCODE_LOAD, VTA_MEM_ID_INP, 0, 0x1000, 16, 1, 16, 
        0, 0, 0, 1); 
    gemmInsn(insn_stream, 1, VTA_OPCODE_GEMM, 0, 0, 3, 4, 2, 0, 0, 0, 
        1, 0, 0, 1);
    memInsn(insn_stream, 2, VTA_OPCODE_STORE, VTA_MEM_ID_ACC, 0, 0x2000, 16, 1, 16, 
        1, 0, 0, 0);
    memInsn(insn_stream, 3, VTA_OPCODE_LOAD, 0, 0, 0, 0, 0, 0, 
        0, 0, 0, 0);

    insn_stream[4].opcode = VTA_OPCODE_FINISH;

    printf("---- DISASSEMBLER ----\n\n");
    disassemble(insn_stream, NUM_INSN, uop_stream);

    return 0;
}