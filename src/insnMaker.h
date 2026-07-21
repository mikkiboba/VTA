#ifndef INSNMAKER_H
#define INSNMAKER_H

#include <vta/driver.h>
#include <vta/hw_spec.h>


/*! \brief Create a memory-type micro instruction (UOP). It can be LOAD/STORE.
    \param insn_queue Queue of micro instructions.
    \param insn_idx Index of the instruction in the queue.
    \param opcode Operation code of the micro instruction, taken from `hw_spec.h`
    \param mem_type Type of memory used. Taken from `hw_spec.h`
    \param sram_base Where to start in the SRAM of the device
    \param dram_base Where to start in the DRAM of the CPU
    \param x_size Length of the data
    \param y_size Heigth of the data
    \param x_stride How much space between rows of the data
    \param pop_prev Pop for previous dependecies
    \param pop_next Pop for next dependecies
    \param push_prev Push for previous dependecies 
    \param push_next Push for next dependecies 
*/
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
);


/*! \brief Create a micro instruction (UOP). It can be GEMM (also ALU, actually).
    \param insn_queue Queue of micro instructions.
    \param insn_idx Index of the instruction in the queue.
    \param opcode Operation code of the micro instruction, taken from `hw_spec.h`
    \param reset_reg Reset value (1 for cleaning, 0 for accumulating)
    \param uop_bgn Starting index of the UOP to execute from `insn_queue`
    \param uop_end Ending index (excluded) of the UOP to execute from `insn_queue` 
    \param iter_out Outer iterations
    \param iter_in Inner iterations
    \param dst_factor How much to advance the output at each iteration
    \param src_factor How much to advance the input at each iteration
    \param wgt_factor How much to advance the weight at each iteration
    \param pop_prev Pop for previous dependecies
    \param pop_next Pop for next dependecies
    \param push_prev Push for previous dependecies 
    \param push_next Push for next dependecies 
*/
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
);


/*! \brief Create an ALU-type micro instruction (UOP).
    \param insn_queue Queue of micro instructions.
    \param insn_idx Index of the instruction in the queue.
    \param opcode Operation code of the micro instruction, taken from `hw_spec.h`
    \param use_imm
    \param imm
    \param uop_bgn Starting index of the UOP to execute from `insn_queue`
    \param uop_end Ending index (excluded) of the UOP to execute from `insn_queue` 
    \param iter_out Outer iterations
    \param iter_in Inner iterations
    \param pop_prev Pop for previous dependecies
    \param pop_next Pop for next dependecies
    \param push_prev Push for previous dependecies 
    \param push_next Push for next dependecies 
*/
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
);

#endif