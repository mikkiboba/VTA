#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <vta/hw_spec.h>

#include "vta.h"


/*! 
* \brief Temporary memory to keep track of which UOP has a label.
*/
typedef struct {
    uint32_t bgn;
    uint32_t end;
    int labelID;
} UopLabelTracker;


#define APPEND(outBuffer, outBufferSize, offsetPtr, ...)    \
    do {                                                    \
        status = appendArgument(                              \
            (outBuffer),                                    \
            (outBufferSize),                                \
            (offsetPtr),                                    \
            __VA_ARGS__                                     \
        );                                                  \
        if (status != VTA_OK)                               \
            goto output_full;                               \
    } while (0)                                             \


/*!
 * \brief Append formatted text to the output buffer.
 *
 * \param outBuffer     Buffer for the text output.
 * \param outBufferSize Size related to outBuffer.
 * \param offset 
 * \param format        Text to append
*/
static VTAErr appendArgument(
    char        *outBuffer,
    size_t      outBufferSize,
    size_t      *offset,
    const char  *format,
    ...
) {
    if (outBuffer == NULL || offset == NULL || format == NULL) 
        return VTA_ERR_NULLPTR;

    if (outBufferSize == 0 || *offset >= outBufferSize)
        return VTA_ERR_OUTPUT_BUFFER_FULL;

    size_t remaining;
    remaining = outBufferSize - *offset;

    va_list args;
    va_start(args, format);

    int written;
    // * vsnprintf returns the number of characters that would have been written (w/o "\0")
    // * if written >= remaining => the output has been truncated
    written = vsnprintf(outBuffer + *offset, remaining, format, args);

    va_end(args);

    if (written < 0)
        return VTA_ERR_OUTPUT_BUFFER_FULL;

    if ((size_t)written >= remaining) {
        *offset = outBufferSize - 1;
        outBuffer[*offset] = '\0';
        
        return VTA_ERR_OUTPUT_BUFFER_FULL;
    }

    *offset += (size_t)written;

    return VTA_OK;
}


/*!
 * \brief Get the right label for the operation. If it doesn't exist, create it.
 *
 * \param labels        Tracker of labels.
 * \param numLabels     Amount of existing labels.
 * \param bgn           Where the label begins.
 * \param end           Where the label ends.
 * \param labelCounter  Counter for labels.
*/
static int findOrCreateLabel(
    UopLabelTracker *labels, 
    int             *numLabels, 
    uint32_t        bgn, 
    uint32_t        end, 
    int             *labelCounter
) {
    for (int i = 0; i < *numLabels; i++) {
        if (labels[i].bgn == bgn && labels[i].end == end)
            return labels[i].labelID;
    }

    int newID;
    newID = (*labelCounter)++;

    labels[*numLabels].bgn      = bgn;
    labels[*numLabels].end      = end;
    labels[*numLabels].labelID  = newID;

    (*numLabels)++;
    return newID;
}


/*!
 * \brief Manage the print for the dependencies POP operation.
 *
 * \param insn          Generic instruction.
 * \param outBuffer     Output buffer for the text to print.
 * \param outBufferSize Size of the output buffer.
 * \param offset        Offset for the text.
*/
static VTAErr printPop(
    const VTAGenericInsn    *insn,
    char                    *outBuffer,
    size_t                  outBufferSize,
    size_t                  *offset
) {
    VTAErr status; 
    status = VTA_OK;

    if (!insn->pop_prev_dep && !insn->pop_next_dep)
        return VTA_OK;

    APPEND(outBuffer, outBufferSize, offset, "POP (");

    int printed;
    printed = 0;

    switch (insn->opcode) {
        case VTA_OPCODE_LOAD:
            if (insn->pop_prev_dep || insn->pop_next_dep)
                APPEND(outBuffer, outBufferSize, offset, "EX->LD");
            break;
        case VTA_OPCODE_STORE:
            if (insn->pop_prev_dep || insn->pop_next_dep)
                APPEND(outBuffer, outBufferSize, offset, "EX->ST");
            break;
        case VTA_OPCODE_ALU:
        case VTA_OPCODE_GEMM:
            if (insn->pop_prev_dep) {
                APPEND(outBuffer, outBufferSize, offset, "LD->EX");
                printed = 1;
            }
            if (insn->pop_next_dep) {
                if (printed) APPEND(outBuffer, outBufferSize, offset, ", ");
                APPEND(outBuffer, outBufferSize, offset, "ST->EX");
            }
            break;
    }
    APPEND(outBuffer, outBufferSize, offset, ")\n");

    return VTA_OK;

output_full:
    return status;
}


