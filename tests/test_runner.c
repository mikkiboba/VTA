#include <stdio.h>
#include <string.h>
#include <stdint.h>
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


static int _checkOutput(const char *testName, VTAErr status, const char *actual, const char *expected) {
    if (status != VTA_OK) {
        printf(" FAIL! %s returned status [%d].\n\n", testName, status);
        return 1;
    }

    if (strcmp(actual, expected) != 0) {
        printf(" FAIL! %s produced unexpected output.\n", testName);
        printf(" Expected: %s - Actual: %s\n", expected, actual);
        return 1;
    }

    printf(" PASS! %s.\n\n", testName);
    return 0;
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

    printf(" 1.4 - Passing numUop = -1\n");
    status = vtaDisassemble(&testInsn, 1, NULL, -1, outBuffer, sizeof(outBuffer));
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


// * helper for below
static int _countOccurrences(const char *text, const char *pattern) {
    int count;
    count = 0;

    const char *cursor;
    cursor = text;

    while ((cursor = strstr(cursor, pattern)) != NULL) {
        count++;
        cursor += strlen(pattern);
    }

    return count;
}


static int test_labelReuse(void) {
    printHeader("5. Check on Label Generation and Reuse (from main.c)");
    int error;
    error = 0;

    VTAGenericInsn insn_stream[5];
    VTAUop uop_stream[5];
    char out_buf[OUT_BUF_SIZE];

    memset(insn_stream, 0, sizeof(insn_stream));
    memset(uop_stream, 0, sizeof(uop_stream));
    memset(out_buf, 0, sizeof(out_buf));

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
        return 1;
    }
    
    if (
        _countOccurrences(out_buf, "lbl1_bgn:") != 1 ||
        _countOccurrences(out_buf, "lbl1_end:") != 1 ||
        _countOccurrences(out_buf, "lbl2_bgn:") != 1 ||
        _countOccurrences(out_buf, "lbl2_end:") != 1 
    ) {
        printf(" FAIL! Disassembler returned error %d\n", status);
        error++;
    } 

    if (_countOccurrences(out_buf, "FOR (4, 2) UOP (lbl1_bgn, lbl1_end)") != 2) {
        printf(" FAIL! UOP range [0, 2) did not reuse lbl1.\n");
        error++;
    }

    if (_countOccurrences(out_buf, "FOR (4, 2) UOP (lbl2_bgn, lbl2_end)") != 1) {
        printf(" FAIL! UOP range [1, 4) did not generate lbl2.\n");
        error++;
    }


    if (error == 0) {
        printf(" PASS! Labels are generated and reused correctly.\n\n");
        printf("Generated output:\n\n%s\n", out_buf);
    }

    return error;
}


static int test_outputBuffer(void) {
    printHeader("6. Check on output buffer parameters.");

    int error;
    error = 0;

    VTAErr status;

    VTAGenericInsn testInsn;

    char outBuffer[OUT_BUF_SIZE];
    memset(&testInsn, 0, sizeof(testInsn));

    printf(" 6.1 - Passing outBuffer = NULL\n");
    status = vtaDisassemble(&testInsn, 1, NULL, 0, NULL, sizeof(outBuffer));
    checkStatus(status, VTA_ERR_NULLPTR, &error);

    printf(" 6.2 - Passing outBufferSize = 0\n");
    status = vtaDisassemble(&testInsn, 1, NULL, 0, outBuffer, 0);
    checkStatus(status, VTA_ERR_INVALID_INSN_SIZE, &error);

    printf(" 6.3 - Passing too small outBuffer\n");
    char tinyBuffer[4];
    status = vtaDisassemble(&testInsn, 1, NULL, 0, tinyBuffer, sizeof(tinyBuffer));
    checkStatus(status, VTA_ERR_OUTPUT_BUFFER_FULL, &error);

    printf(" 6.4 - Passing exactly sized output buffer\n");
    char exactBuffer[9];
    status = vtaDisassemble(&testInsn, 1, NULL, 0, exactBuffer, sizeof(exactBuffer));
    checkStatus(status, VTA_OK, &error);

    if (status == VTA_OK && strcmp(exactBuffer, "FINISH\n\n") != 0) {
        printf(" FAIL! Exact-size buffer contains incorrect output.\n\n");
    }

    return error;
}


