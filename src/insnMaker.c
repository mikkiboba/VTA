#include "insnMaker.h"

#include <string.h>
#include <stdio.h>


void memInsn( 
    VTAGenericInsn* insn_queue, 
    int insn_idx, 
    int opcode, 
    int mem_type, 
    int sram_base, 
    uint32_t dram_base,
    int x_size,
    int y_size,
    int x_stride,
    int pop_prev,
    int pop_next,
    int push_prev,
    int push_next
) {
    VTAMemInsn* insn = (VTAMemInsn*)&insn_queue[insn_idx];
    memset(insn, 0, sizeof(VTAMemInsn));

    insn->opcode        = opcode;
    insn->memory_type   = mem_type;
    insn->sram_base     = sram_base;
    insn->dram_base     = dram_base;
    insn->x_size        = x_size;
    insn->y_size        = y_size;
    insn->x_stride      = x_stride;
    insn->pop_prev_dep  = pop_prev;
    insn->pop_next_dep  = pop_next;
    insn->push_prev_dep = push_prev;
    insn->push_next_dep = push_next;
}


void gemmInsn(
    VTAGenericInsn* insn_queue,
    int insn_idx,
    int opcode,
    int reset_reg,
    int uop_bgn,
    int uop_end,
    int iter_out,
    int iter_in,
    int dst_factor,
    int src_factor,
    int wgt_factor,
    int pop_prev,
    int pop_next,
    int push_prev,
    int push_next
){
    VTAGemInsn* insn = (VTAGemInsn*)&insn_queue[insn_idx];
    memset(insn, 0, sizeof(VTAGemInsn));

    insn->opcode            = opcode;
    insn->reset_reg         = reset_reg;
    insn->uop_bgn           = uop_bgn;
    insn->uop_end           = uop_end;
    insn->iter_out          = iter_out;
    insn->iter_in           = iter_in;
    insn->dst_factor_out    = dst_factor;
    insn->src_factor_out    = src_factor;
    insn->wgt_factor_out    = wgt_factor;
    insn->pop_prev_dep      = pop_prev;
    insn->pop_next_dep      = pop_next;
    insn->push_prev_dep     = push_prev;
    insn->push_next_dep     = push_next;
}


void aluInsn(
    VTAGenericInsn* insn_queue,
    int insn_idx,
    int opcode,
    int use_imm,
    int imm,
    int uop_bgn,
    int uop_end,
    int iter_out,
    int iter_in,
    int pop_prev,
    int pop_next,
    int push_prev,
    int push_next
) {
    VTAAluInsn* insn = (VTAAluInsn*)&insn_queue[insn_idx];
    memset(insn, 0, sizeof(VTAAluInsn));

    insn->opcode        = opcode;
    insn->use_imm       = use_imm;
    insn->imm           = imm;
    insn->uop_bgn       = uop_bgn;
    insn->uop_end       = uop_end;
    insn->iter_out      = iter_out;
    insn->iter_in       = iter_in;
    insn->pop_prev_dep  = pop_prev;
    insn->pop_next_dep  = pop_next;
    insn->push_prev_dep = push_prev;
    insn->push_next_dep = push_next;
}