/*!
 * \brief Manage the print for the dependencies PUSH operation.
 *
 * \param insn          Generic instruction.
 * \param outBuffer     Output buffer for the text to print.
 * \param outBufferSize Size of the output buffer.
 * \param offset        Offset for the text.
*/
static VTAErr printPush(
    const VTAGenericInsn    *insn, 
    char                    *outBuffer, 
    size_t                  outBufferSize,
    size_t                  *offset 
) {
    VTAErr status;
    status = VTA_OK;


    if (!insn->push_prev_dep && !insn->push_next_dep) 
        return VTA_OK;

    APPEND(outBuffer, outBufferSize, offset, "PUSH (");

    int printed;
    printed = 0;

    switch (insn->opcode) {
        case VTA_OPCODE_LOAD:
            if (insn->push_prev_dep || insn->push_next_dep)
                APPEND(outBuffer, outBufferSize, offset, "LD->EX");
            break;
        case VTA_OPCODE_STORE:
            if (insn->push_prev_dep || insn->push_next_dep)
                APPEND(outBuffer, outBufferSize, offset, "ST->EX");
            break;
        case VTA_OPCODE_ALU:
        case VTA_OPCODE_GEMM:
            if (insn->push_prev_dep) {
                APPEND(outBuffer, outBufferSize, offset, "EX->LD");
                printed = 1;
            }
            if (insn->push_next_dep) {
                if (printed) APPEND(outBuffer, outBufferSize, offset, ", ");
                APPEND(outBuffer, outBufferSize, offset, "EX->ST");
            }
            break;
    }
    APPEND(outBuffer, outBufferSize, offset, ")\n");

    return VTA_OK;

output_full:
    return status;
}