static int test_uopBufferSize(void) {
    printHeader("7. Check UOP indices against numUop.");

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
    testGemmInsn.uop_end = 5;

    int numUop;
    numUop = 4;

    printf(" 7.1 - Passing uop_end (%u) > numUop (%u)", (unsigned)testGemmInsn.uop_end, (unsigned)numUop);

    status = vtaDisassemble(insn, 1, NULL, numUop, outBuffer, sizeof(outBuffer));
    checkStatus(status, VTA_ERR_UOP_OUT_OF_BOUNDS, &error);

    testGemmInsn.uop_bgn = 0;
    testGemmInsn.uop_end = 4;

    printf(" 7.2 - Passing valid range [%u, %u) with numUop = %u\n", (unsigned)testGemmInsn.uop_bgn, (unsigned)testGemmInsn.uop_end, (unsigned)numUop);

    status = vtaDisassemble(insn, 1, NULL, numUop, outBuffer, sizeof(outBuffer));
    checkStatus(status, VTA_OK, &error);

    return error;
}


static int test_finish(void) {
    printHeader("8. Check FINISH disassembly.");

    int error;
    error = 0;

    VTAGenericInsn insn;
    memset(&insn, 0, sizeof(insn));

    char outBuffer[OUT_BUF_SIZE];
    memset(outBuffer, 0, sizeof(outBuffer));

    insn.opcode = VTA_OPCODE_FINISH;

    VTAErr status;
    status = vtaDisassemble(&insn, 1, NULL, 0, outBuffer, sizeof(outBuffer));

    if (status != VTA_OK) {
        printf(" FAIL! Unexpected status [%d]", status);
        return 1;
    }

    if (strcmp(outBuffer, "FINISH\n\n") != 0) {
        printf(" FAIL! Unexpected output:\n%s\n", outBuffer);
        return 1;
    }

    printf(" PASS! FINISH disassembled correctly.\n");
    return 0;
}


static int test_noop(void) {
    printHeader("9. Check NOOP disassembly.");

    VTAMemInsn insn;
    char outBuffer[OUT_BUF_SIZE];

    memset(&insn, 0, sizeof(insn));
    memset(outBuffer, 0, sizeof(outBuffer));

    insn.opcode = VTA_OPCODE_LOAD;
    insn.x_size = 0;

    VTAErr status;
    status = vtaDisassemble((VTAGenericInsn *)&insn, 1, NULL, 0, outBuffer, sizeof(outBuffer));

    return _checkOutput("NOOP", status, outBuffer, "NOOP\n\n");
}

static int test_load(void) {
    printHeader("10. Check LOAD disassembly.");

    int error;
    error = 0;

    VTAMemInsn insn;
    char outBuffer[OUT_BUF_SIZE];

    printf(" 10.1 - Uop memory load.\n");

    memset(&insn, 0, sizeof(insn));
    memset(outBuffer, 0, sizeof(outBuffer));

    insn.opcode         = VTA_OPCODE_LOAD;
    insn.memory_type    = VTA_MEM_ID_UOP;
    insn.sram_base      = 7;
    insn.dram_base      = 11;
    insn.x_size         = 3;

    VTAErr status;
    status = vtaDisassemble((VTAGenericInsn *)&insn, 1, NULL, 0, outBuffer, sizeof(outBuffer));

    error += _checkOutput("LOAD from UOP memory", status, outBuffer, "LOAD(BUF[7], MEM[11, 3])\n\n");

    printf(" 10.2 - Normal 2d load.\n");

    memset(&insn, 0, sizeof(insn));
    memset(outBuffer, 0, sizeof(outBuffer));

    insn.opcode         = VTA_OPCODE_LOAD;
    insn.memory_type    = VTA_MEM_ID_INP;
    insn.sram_base      = 4;
    insn.dram_base      = 100;
    insn.y_size         = 2;
    insn.x_size         = 8;
    insn.x_stride       = 16;

    status = vtaDisassemble((VTAGenericInsn *)&insn, 1, NULL, 0, outBuffer, sizeof(outBuffer));
    error += _checkOutput("2D LOAD", status, outBuffer, "LOAD(BUF[4], MEM[100, 2, 8, 16])\n\n");

    return error;
}


