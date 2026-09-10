#ifndef VTA_H
#define VTA_H


#include <stdio.h>
#include <stdint.h>
#include <stddef.h>
#include <vta/hw_spec.h>


/*!
 * \brief STATUS code for VTA operations (for both disassembler and assembler).
 * \note VTA_OK = 0 guarantees success.
*/
typedef enum {
    VTA_OK = 0,

    VTA_ERR_NULLPTR,
    VTA_ERR_INVALID_INSN_SIZE,
    VTA_ERR_NO_MEM_LABELS,
    VTA_ERR_UNKNOWN_OPCODE,
    VTA_ERR_UOP_OUT_OF_BOUNDS,
    
    VTA_ERR_INSN_BUFFER_FULL,
    VTA_ERR_UOP_BUFFER_FULL,
    VTA_ERR_SYNTAX,
    VTA_ERR_UNKNOWN_MNEMONIC,
    VTA_ERR_LABEL_NOT_FOUND,
    VTA_ERR_OUT_OF_RANGE
} VTAErr;


/*!
 * \brief Assemble the VTA source code in binary instructions VTA and UOPs.
 *
 * \param asmCode       Null-terminated string with source Assembly VTA code.
 * \param insnBuffer    Pre-allocated buffer that will contain the binary VTAGenericInsn instructions.
 * \param maxInsn       Maximum capacity (#elements) for insnBuffer.
 * \param numInsn       Pointer that will contain the total number of binary instructions generated.
 * \param uopBuffer     Pre-allocated buffer that will contain the VTAUop UOPs.
 * \param maxUop        Maximum capacity (#elements) for uopBuffer.
 * \param numUop        Pointer that will contain the total number of UOPs generated.  
 * 
 * \return Status code.
*/
VTAErr vtaAssemble(
    const char      *asmCode,
    VTAGenericInsn  *insnBuffer,
    int             maxInsn,
    int             *numInsn,
    VTAUop          *uopBuffer,
    int             maxUop,
    int             *numUop
);


/*!
 * \brief Disassemble VTA instruction to convert them in text.
 *
 * \param insnBuffer    Buffer of instructions.
 * \param numInsn       How many instructions.
 * \param uopBuffer     Buffer of micro operations.
 * \param numUop        How many micro instructions.
 * \param outBuffer     Output buffer.
 * \param outBufferSize Size of the output buffer.
 * 
 * \return Status code.
*/
VTAErr vtaDisassemble(
    const VTAGenericInsn    *insnBuffer,
    int                     numInsn,
    const VTAUop            *uopBuffer,
    int                     numUop,
    char                    *outBuffer,
    size_t                  outBufferSize
);


/*!
 * \brief Print a string of error based on the status code.
 *
 * \param status    Status code (VTAErr).
 * \param lineNum   Number of line in the code.
*/
void vtaErrorPrint(VTAErr status, int lineNum);


#endif