VTAErr vtaDisassemble(
    const VTAGenericInsn    *insnBuffer,
    int                     numInsn,
    const VTAUop            *uopBuffer,
    int                     numUop,
    char                    *outBuffer,
    size_t                  outBufferSize
) {
    if (insnBuffer == NULL || outBuffer == NULL)
        return VTA_ERR_NULLPTR;
    if (numInsn <= 0) 
        return VTA_ERR_INVALID_INSN_SIZE;
    if (numUop < 0)
        return VTA_ERR_INVALID_INSN_SIZE; // * placeholder
    if (outBufferSize == 0)
        return VTA_ERR_INVALID_INSN_SIZE; // * placeholder

    outBuffer[0] = '\0';
    size_t offset;
    offset = 0;

    VTAErr status;
    status = VTA_OK;

    int labelCounter;
    labelCounter = 1;

    int numLabels;
    numLabels = 0;

    UopLabelTracker *labels;
    labels = (UopLabelTracker *)malloc(numInsn * sizeof(UopLabelTracker));
    if (labels == NULL) 
        return VTA_ERR_NO_MEM_LABELS;

    // * first step: disassemble each insn and build the table
    for (int i = 0; i < numInsn; i++) {
        const VTAGenericInsn *genericInsn;
        genericInsn = &insnBuffer[i];
        
        int opcode;
        opcode = genericInsn->opcode;
        switch (opcode) {
            case VTA_OPCODE_LOAD: {
                const VTAMemInsn *mem;
                mem = (const VTAMemInsn *)&insnBuffer[i];

                if (mem->x_size == 0) {
                    APPEND(outBuffer, outBufferSize, &offset, "NOOP\n\n");
                    break;
                }

                status = printPop(genericInsn, outBuffer, outBufferSize, &offset);
                if (status != VTA_OK)
                    goto output_full;

                if (mem->memory_type == VTA_MEM_ID_UOP) 
                    APPEND(
                        outBuffer, outBufferSize, &offset,
                        "LOAD(BUF[%u], MEM[%u, %u])\n", 
                        mem->sram_base, mem->dram_base, mem->x_size
                    );
                else
                    APPEND(
                        outBuffer, outBufferSize, &offset,
                        "LOAD(BUF[%u], MEM[%u, %u, %u, %u])\n",
                        mem->sram_base, mem->dram_base, mem->y_size, mem->x_size, mem->x_stride
                    );
                
                status = printPush(genericInsn, outBuffer, outBufferSize, &offset);
                if (status != VTA_OK)
                    goto output_full;

                APPEND(outBuffer, outBufferSize, &offset, "\n");
                break;
            }
            case VTA_OPCODE_STORE: {
                const VTAMemInsn *mem;
                mem = (const VTAMemInsn *)&insnBuffer[i];
                
                status = printPop(genericInsn, outBuffer, outBufferSize, &offset);
                if (status != VTA_OK)
                    goto output_full;

                APPEND(
                    outBuffer, outBufferSize, &offset,
                    "STOR(MEM[%u, %u, %u, %u], ACC[%u])\n",
                    mem->dram_base, mem->y_size, mem->x_size, mem->x_stride, mem->sram_base
                );

                status = printPush(genericInsn, outBuffer, outBufferSize, &offset);
                if (status != VTA_OK)
                    goto output_full;

                APPEND(outBuffer, outBufferSize, &offset, "\n");
                break;
            }
            case VTA_OPCODE_ALU: {
                const VTAAluInsn *alu;
                alu = (const VTAAluInsn *)&insnBuffer[i];

                if (
                    alu->uop_bgn > alu->uop_end     || 
                    alu->uop_end > (uint32_t)numUop ||
                    alu->uop_end > VTA_UOP_BUFF_DEPTH
                ) {
                    free(labels);
                    return VTA_ERR_UOP_OUT_OF_BOUNDS;
                }

                int currentLabel;
                currentLabel = findOrCreateLabel(labels, &numLabels, alu->uop_bgn, alu->uop_end, &labelCounter);

                status = printPop(genericInsn, outBuffer, outBufferSize, &offset);
                if (status != VTA_OK)
                    goto output_full;
                
                APPEND(
                    outBuffer, outBufferSize, &offset,
                    "FOR (%u, %u) UOP (lbl%d_bgn, lbl%d_end)\n",
                    alu->iter_out, alu->iter_in, currentLabel, currentLabel
                );

                if (alu->use_imm)
                    APPEND(
                        outBuffer, outBufferSize, &offset,
                        "ALU.OP(DST[%u:%u], %d)\n",
                        alu->uop_bgn, alu->uop_end, alu->imm
                    );
                else
                    APPEND(
                        outBuffer, outBufferSize, &offset,
                        "ALU.OP(DST[%u:%u], SRC[%u:%u])\n",
                        alu->uop_bgn, alu->uop_end, alu->uop_bgn, alu->uop_end
                    );

                status = printPush(genericInsn, outBuffer, outBufferSize, &offset);
                if (status != VTA_OK)
                    goto output_full;

                APPEND(outBuffer, outBufferSize, &offset, "\n");
                break;
            }
            case VTA_OPCODE_GEMM: {
                const VTAGemInsn *gemm;
                gemm = (const VTAGemInsn *)&insnBuffer[i];

                if (
                    gemm->uop_bgn > gemm->uop_end       ||
                    gemm->uop_end > (uint32_t)numUop    ||
                    gemm->uop_end > VTA_UOP_BUFF_DEPTH
                ) {
                    free(labels);
                    return VTA_ERR_UOP_OUT_OF_BOUNDS;
                }

                int currentLabel;
                currentLabel = findOrCreateLabel(labels, &numLabels, gemm->uop_bgn, gemm->uop_end, &labelCounter);

                status = printPop(genericInsn, outBuffer, outBufferSize, &offset);
                if (status != VTA_OK) 
                    goto output_full;

                APPEND(
                    outBuffer, outBufferSize, &offset,
                    "FOR (%u, %u) UOP (lbl%d_bgn, lbl%d_end)\n",
                    gemm->iter_out, gemm->iter_in, currentLabel, currentLabel
                );

                if (gemm->reset_reg)
                    APPEND(
                        outBuffer, outBufferSize, &offset,
                        "GEMM.RST(ACC[%u:%u])\n",
                        gemm->uop_bgn, gemm->uop_end
                    );
                else
                    APPEND(
                        outBuffer, outBufferSize, &offset,
                        "GEMM(ACC[%u:%u], INP[%u:%u], WGT[%u:%u])\n",
                        gemm->uop_bgn, gemm->uop_end, gemm->uop_bgn, gemm->uop_end, gemm->uop_bgn, gemm->uop_end
                    );

                status = printPush(genericInsn, outBuffer, outBufferSize, &offset);
                if (status != VTA_OK)
                    goto output_full;

                APPEND(outBuffer, outBufferSize, &offset, "\n");
                break;
            }
            case VTA_OPCODE_FINISH: {
                APPEND(outBuffer, outBufferSize, &offset, "FINISH\n\n");
                break;
            }
            default: {
                free(labels);
                return VTA_ERR_UNKNOWN_OPCODE;
            }
        }
    }

    if (numLabels > 0 && uopBuffer != NULL) {
        for (int i = 0; i < numLabels; ++i) {
            uint32_t bgn, end;
            bgn = labels[i].bgn;
            end = labels[i].end;

            int label;
            label = labels[i].labelID;

            APPEND(outBuffer, outBufferSize, &offset, "lbl%d_bgn:\n", label);

            for (uint32_t u = bgn; u < end; ++u) {
                if (u < (uint32_t)numUop)
                    APPEND(
                        outBuffer, outBufferSize, &offset,
                        "   %u, %u, %u\n",
                        uopBuffer[u].dst_idx, uopBuffer[u].src_idx, uopBuffer[u].wgt_idx
                    );
            }

            APPEND(outBuffer, outBufferSize, &offset, "lbl%d_end:\n----\n", label);
        }
    }

    free(labels);
    return VTA_OK;

output_full:
    free(labels);
    return status;
}