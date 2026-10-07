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

    if (numUop > 0 && uopBuffer == NULL)
        return VTA_ERR_NULLPTR;

    if (numInsn <= 0)
        return VTA_ERR_INVALID_INSN_SIZE;

    if (numUop < 0)
        return VTA_ERR_INVALID_INSN_SIZE;

    if (outBufferSize == 0)
        return VTA_ERR_INVALID_INSN_SIZE;

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
    labels = (UopLabelTracker *)malloc(
        numInsn * sizeof(UopLabelTracker)
    );

    if (labels == NULL)
        return VTA_ERR_NO_MEM_LABELS;

    /*
     * First step
     *
     * Build UOP label table before the text output.
     * -> labels and uop def appear before insn that use them.
    */
    for (int i = 0; i < numInsn; i++) {
        const VTAGenericInsn *genericInsn;
        genericInsn = &insnBuffer[i];

        if (
            genericInsn->opcode != VTA_OPCODE_ALU   &&
            genericInsn->opcode != VTA_OPCODE_GEMM
        )
            continue;

        uint32_t uopBgn;
        uint32_t uopEnd;

        if (genericInsn->opcode == VTA_OPCODE_ALU) {
            const VTAAluInsn *alu;
            alu = (const VTAAluInsn *)genericInsn;

            uopBgn = alu->uop_bgn;
            uopEnd = alu->uop_end;
        } else {
            const VTAGemInsn *gemm;
            gemm = (const VTAGemInsn *)genericInsn;

            uopBgn = gemm->uop_bgn;
            uopEnd = gemm->uop_end;
        }

        if (
            uopBgn > uopEnd ||
            uopEnd > (uint32_t)numUop ||
            uopEnd > VTA_UOP_BUFF_DEPTH
        ) {
            free(labels);
            return VTA_ERR_UOP_OUT_OF_BOUNDS;
        }

        findOrCreateLabel(
            labels,
            &numLabels,
            uopBgn,
            uopEnd,
            &labelCounter
        );
    }

    if (uopBuffer != NULL) {
        for (uint32_t u = 0; u <= (uint32_t)numUop; u++) {
            for (int i = 0; i < numLabels; i++) {
                if (labels[i].bgn == u) {
                    APPEND(
                        outBuffer, outBufferSize, &offset,
                        "lbl%d_bgn:\n",
                        labels[i].labelID
                    );
                }
            }

            for (int i = 0; i < numLabels; i++) {
                if (labels[i].end == u) {
                    APPEND(
                        outBuffer, outBufferSize, &offset,
                        "lbl%d_end:\n",
                        labels[i].labelID
                    );
                }
            }

            if (u == (uint32_t)numUop)
                break;

            APPEND(
                outBuffer, outBufferSize, &offset,
                "   %u, %u, %u\n",
                uopBuffer[u].dst_idx, uopBuffer[u].src_idx, uopBuffer[u].wgt_idx
            );
        }

        if (numUop > 0)
            APPEND(
                outBuffer, outBufferSize, &offset,
                "\n"
            );
    }

    /*
     * Second step: 
     * 
     * disassemble insn
    */
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
                    APPEND(
                        outBuffer,
                        outBufferSize,
                        &offset,
                        "NOOP\n\n"
                    );
                    break;
                }

                status = printPop(
                    genericInsn,
                    outBuffer,
                    outBufferSize,
                    &offset
                );

                if (status != VTA_OK)
                    goto output_full;

                const char *memoryType;

                switch (mem->memory_type) {
                    case VTA_MEM_ID_UOP:
                        memoryType = "UOP";
                        break;

                    case VTA_MEM_ID_INP:
                        memoryType = "INP";
                        break;

                    case VTA_MEM_ID_WGT:
                        memoryType = "WGT";
                        break;

                    case VTA_MEM_ID_ACC:
                        memoryType = "ACC";
                        break;

                    case VTA_MEM_ID_ACC_8BIT:
                        memoryType = "ACC_8BIT";
                        break;

                    default:
                        free(labels);
                        return VTA_ERR_UNKNOWN_OPCODE;
                }

                if (mem->memory_type == VTA_MEM_ID_UOP) {
                    APPEND(
                        outBuffer,
                        outBufferSize,
                        &offset,
                        "LOAD(%s[%u], MEM[%u, %u])\n",
                        memoryType,
                        mem->sram_base,
                        mem->dram_base,
                        mem->x_size
                    );
                } else {
                    int hasPadding;
                    hasPadding = 
                        mem->y_pad_0 != 0 ||
                        mem->y_pad_1 != 0 ||
                        mem->x_pad_0 != 0 ||
                        mem->x_pad_1 != 0;

                    if (hasPadding) {
                        APPEND(
                            outBuffer, outBufferSize, &offset,
                            "LOAD(%s[%u], MEM[%u, %u, %u, %u]) "
                            "PADDING(%u, %u, %u, %u)\n",
                            memoryType, mem->sram_base, mem->dram_base, mem->y_size, mem->x_size,
                            mem->x_stride, mem->y_pad_0, mem->y_pad_1, mem->x_pad_0, mem->x_pad_1
                        );
                    } else {
                        APPEND(
                            outBuffer, outBufferSize, &offset,
                            "LOAD(%s[%u], MEM[%u, %u, %u, %u])\n",
                            memoryType, mem->sram_base, mem->dram_base, mem->y_size, mem->x_size, mem->x_stride
                        );
                    }
                }

                status = printPush(
                    genericInsn,
                    outBuffer,
                    outBufferSize,
                    &offset
                );

                if (status != VTA_OK)
                    goto output_full;

                APPEND(
                    outBuffer,
                    outBufferSize,
                    &offset,
                    "\n"
                );

                break;
            }

            case VTA_OPCODE_STORE: {
                const VTAMemInsn *mem;
                mem = (const VTAMemInsn *)&insnBuffer[i];

                status = printPop(
                    genericInsn,
                    outBuffer,
                    outBufferSize,
                    &offset
                );

                if (status != VTA_OK)
                    goto output_full;

                int hasPadding;

                hasPadding =
                    mem->y_pad_0 != 0 ||
                    mem->y_pad_1 != 0 ||
                    mem->x_pad_0 != 0 ||
                    mem->x_pad_1 != 0;

                if (hasPadding) {
                    APPEND(
                        outBuffer, outBufferSize, &offset,
                        "STOR(MEM[%u, %u, %u, %u], ACC[%u]) "
                        "PADDING(%u, %u, %u, %u)\n",
                        mem->dram_base, mem->y_size, mem->x_size, mem->x_stride, mem->sram_base,
                        mem->y_pad_0, mem->y_pad_1, mem->x_pad_0, mem->x_pad_1
                    );
                } else {
                    APPEND(
                        outBuffer, outBufferSize, &offset,
                        "STOR(MEM[%u, %u, %u, %u], ACC[%u])\n",
                        mem->dram_base, mem->y_size, mem->x_size, mem->x_stride, mem->sram_base
                    );
                }

                status = printPush(
                    genericInsn,
                    outBuffer,
                    outBufferSize,
                    &offset
                );

                if (status != VTA_OK)
                    goto output_full;

                APPEND(
                    outBuffer,
                    outBufferSize,
                    &offset,
                    "\n"
                );

                break;
            }

            case VTA_OPCODE_ALU: {
                const VTAAluInsn *alu;
                alu = (const VTAAluInsn *)&insnBuffer[i];

                int currentLabel;
                currentLabel = findOrCreateLabel(
                    labels,
                    &numLabels,
                    alu->uop_bgn,
                    alu->uop_end,
                    &labelCounter
                );

                status = printPop(
                    genericInsn,
                    outBuffer,
                    outBufferSize,
                    &offset
                );

                if (status != VTA_OK)
                    goto output_full;

                int hasFactors;
                hasFactors = 
                    alu->dst_factor_out != 0 ||
                    alu->dst_factor_in  != 0 ||
                    alu->src_factor_out != 0 ||
                    alu->src_factor_in  != 0;

                if (hasFactors)
                    APPEND(
                        outBuffer, outBufferSize, &offset,
                        "FOR (%u, %u) UOP (lbl%d_bgn, lbl%d_end) "
                        "FACTORS(%u, %u, %u, %u)\n",
                        alu->iter_out, alu->iter_in, currentLabel, currentLabel,
                        alu->dst_factor_out, alu->dst_factor_in, alu->src_factor_out, alu->src_factor_in
                    );
                else
                    APPEND(
                        outBuffer,
                        outBufferSize,
                        &offset,
                        "FOR (%u, %u) UOP (lbl%d_bgn, lbl%d_end)\n",
                        alu->iter_out, alu->iter_in, currentLabel, currentLabel
                    );

                const char *aluMnemonic;

                switch (alu->alu_opcode) {
                    case VTA_ALU_OPCODE_MIN:
                        aluMnemonic = "ALU.MIN";
                        break;

                    case VTA_ALU_OPCODE_MAX:
                        aluMnemonic = "ALU.MAX";
                        break;

                    case VTA_ALU_OPCODE_ADD:
                        aluMnemonic = "ALU.ADD";
                        break;

                    case VTA_ALU_OPCODE_SHR:
                        aluMnemonic = "ALU.SHR";
                        break;

                    case VTA_ALU_OPCODE_MUL:
                        aluMnemonic = "ALU.MUL";
                        break;

                    default:
                        free(labels);
                        return VTA_ERR_UNKNOWN_OPCODE;
                }

                if (alu->use_imm) {
                    APPEND(
                        outBuffer,
                        outBufferSize,
                        &offset,
                        "%s(DST[%u:%u], %d)\n",
                        aluMnemonic,
                        alu->uop_bgn,
                        alu->uop_end,
                        alu->imm
                    );
                } else {
                    APPEND(
                        outBuffer,
                        outBufferSize,
                        &offset,
                        "%s(DST[%u:%u], SRC[%u:%u])\n",
                        aluMnemonic,
                        alu->uop_bgn,
                        alu->uop_end,
                        alu->uop_bgn,
                        alu->uop_end
                    );
                }

                status = printPush(
                    genericInsn,
                    outBuffer,
                    outBufferSize,
                    &offset
                );

                if (status != VTA_OK)
                    goto output_full;

                APPEND(
                    outBuffer,
                    outBufferSize,
                    &offset,
                    "\n"
                );

                break;
            }

            case VTA_OPCODE_GEMM: {
                const VTAGemInsn *gemm;
                gemm = (const VTAGemInsn *)&insnBuffer[i];

                int currentLabel;
                currentLabel = findOrCreateLabel(
                    labels,
                    &numLabels,
                    gemm->uop_bgn,
                    gemm->uop_end,
                    &labelCounter
                );

                status = printPop(
                    genericInsn,
                    outBuffer,
                    outBufferSize,
                    &offset
                );

                if (status != VTA_OK)
                    goto output_full;

                int hasFactors;
                hasFactors = 
                    gemm->dst_factor_out != 0 ||
                    gemm->dst_factor_in  != 0 ||
                    gemm->src_factor_out != 0 ||
                    gemm->src_factor_in  != 0 ||
                    gemm->wgt_factor_out != 0 ||
                    gemm->wgt_factor_in  != 0;
                
                if (hasFactors) 
                    APPEND(
                        outBuffer, outBufferSize, &offset,
                        "FOR (%u, %u) UOP (lbl%d_bgn, lbl%d_end) "
                        "FACTORS(%u, %u, %u, %u, %u, %u)\n",
                        gemm->iter_out, gemm->iter_in, currentLabel, currentLabel,
                        gemm->dst_factor_out, gemm->dst_factor_in, 
                        gemm->src_factor_out, gemm->src_factor_in, 
                        gemm->wgt_factor_out, gemm->wgt_factor_in
                    );
                else
                    APPEND(
                        outBuffer, outBufferSize, &offset,
                        "FOR (%u, %u) UOP (lbl%d_bgn, lbl%d_end)\n",
                        gemm->iter_out, gemm->iter_in, currentLabel, currentLabel
                    );

                if (gemm->reset_reg) {
                    APPEND(
                        outBuffer,
                        outBufferSize,
                        &offset,
                        "GEMM.RST(ACC[%u:%u])\n",
                        gemm->uop_bgn,
                        gemm->uop_end
                    );
                } else {
                    APPEND(
                        outBuffer,
                        outBufferSize,
                        &offset,
                        "GEMM(ACC[%u:%u], INP[%u:%u], WGT[%u:%u])\n",
                        gemm->uop_bgn,
                        gemm->uop_end,
                        gemm->uop_bgn,
                        gemm->uop_end,
                        gemm->uop_bgn,
                        gemm->uop_end
                    );
                }

                status = printPush(
                    genericInsn,
                    outBuffer,
                    outBufferSize,
                    &offset
                );

                if (status != VTA_OK)
                    goto output_full;

                APPEND(
                    outBuffer,
                    outBufferSize,
                    &offset,
                    "\n"
                );

                break;
            }

            case VTA_OPCODE_FINISH: {
                APPEND(
                    outBuffer,
                    outBufferSize,
                    &offset,
                    "FINISH\n\n"
                );

                break;
            }

            default: {
                free(labels);
                return VTA_ERR_UNKNOWN_OPCODE;
            }
        }
    }

    free(labels);
    return VTA_OK;

output_full:
    free(labels);
    return status;
}