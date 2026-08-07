#ifndef ASSEMBLER_H
#define ASSEMBLER_H


#include <stdint.h>
#include <stddef.h>

#include <vta/hw_spec.h>



/*! 
* \brief STATUS codes for VTA operations (Assembler and Disassembler)
* \note VTA_OK = 0 guarantees success.
*/
typedef enum {
    VTA_OK = 0,                     /*!< Operations successfully completed */
    VTA_ERR_NULLPTR,                /*< Passed a NULL pointer */
    VTA_ERR_INVALID_INSN_SIZE,      /*< Invalid buffer dimension or number of instructions */
    VTA_ERR_NO_MEM_LABELS,          /*< Memory error while checking on the labels */
    VTA_ERR_INVALID_OPCODE,         /*< Opcode unknown or not valid */
    VTA_ERR_UOP_OOB,                /*< UOP indices out of bounds */

    VTA_ERR_INSN_BUFFER_FULL,
    VTA_ERR_UOP_BUFFER_FULL,
    VTA_ERR_SYNTAX,
    VTA_ERR_UNKNOWN_MNEMONIC,
    VTA_ERR_LABEL_NOT_FOUND,
    VTA_ERR_OUT_OF_RANGE,
} VTAErr;


/*!
 * \brief Assemble the VTA source code in binary instructions VTA and UOP.
 *
 * \param asmCode Null-terminated string with source Assembly VTA code.
 * \param insnBuffer Pre-allocated buffer that will contain the binary VTAGenericInsn instructions.
 * \param maxInsn Maximum capacity (#elements) for insnBuffer.
 * \param numInsn Pointer that will contain the total number of binary instructions generated.
 * \param uopBuffer Pre-allocated buffer that will contain the VTAUop UOPs.
 * \param maxUop Maximum capacity (#elements) for uopBuffer.
 * \param numUop Pointer that will contain the total number of UOPs generated.  
*/
VTAErr assemble(
    const char* asmCode,
    VTAGenericInsn* insnBuffer,
    int maxInsn,
    int* numInsn,
    VTAUop* uopBuffer,
    int maxUop,
    int* numUop
);


/*!
 * \brief Helper function to print assembler's errors on stderr.
 * \param status Status code (VTAErr) returned from VTA_assemble().
 * \param lineNumber Line count of the source where the error happened (-1 if not applicable).
*/
void assemble_errorPrint(VTAErr status, int lineNumber);


#endif