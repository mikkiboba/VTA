#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <vta/hw_spec.h>


typedef struct {
    uint32_t bgn;
    uint32_t end;
    int labelID;
} UopLabelTracker;

/*! \brief Helper function to print POP (stage1->stage2)
    \param insn Instruction to work on
*/
void printPop(const VTAGenericInsn* insn) {
    if (!insn->pop_prev_dep && !insn->pop_next_dep) return;

    printf("POP (");

    int printed;
    printed = 0;

    switch(insn->opcode) {
        case VTA_OPCODE_LOAD:
            if (insn->pop_prev_dep || insn->pop_next_dep)
                printf("EX->LD");
            break;
        case VTA_OPCODE_STORE:
            if (insn->pop_prev_dep || insn->pop_next_dep)
                printf("EX->ST");
            break;
        case VTA_OPCODE_ALU:
        case VTA_OPCODE_GEMM:
            if (insn->pop_prev_dep) {
                printf("LD->EX");
                printed = 1;
            }
            if (insn->pop_next_dep) {
                if (printed) printf(", ");
                printf("ST->EX");
            }
            break;
    }
    printf(")\n");
}


/*! \brief Helper function to print PUSH (stage1->stage2)
    \param insn Instruction to work on
*/
void printPush(const VTAGenericInsn* insn) {
    if (!insn->push_prev_dep && !insn->push_next_dep) return;

    printf("PUSH (");

    int printed;
    printed = 0;

    switch(insn->opcode) {
        case VTA_OPCODE_LOAD:
            if (insn->push_prev_dep || insn->push_next_dep)
                printf("LD->EX");
            break;
        case VTA_OPCODE_STORE:
            if (insn->push_prev_dep || insn->push_next_dep)
                printf("ST->EX");
            break;
        case VTA_OPCODE_ALU:
        case VTA_OPCODE_GEMM:
            if (insn->push_prev_dep) {
                printf("EX->LD");
                printed = 1;
            }
            if (insn->push_next_dep) {
                if (printed) printf(", ");
                printf("EX->ST");
            }
            break;
    }
    printf(")\n");
}


void disassemble(const VTAGenericInsn* insnBuffer, int numInsn, const VTAUop* uopBuffer) {
    int labelCounter;
    labelCounter = 1;

    UopLabelTracker* labels;
    labels = (UopLabelTracker*)malloc(numInsn * sizeof(UopLabelTracker));
    if (!labels) {
        fprintf(stderr, "No memory to allocate for labels.");
        return;
    }
    
    int numLabels;
    numLabels = 0;

    for (int i = 0; i < numInsn; ++i) {
        const VTAGenericInsn* genInsn;
        genInsn = (const VTAGenericInsn*)&insnBuffer[i];

        switch (genInsn->opcode) {
            case VTA_OPCODE_LOAD: {
                const VTAMemInsn* mem;
                mem = (const VTAMemInsn*)&insnBuffer[i]; 

                if (mem->x_size == 0) {
                    printf("NOOP\n\n");
                    break;
                }

                printPop(genInsn);

                if (mem->x_pad_0 || mem->x_pad_1 || mem->y_pad_0 || mem->y_pad_1)
                    printf("PAD (%u, %u, %u, %u)\n", 
                        mem->x_pad_0, mem->x_pad_1, mem->y_pad_0, mem->y_pad_1);

                if (mem->memory_type == VTA_MEM_ID_UOP)
                    printf("LOAD(UOP[%u], MEM[%u, %u])\n", 
                        mem->sram_base, mem->dram_base, mem->x_size);
                else 
                    printf("LOAD(BUF[%u], MEM[%u, %u, %u, %u])\n", 
                        mem->sram_base, mem->dram_base, mem->y_size, mem->x_size, mem->x_stride);
            
                printPush(genInsn);
                printf("\n");
                break;
            }
            case VTA_OPCODE_STORE: {
                const VTAMemInsn* mem;
                mem = (const VTAMemInsn*)&insnBuffer[i]; 

                printPop(genInsn);

                printf("STOR(MEM[%u, %u, %u, %u], ACC[%u])\n", 
                    mem->dram_base, mem->y_size, mem->x_size, mem->x_stride, mem->sram_base);
                
                printPush(genInsn);
                printf("\n");
                break;
            }
            case VTA_OPCODE_ALU: {
                const VTAAluInsn* alu;
                alu = (const VTAAluInsn*)&insnBuffer[i];

                int currLabel;
                currLabel = labelCounter++;

                labels[numLabels].bgn       = alu->uop_bgn;
                labels[numLabels].end       = alu->uop_end;
                labels[numLabels].labelID   = currLabel;
                numLabels++;

                printPop(genInsn);

                printf("FOR (%u, %u) UOP (lbl%d_bgn, lbl%d_end)\n", 
                    alu->iter_out, alu->iter_in, currLabel, currLabel);
                
                if (alu->use_imm)
                    printf("ALU.OP(DST[%u:%u], %d)\n", alu->uop_bgn, alu->uop_end, alu->imm);
                else
                    printf("ALU.OP(DST[%u:%u], SRC[%u:%u])\n", 
                        alu->uop_bgn, alu->uop_end, alu->uop_bgn, alu->uop_end);

                printPush(genInsn);
                printf("\n");
                break;
            }
            case VTA_OPCODE_GEMM: {
                const VTAGemInsn* gemm;
                gemm = (const VTAGemInsn*)&insnBuffer[i];

                int currLabel;
                currLabel = labelCounter++;

                labels[numLabels].bgn       = gemm->uop_bgn;
                labels[numLabels].end       = gemm->uop_end;
                labels[numLabels].labelID   = currLabel;
                numLabels++;

                printPop(genInsn);

                printf("FOR (%u, %u) UOP (lbl%d_bgn, lbl%d_end)\n", 
                    gemm->iter_out, gemm->iter_in, currLabel, currLabel);

                if (gemm->reset_reg)
                    printf("GEMM.RST(ACC[%u:%u])\n", gemm->uop_bgn, gemm->uop_end);
                else
                    printf("GEMM(ACC[%u:%u], INP[%u:%u], WGT[%u:%u])\n", 
                        gemm->uop_bgn, gemm->uop_end, gemm->uop_bgn, gemm->uop_end, gemm->uop_bgn, gemm->uop_end);
            
                printPush(genInsn);
                printf("\n");
                break;
            }
            case VTA_OPCODE_FINISH:
                printf("FINISH\n\n");
                break;
            
            default: {
                printf("UNK (opcode: %u)\n\n", genInsn->opcode);
                break;
            }
        }
    }

    if (numLabels > 0 && uopBuffer != NULL) {
        for (int i = 0; i < numLabels; ++i) {
            uint32_t bgn, end;
            bgn    = labels[i].bgn;
            end    = labels[i].end;
            
            int label;
            label = labels[i].labelID;

            printf("lbl%d_bgn\n", label);
            for(uint32_t u = bgn; u < end; ++u)
                printf("\t%u,%u,%u\n", 
                    uopBuffer[u].dst_idx, uopBuffer[u].src_idx, uopBuffer[u].wgt_idx);
            
            printf("lbl%d_end\n----\n", label);
        }
    }

    free(labels);
}