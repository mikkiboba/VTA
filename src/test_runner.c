#include <stdio.h>
#include <string.h>
#include <vta/hw_spec.h>

#include "vta.h"

#define OUT_BUF_SIZE 2048


static void printHeader(const char *testName) {
    printf("\n----\n");
    printf(" Stress test: %s", testName);
    printf("\n----\n");
}


static void checkStatus(VTAErr status, VTAErr expectedStatus, int *errorCount) {
    if (status == expectedStatus) {
        if (status != VTA_OK) vtaErrorPrint(status, -1);
        printf(" PASS! Crashes avoided and correct error returned.\n\n");
    } else {
        printf(" FAIL! Expected status: [%d] but got [%d].\n\n", expectedStatus, status);
        (*errorCount)++;
    }
}


static int test_nullPointers(void) {
    printHeader("1. Check on NULL pointers and numInsn <= 0.");

    int error;
    error = 0;

    VTAErr status;
    
    char outBuffer[OUT_BUF_SIZE];

    printf(" 1.1 - Passing insnBuffer = NULL, numInsn = 5\n");
    status = vtaDisassemble(NULL, 5, NULL, 0, outBuffer, sizeof(outBuffer));
    checkStatus(status, VTA_ERR_NULLPTR, &error);

    printf(" 1.2 - Passing numInsn = 0\n");
    VTAGenericInsn testInsn;
    memset(&testInsn, 0, sizeof(testInsn));
    status = vtaDisassemble(&testInsn, 0, NULL, 0, outBuffer, sizeof(outBuffer));
    checkStatus(status, VTA_ERR_INVALID_INSN_SIZE, &error);

    printf(" 1.3 - Passing numInsn = -5\n");
    status = vtaDisassemble(&testInsn, -5, NULL, 0, outBuffer, sizeof(outBuffer));
    checkStatus(status, VTA_ERR_INVALID_INSN_SIZE, &error);

    return error;
}


static int test_invalidOPCode(void) {
    printHeader("2. Check on invalid/unknown OPCodes.");

    int error;
    error = 0;

    VTAErr status;

    char outBuffer[OUT_BUF_SIZE];

    VTAGenericInsn testInsns;
    memset(&testInsns, 0, sizeof(testInsns));
    testInsns.opcode = 7;

    printf(" 2.1 - Passing instruction with unknown opcode (%d)\n", testInsns.opcode);
    status = vtaDisassemble(&testInsns, 1, NULL, 0, outBuffer, sizeof(outBuffer));
    checkStatus(status, VTA_ERR_UNKNOWN_OPCODE, &error);

    return error;
}


static int test_uopOutOfBounds(void) {
    printHeader("3. Check on if UOP indices are out of bounds.");

    int error;
    error = 0;

    VTAErr status;
    
    char outBuffer[OUT_BUF_SIZE];

    VTAGemInsn testGemmInsn;
    memset(&testGemmInsn, 0, sizeof(testGemmInsn));

    VTAGenericInsn *insn;
    insn = (VTAGenericInsn *)&testGemmInsn;

    insn->opcode = VTA_OPCODE_GEMM;
    testGemmInsn.uop_bgn = 0;
    testGemmInsn.uop_end = VTA_UOP_BUFF_DEPTH + 1;

    printf(" 3.1 - Passing GEMM instruction with uop_end (%u) > VTA_UOP_BUFF_DEPTH (%u)\n", (unsigned)testGemmInsn.uop_end, (unsigned)VTA_UOP_BUFF_DEPTH);
    status = vtaDisassemble(insn, 1, NULL, VTA_UOP_BUFF_DEPTH, outBuffer, sizeof(outBuffer));
    checkStatus(status, VTA_ERR_UOP_OUT_OF_BOUNDS, &error);

    testGemmInsn.uop_bgn = 10;
    testGemmInsn.uop_end = 9;

    printf(" 3.2 - Passing GEMM instruction with uop_bgn (%u) > uop_end (%u)\n", (unsigned)testGemmInsn.uop_bgn, (unsigned)testGemmInsn.uop_end);
    status = vtaDisassemble(insn, 1, NULL, VTA_UOP_BUFF_DEPTH, outBuffer, sizeof(outBuffer));
    checkStatus(status, VTA_ERR_UOP_OUT_OF_BOUNDS, &error);

    return error;
}