static int test_store(void) {
    printHeader("11. Check STORE disassembly.");

    VTAMemInsn insn;
    char outBuffer[OUT_BUF_SIZE];

    memset(&insn, 0, sizeof(insn));
    memset(outBuffer, 0, sizeof(outBuffer));

    insn.opcode     = VTA_OPCODE_STORE;
    insn.sram_base  = 5;
    insn.dram_base  = 200;
    insn.y_size     = 2;
    insn.x_size     = 8;
    insn.x_stride   = 16;

    VTAErr status;
    status = vtaDisassemble((VTAGenericInsn *)&insn, 1, NULL, 0, outBuffer, sizeof(outBuffer));

    return _checkOutput("STORE", status, outBuffer, "STOR(MEM[200, 2, 8, 16], ACC[5])\n\n");
}


static int test_alu(void) {
    printHeader("12. Check ALU disassembly.\n");

    int error = 0;

    VTAAluInsn insn;
    char outBuffer[OUT_BUF_SIZE];

    printf(" 12.1 - ALU with intermediate.\n");

    memset(&insn, 0, sizeof(insn));
    memset(outBuffer, 0, sizeof(outBuffer));

    insn.opcode     = VTA_OPCODE_ALU;
    insn.uop_bgn    = 0;
    insn.uop_end    = 2;
    insn.iter_out   = 3;
    insn.iter_in    = 4;
    insn.use_imm    = 1;
    insn.imm        = -7;

    VTAErr status;
    status = vtaDisassemble((VTAGenericInsn *)&insn, 1, NULL, 2, outBuffer, sizeof(outBuffer));

    error += _checkOutput("ALU with intermediate", status, outBuffer, "FOR (3, 4) UOP (lbl1_bgn, lbl1_end)\nALU.OP(DST[0:2], -7)\n\n");

    return error;
}


static int test_gemm(void)
{
    printHeader("13. Check GEMM disassembly.");

    int error = 0;

    VTAGemInsn insn;
    char outBuffer[OUT_BUF_SIZE];

    printf(" 13.1 - Normal GEMM.\n");
    memset(&insn, 0, sizeof(insn));
    memset(outBuffer, 0, sizeof(outBuffer));

    insn.opcode = VTA_OPCODE_GEMM;
    insn.uop_bgn = 0;
    insn.uop_end = 3;
    insn.iter_out = 2;
    insn.iter_in = 4;
    insn.reset_reg = 0;

    VTAErr status = vtaDisassemble(
        (VTAGenericInsn *)&insn,
        1,
        NULL,
        3,
        outBuffer,
        sizeof(outBuffer)
    );

    error += _checkOutput(
        "GEMM",
        status,
        outBuffer,
        "FOR (2, 4) UOP (lbl1_bgn, lbl1_end)\n"
        "GEMM(ACC[0:3], INP[0:3], WGT[0:3])\n\n"
    );


    printf(" 13.2 - GEMM reset.\n");
    memset(&insn, 0, sizeof(insn));
    memset(outBuffer, 0, sizeof(outBuffer));

    insn.opcode = VTA_OPCODE_GEMM;
    insn.uop_bgn = 2;
    insn.uop_end = 5;
    insn.iter_out = 1;
    insn.iter_in = 6;
    insn.reset_reg = 1;

    status = vtaDisassemble(
        (VTAGenericInsn *)&insn,
        1,
        NULL,
        5,
        outBuffer,
        sizeof(outBuffer)
    );

    error += _checkOutput(
        "GEMM.RST",
        status,
        outBuffer,
        "FOR (1, 6) UOP (lbl1_bgn, lbl1_end)\n"
        "GEMM.RST(ACC[2:5])\n\n"
    );

    return error;
}


