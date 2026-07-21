#include <stdio.h>
#include <string.h>
#include <vta/hw_spec.h>
#include "insnMaker.h"

#define NUM_INSN 9
#define NUM_UOPS 8

void disassemble(const VTAGenericInsn* insn_buf, int num_insn, const VTAUop* uop_buf);

int main(void) {
    VTAGenericInsn insn_stream[NUM_INSN];
    VTAUop uop_stream[NUM_UOPS];

    memset(insn_stream, 0, sizeof(insn_stream));
    memset(uop_stream, 0, sizeof(uop_stream));

    uop_stream[0].dst_idx = 1; uop_stream[0].src_idx = 2; uop_stream[0].wgt_idx = 3;
    uop_stream[1].dst_idx = 4; uop_stream[1].src_idx = 5; uop_stream[1].wgt_idx = 6;
    uop_stream[2].dst_idx = 7; uop_stream[2].src_idx = 8; uop_stream[2].wgt_idx = 9;
    uop_stream[3].dst_idx = 10; uop_stream[3].src_idx = 11; uop_stream[3].wgt_idx = 0;
    uop_stream[4].dst_idx = 12; uop_stream[4].src_idx = 13; uop_stream[4].wgt_idx = 0;

    gemmInsn(insn_stream, 0, VTA_OPCODE_GEMM, 0, 0, 2, 4, 2, 0, 0, 0, 1, 1, 1, 1);
    gemmInsn(insn_stream, 1, VTA_OPCODE_GEMM, 1, 2, 3, 4, 2, 0, 0, 0, 1, 1, 1, 1);

    aluInsn(insn_stream, 2, VTA_OPCODE_ALU, 0, 0, 3, 4, 4, 2,
        1, 1, 1, 1);

    aluInsn(insn_stream, 3, VTA_OPCODE_ALU, 1, 128, 4, 5, 4, 2,
        1, 1, 1, 1);

    VTAMemInsn* load_pad = (VTAMemInsn*)&insn_stream[4];
    memInsn(insn_stream, 4, VTA_OPCODE_LOAD, VTA_MEM_ID_INP, 0, 0x1000, 16, 1, 16, 1, 0, 1, 0);
    load_pad->x_pad_0 = 1; load_pad->x_pad_1 = 1;
    load_pad->y_pad_0 = 1; load_pad->y_pad_1 = 1;

    memInsn(insn_stream, 5, VTA_OPCODE_STORE, VTA_MEM_ID_ACC, 0, 0x2000, 16, 1, 16, 1, 0, 1, 0);
    memInsn(insn_stream, 6, VTA_OPCODE_LOAD, VTA_MEM_ID_UOP, 0, 0x0500, 8, 1, 8, 1, 0, 1, 0);

    insn_stream[7].opcode = VTA_OPCODE_FINISH;
    memInsn(insn_stream, 8, VTA_OPCODE_LOAD, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0);

    printf("---- test\n\n");
    disassemble(insn_stream, NUM_INSN, uop_stream);

    return 0;
}