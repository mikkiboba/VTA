#ifndef DISASSEMBLER_H
#define DISASSEMBLER_H

#include <stdint.h>
#include <vta/hw_spec.h>


/*! \brief STATUS codes*/
typedef enum {
    VTA_OK = 0,
    VTA_ERR_NULLPTR,
    VTA_ERR_INVALID_INSN_SIZE,
    VTA_ERR_NO_MEM_LABELS,
    VTA_ERR_INVALID_OPCODE,
    VTA_ERR_UOP_OOB,
} VTAErr;



static void errorPrint(VTAErr status) {
    switch (status)
    {
    case VTA_ERR_NULLPTR:
        fprintf(stderr, "[DIS_ERR]: NULL pointer to the disassembler.\n");
        break;
    case VTA_ERR_INVALID_INSN_SIZE:
        fprintf(stderr, "[DIS_ERR]: Invalid instruction size. There are no instruction to elaborate.\n");
        break;
    case VTA_ERR_NO_MEM_LABELS:
        fprintf(stderr, "[DIS_ERR]: No memory to allocate for labels.\n");
        break;
    case VTA_ERR_INVALID_OPCODE:
        fprintf(stderr, "[DIS_ERR]: Unknown or invalid opcode in the instruction stream.\n");
        break;
    case VTA_ERR_UOP_OOB:
        fprintf(stderr, "[DIS_ERR]: Micro operations (UOP) indices are out of bounds (bgn > end || end > VTA_UOP_BUFF_DEPTH).\n");
        break;
    default:
        break;
    }
}


/*! \brief Temporary memory to keep track of which UOP has a label*/
typedef struct {
    uint32_t bgn;
    uint32_t end;
    int labelID;
} UopLabelTracker;


/*! \brief Get the right label for the operation. If it doesn't exists, create it.
    \param labels Tracker of labels.
    \param numLabels Amount of existing labels.
    \param bgn Where the label begins.
    \param end Where the label ends.
    \param labelCounter Counter for labels.
*/
int findOrCreateLabel(
    UopLabelTracker* labels, 
    int* numLabels, 
    uint32_t bgn, 
    uint32_t end, 
    int* labelCounter
);


/*! \brief Print fprintf based on the status.
    \param status Status of type VTAErr.
*/
void errorPrint(VTAErr status);


/*! \brief Helper function to print POP (stage1->stage2)
    \param insn Instruction to work on
*/
void printPop(const VTAGenericInsn* insn);


/*! \brief Helper function to print PUSH (stage1->stage2)
    \param insn Instruction to work on
*/
void printPush(const VTAGenericInsn* insn);


/*! \brief Disassemble instructions to convert them in text
    \param insnsBuffer Buffer of instructions.
    \param numInsns How many instructions.
    \param uopBuffer Buffer of micro operations.
*/
VTAErr disassemble(
    const VTAGenericInsn* insnBuffer, 
    int numInsn, 
    const VTAUop* uopBuffer
);



#endif