static int test_dependencies(void)
{
    printHeader("14. Check POP/PUSH dependency output.");

    int error = 0;

    char outBuffer[OUT_BUF_SIZE];


    /*
     * Current disassembler mapping:
     * POP  -> EX->LD
     * PUSH -> LD->EX
     */
    printf(" 14.1 - LOAD dep.\n");
    VTAMemInsn load;
    memset(&load, 0, sizeof(load));
    memset(outBuffer, 0, sizeof(outBuffer));

    load.opcode = VTA_OPCODE_LOAD;
    load.memory_type = VTA_MEM_ID_UOP;
    load.sram_base = 1;
    load.dram_base = 2;
    load.x_size = 3;

    load.pop_prev_dep = 1;
    load.push_prev_dep = 1;

    VTAErr status = vtaDisassemble(
        (VTAGenericInsn *)&load,
        1,
        NULL,
        0,
        outBuffer,
        sizeof(outBuffer)
    );

    error += _checkOutput(
        "LOAD dependencies",
        status,
        outBuffer,
        "POP (EX->LD)\n"
        "LOAD(BUF[1], MEM[2, 3])\n"
        "PUSH (LD->EX)\n\n"
    );


    /*
     * Current disassembler mapping:
     * POP  -> EX->ST
     * PUSH -> ST->EX
    */
    printf(" 14.2 - STORE dep.\n");
    VTAMemInsn store;
    memset(&store, 0, sizeof(store));
    memset(outBuffer, 0, sizeof(outBuffer));

    store.opcode = VTA_OPCODE_STORE;
    store.sram_base = 4;
    store.dram_base = 10;
    store.y_size = 1;
    store.x_size = 2;
    store.x_stride = 2;

    store.pop_prev_dep = 1;
    store.push_prev_dep = 1;

    status = vtaDisassemble(
        (VTAGenericInsn *)&store,
        1,
        NULL,
        0,
        outBuffer,
        sizeof(outBuffer)
    );

    error += _checkOutput(
        "STORE dependencies",
        status,
        outBuffer,
        "POP (EX->ST)\n"
        "STOR(MEM[10, 1, 2, 2], ACC[4])\n"
        "PUSH (ST->EX)\n\n"
    );


    /*
     * All four dependency bits are active.
     */
    printf("14.3 - GEMM dep.\n");
    VTAGemInsn gemm;
    memset(&gemm, 0, sizeof(gemm));
    memset(outBuffer, 0, sizeof(outBuffer));

    gemm.opcode = VTA_OPCODE_GEMM;
    gemm.uop_bgn = 0;
    gemm.uop_end = 1;
    gemm.iter_out = 1;
    gemm.iter_in = 1;

    gemm.pop_prev_dep = 1;
    gemm.pop_next_dep = 1;
    gemm.push_prev_dep = 1;
    gemm.push_next_dep = 1;

    status = vtaDisassemble(
        (VTAGenericInsn *)&gemm,
        1,
        NULL,
        1,
        outBuffer,
        sizeof(outBuffer)
    );

    error += _checkOutput(
        "GEMM dependencies",
        status,
        outBuffer,
        "POP (LD->EX, ST->EX)\n"
        "FOR (1, 1) UOP (lbl1_bgn, lbl1_end)\n"
        "GEMM(ACC[0:1], INP[0:1], WGT[0:1])\n"
        "PUSH (EX->LD, EX->ST)\n\n"
    );

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
    error += test_outputBuffer();
    error += test_uopBufferSize();
    error += test_finish();
    error += test_noop();
    error += test_load();
    error += test_store();
    error += test_alu();
    error += test_gemm();
    error += test_dependencies();

    if (error > 0)
        printf("X - NOT ALL TESTS HAVE PASSED (%d failures).\n", error);
    else
        printf("✔ - ALL TEST HAVE BEEN SUCCESSFULLY HANDLED!\n");

    return error;
}