static int test_nullUopBuffer(void) {
    printHeader("4. Check on GEMM/ALU instructions with uopBuffer = NULL.");

    int error;
    error = 0;

    VTAErr status;

    char outBuffer[OUT_BUF_SIZE];


    VTAGemInsn testGemmInsn;
    memset(&testGemmInsn, 0, sizeof(testGemmInsn));

    VTAGenericInsn* insn;
    insn = (VTAGenericInsn*)&testGemmInsn;

    insn->opcode = VTA_OPCODE_GEMM;
    testGemmInsn.uop_bgn    = 0;
    testGemmInsn.uop_end    = 4;
    testGemmInsn.iter_out   = 1;
    testGemmInsn.iter_in    = 1;

    printf(" 4.1 - Disassembling GEMM with uopBuffer = NULL\n");
    status = vtaDisassemble(insn, 1, NULL, 5, outBuffer, sizeof(outBuffer));
    checkStatus(status, VTA_OK, &error);

    return error;
}


// * this was the content in the old main + insnMaker
static void memInsn( 
    VTAGenericInsn *insn_queue, int insn_idx, int opcode, int mem_type, 
    int sram_base, uint32_t dram_base, int x_size, int y_size, int x_stride,
    int pop_prev, int pop_next, int push_prev, int push_next
) {
    VTAMemInsn *insn = (VTAMemInsn *)&insn_queue[insn_idx];
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

static void gemmInsn(
    VTAGenericInsn *insn_queue, int insn_idx, int opcode, int reset_reg, 
    int uop_bgn, int uop_end, int iter_out, int iter_in, int dst_factor, 
    int src_factor, int wgt_factor, int pop_prev, int pop_next, 
    int push_prev, int push_next
){
    VTAGemInsn *insn = (VTAGemInsn *)&insn_queue[insn_idx];
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


static int test_labelReuse(void) {
    printHeader("5. Check on Label Generation and Reuse (from main.c)");
    int error = 0;

    VTAGenericInsn insn_stream[5];
    VTAUop uop_stream[5];
    char out_buf[OUT_BUF_SIZE];

    memset(insn_stream, 0, sizeof(insn_stream));
    memset(uop_stream, 0, sizeof(uop_stream));

    uop_stream[0].dst_idx = 10; uop_stream[0].src_idx = 20; uop_stream[0].wgt_idx = 30;
    uop_stream[1].dst_idx = 40; uop_stream[1].src_idx = 50; uop_stream[1].wgt_idx = 60;
    uop_stream[2].dst_idx = 70; uop_stream[2].src_idx = 80; uop_stream[2].wgt_idx = 90;
    uop_stream[3].dst_idx = 11; uop_stream[3].src_idx = 22; uop_stream[3].wgt_idx = 33;
    uop_stream[4].dst_idx = 44; uop_stream[4].src_idx = 55; uop_stream[4].wgt_idx = 66;

    // * 5.1 gemm on uops [0:2] -> generates lbl1
    gemmInsn(insn_stream, 0, VTA_OPCODE_GEMM, 0, 0, 2, 4, 2, 0, 0, 0, 1, 1, 1, 1);
    // * 5.2 gemm on uops[0:2] -> reuses lbl1
    gemmInsn(insn_stream, 1, VTA_OPCODE_GEMM, 0, 0, 2, 4, 2, 0, 0, 0, 1, 1, 1, 1);
    // * 5.3 gemm on uops [1:4] -> generates lbl2
    gemmInsn(insn_stream, 2, VTA_OPCODE_GEMM, 0, 1, 4, 4, 2, 0, 0, 0, 1, 1, 1, 1);
    
    insn_stream[3].opcode = VTA_OPCODE_FINISH;
    memInsn(insn_stream, 4, VTA_OPCODE_LOAD, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0);

    VTAErr status;
    status = vtaDisassemble(insn_stream, 5, uop_stream, 5, out_buf, sizeof(out_buf));    
    if (status != VTA_OK) {
        printf(" FAIL! Disassembler returned error %d\n", status);
        error++;
    } else {
        printf(" PASS! Disassembly successful. Output:\n\n%s\n", out_buf);
    }

    return error;
}


int main(void) {
    int error;
    error = 0;

    printf("- STRESS TEST FOR VTA DISASSEMBLER.");

    error += test_nullPointers();
    error += test_invalidOPCode();
    error += test_uopOutOfBounds();
    error += test_nullUopBuffer();
    error += test_labelReuse();

    if (error > 0)
        printf("X - NOT ALL TESTS HAVE PASSED (%d failures).\n", error);
    else
        printf("✔ - ALL TEST HAVE BEEN SUCCESSFULLY HANDLED!\n");

    return error;
}