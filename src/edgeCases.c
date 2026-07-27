#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <vta/hw_spec.h>
#include "insnMaker.h"

void disassemble(const VTAGenericInsn* insnBuffer, int numInsn, const VTAUop* uopBuffer);

#define MAX_INSN 12
#define MAX_UOP  15

int main(void) {
    VTAGenericInsn insn_stream[MAX_INSN];
    VTAUop uop_stream[MAX_UOP];

    memset(insn_stream, 0, sizeof(insn_stream));
    memset(uop_stream, 0, sizeof(uop_stream));

    for (int i = 0; i < MAX_UOP; ++i) {
        uop_stream[i].dst_idx = (i + 1) * 10;
        uop_stream[i].src_idx = (i + 1) * 10 + 1;
        uop_stream[i].wgt_idx = (i + 1) * 10 + 2;
    }

    int insn_idx = 0;

    // Caso 1: Condivisione totale di uOP [0:3] (riutilizzo lbl1)
    printf("=== Case 1: total sharing of uOP [0:3] (reuse of lbl1) ===\n");
    gemmInsn(insn_stream, insn_idx++, VTA_OPCODE_GEMM, 0, 0, 3, 2, 2, 0, 0, 0, 1, 0, 1, 0);
    gemmInsn(insn_stream, insn_idx++, VTA_OPCODE_GEMM, 0, 0, 3, 2, 2, 0, 0, 0, 1, 0, 1, 0);

    // Caso 2: Intervalli sovrapposti [2:5] (lbl2)
    printf("=== Case 2: overlapping intervals [2:5] lbl2 ===\n");
    gemmInsn(insn_stream, insn_idx++, VTA_OPCODE_GEMM, 0, 2, 5, 4, 1, 0, 0, 0, 0, 0, 0, 0);

    // Caso 3: Sotto-intervallo [1:3] (lbl3)
    printf("=== Case 3: sub-interval [1:3] lbl3 ===\n");
    aluInsn(insn_stream, insn_idx++, VTA_OPCODE_ALU, 0, 0, 1, 3, 1, 1, 0, 0, 0, 0);

    // Caso 4: NOOP (x_size == 0)
    printf("=== Case 4: NOOP (x_size == 0) ===\n");
    memInsn(insn_stream, insn_idx++, VTA_OPCODE_LOAD, VTA_MEM_ID_INP, 0, 0, 0, 0, 0, 0, 0, 0, 0);

    // Caso 5: Padding Completo (impostato via cast a VTAMemInsn)
    printf("=== Case 5: Complete padding (setup via cast on VTAMemInsn) ===\n");
    memInsn(insn_stream, insn_idx, VTA_OPCODE_LOAD, VTA_MEM_ID_INP, 100, 2000, 4, 2, 4, 0, 0, 0, 0);
    VTAMemInsn* pad_insn = (VTAMemInsn*)&insn_stream[insn_idx++];
    pad_insn->x_pad_0 = 1;
    pad_insn->x_pad_1 = 2;
    pad_insn->y_pad_0 = 3;
    pad_insn->y_pad_1 = 4;

    // Caso 6: ALU con Immediato Negativo
    printf("=== Case 6: ALU with immediate negative ===\n");
    aluInsn(insn_stream, insn_idx++, VTA_OPCODE_ALU, 1, -128, 0, 3, 1, 1, 0, 0, 0, 0);

    // Caso 7: PUSH e POP simultanei
    printf("=== Case 7: PUSH and POP at the same time ===\n");
    gemmInsn(insn_stream, insn_idx++, VTA_OPCODE_GEMM, 0, 0, 3, 1, 1, 0, 0, 0, 1, 1, 1, 1);

    // Caso 8: Intervallo non contiguo [10:13] (lbl4)
    printf("=== Case 8: Non contiguous interval ===\n");
    gemmInsn(insn_stream, insn_idx++, VTA_OPCODE_GEMM, 0, 10, 13, 1, 1, 0, 0, 0, 0, 0, 0, 0);

    insn_stream[insn_idx++].opcode = VTA_OPCODE_FINISH;

    printf("========================================================\n");
    printf(" RUNNING DISASSEMBLER EDGE CASE TESTS \n");
    printf("========================================================\n\n");

    disassemble(insn_stream, insn_idx, uop_stream);

    return 0;
}