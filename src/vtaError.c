#include <stdio.h>

#include "vta.h"



void vtaErrorPrint(VTAErr status, int lineNum) {
    if (status == VTA_OK) return;

    if (lineNum > 0)
        fprintf(stderr, "[VTA_ERR] Row %d: ", lineNum);
    else
        fprintf(stderr, "[VTA_ERR]: ");

    switch (status) {
        case VTA_ERR_NULLPTR:
            fprintf(stderr, "NULL pointer passed as argument.\n");
            break;
        case VTA_ERR_INVALID_INSN_SIZE:
            fprintf(stderr, "Buffer size not valid or <= 0.\n");
            break;
        case VTA_ERR_NO_MEM_LABELS:
            fprintf(stderr, "Failed to allocate memory for labels.\n");
            break;
        case VTA_ERR_UNKNOWN_OPCODE:
            fprintf(stderr, "Unknown opcode in binary.\n");
            break;
        case VTA_ERR_UOP_OUT_OF_BOUNDS:
            fprintf(stderr, "Micro operation index out of bounds.\n");
            break;
        case VTA_ERR_OUTPUT_BUFFER_FULL:
            fprintf(stderr, "The output buffer is full.\n");
            break;
        case VTA_ERR_INSN_BUFFER_FULL:
            fprintf(stderr, "The instruction buffer is full.\n");
            break;
        case VTA_ERR_UOP_BUFFER_FULL:
            fprintf(stderr, "The UOP buffer is full.\n");
            break;
        case VTA_ERR_SYNTAX:
            fprintf(stderr, "Syntax error. Unexpected token or invalid format.\n");
            break;
        case VTA_ERR_UNKNOWN_MNEMONIC:
            fprintf(stderr, "Unknown instruction or register mnemonic.\n");
            break;
        case VTA_ERR_LABEL_NOT_FOUND:
            fprintf(stderr, "Label reference not found.\n");
            break;
        case VTA_ERR_OUT_OF_RANGE:
            fprintf(stderr, "Numeric value outside of range.\n");
            break;
        default:
            fprintf(stderr, "Unknown VTAErr code (%d).\n", status);
            break;
    }
}