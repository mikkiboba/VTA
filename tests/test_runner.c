#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include <stdarg.h>

#include <vta/hw_spec.h>

#include "vta.h"


#define OUT_BUF_SIZE 2048

#define PASS_CHAR "✔" 
#define FAIL_CHAR "X"


/* ---------
    HELPER FOR PRINTED OUTPUT
*/

static void printHeader(
    const char *label, 
    const char *testName
)
{
    printf("\n----\n");
    printf("[%s]: %s", label, testName);
    printf("\n----\n");
}


static void printTestName(
    int verbose, 
    const char *testName, 
    ...
) 
{
    va_list args;
    va_start(args, testName);

    printf("» ");
    vprintf(testName, args);
    printf("%c ", (verbose) ? '\n' : ':');

    va_end(args);
}


static void checkStatus(
    VTAErr  status,
    VTAErr  expectedStatus,
    int     *errorCount,
    int     verbose
)
{
    if (verbose) 
    {
        if (status == expectedStatus)
        {
            if (status != VTA_OK)
                vtaErrorPrint(status, -1);
            printf("%s Crashes avoided and correct error returned.\n\n", PASS_CHAR);
        }
        else
        {
            printf("%s Expected status [%d] but got [%d].\n\n", FAIL_CHAR, expectedStatus, status);
            (*errorCount)++;
        }
    }
    else 
    {
        if (status == expectedStatus)
            printf("%s\n", PASS_CHAR);
        else
        {
            printf("%s\n", FAIL_CHAR);
            (*errorCount)++;
        }
    }
}


/* ------------
    HELPERS FOR TESTS 
*/
static void memInsn( 
    VTAGenericInsn *insn_queue, int insn_idx, int opcode, int mem_type, 
    int sram_base, uint32_t dram_base, int x_size, int y_size, int x_stride,
    int pop_prev, int pop_next, int push_prev, int push_next
) 
{
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
)
{
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


static int countOccurrences(const char *text, const char *pattern) 
{
    int count;
    count = 0;

    const char *cursor;
    cursor = text;

    while ((cursor = strstr(cursor, pattern)) != NULL) 
    {
        count++;
        cursor += strlen(pattern);
    }

    return count;
}


static int checkOutput(
    const char *testName,
    VTAErr status, 
    const char *actual, 
    const char *expected,
    int verbose
) 
{
    if (status != VTA_OK) 
    {
        printf((verbose) ? " %s! %s returned status [%d].\n\n" : "X\n", FAIL_CHAR, testName, status);
        return 1;
    }

    if (strcmp(actual, expected) != 0) 
    {
        printf((verbose) ? " %s! %s produced unexpected output.\n" : "%s", FAIL_CHAR, testName);
        printf((verbose) ? " - Expected:\n%s - Actual:\n%s\n" : "\n", expected, actual);
        return 1;
    }

    printf((verbose) ? " %s! %s.\n\n" : "%s\n", PASS_CHAR, testName);
    return 0;
}


/* ------------
    TESTS FOR THE DISASSEMBLER
*/


static int dTest_nullPointers(int verbose) 
{
    printHeader("DISASSEMBLER", "Check on NULL pointers and numInsn <= 0");

    int error;
    error = 0;

    VTAErr status;
    
    char outBuffer[OUT_BUF_SIZE];

    printTestName(verbose, "Passing insnBuffer = NULL, numInsn = 5");
    status = vtaDisassemble(NULL, 5, NULL, 0, outBuffer, sizeof(outBuffer));
    checkStatus(status, VTA_ERR_NULLPTR, &error, verbose);

    printTestName(verbose, "Passing numInsn = 0");
    VTAGenericInsn testInsn;
    memset(&testInsn, 0, sizeof(testInsn));
    status = vtaDisassemble(&testInsn, 0, NULL, 0, outBuffer, sizeof(outBuffer));
    checkStatus(status, VTA_ERR_INVALID_INSN_SIZE, &error, verbose);

    printTestName(verbose, "Passing numInsn = -5");
    status = vtaDisassemble(&testInsn, -5, NULL, 0, outBuffer, sizeof(outBuffer));
    checkStatus(status, VTA_ERR_INVALID_INSN_SIZE, &error, verbose);

    printTestName(verbose, "Passing numUop = -1");
    status = vtaDisassemble(&testInsn, 1, NULL, -1, outBuffer, sizeof(outBuffer));
    checkStatus(status, VTA_ERR_INVALID_INSN_SIZE, &error, verbose);

    return error;
}


static int dTest_invalidOPCode(int verbose) 
{
    printHeader("DISASSEMBLER", "Check on invalid / unknwon opcodes.");

    int error;
    error = 0;

    VTAErr status;

    char outBuffer[OUT_BUF_SIZE];

    VTAGenericInsn testInsns;
    memset(&testInsns, 0, sizeof(testInsns));
    testInsns.opcode = 7;

    printTestName(verbose, "Passing instruction with unknown opcode (%d)", testInsns.opcode);
    status = vtaDisassemble(&testInsns, 1, NULL, 0, outBuffer, sizeof(outBuffer));
    checkStatus(status, VTA_ERR_UNKNOWN_OPCODE, &error, verbose);

    return error;
}


static int dTest_uopOutOfBounds(int verbose) 
{
    printHeader("DISASSEMBLER", "Check on if UOP indices are out of bounds.");

    int error;
    error = 0;

    VTAErr status;
    
    char outBuffer[OUT_BUF_SIZE];
    
    VTAUop uopBuffer[VTA_UOP_BUFF_DEPTH];
    memset(uopBuffer, 0, sizeof(uopBuffer));

    VTAGemInsn testGemmInsn;
    memset(&testGemmInsn, 0, sizeof(testGemmInsn));

    VTAGenericInsn *insn;
    insn = (VTAGenericInsn *)&testGemmInsn;

    insn->opcode            = VTA_OPCODE_GEMM;
    testGemmInsn.uop_bgn    = 0;
    testGemmInsn.uop_end    = VTA_UOP_BUFF_DEPTH + 1;

    printTestName(verbose, "Passing GEMM instruction with uop_end (%u) > VTA_UOP_BUFF_DEPTH (%u)", 
        (unsigned)testGemmInsn.uop_end, (unsigned)VTA_UOP_BUFF_DEPTH);

    status = vtaDisassemble(insn, 1, uopBuffer, VTA_UOP_BUFF_DEPTH, outBuffer, sizeof(outBuffer));
    checkStatus(status, VTA_ERR_UOP_OUT_OF_BOUNDS, &error, verbose);

    testGemmInsn.uop_bgn = 10;
    testGemmInsn.uop_end = 9;

    printTestName(verbose, "Passing GEMM instruction with uop_bgn (%u) > uop_end (%u)", 
        (unsigned)testGemmInsn.uop_bgn, (unsigned)testGemmInsn.uop_end);

    status = vtaDisassemble(insn, 1, uopBuffer, VTA_UOP_BUFF_DEPTH, outBuffer, sizeof(outBuffer));
    checkStatus(status, VTA_ERR_UOP_OUT_OF_BOUNDS, &error, verbose);

    return error;
}


static int dTest_nullUopBuffer(int verbose) 
{
    printHeader("DISASSEMBLER", "Check on GEMM/ALU instructions with uopBuffer = NULL.");

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

    printTestName(verbose, "Disassembling GEMM with uopBuffer = NULL");
    status = vtaDisassemble(insn, 1, NULL, 5, outBuffer, sizeof(outBuffer));
    checkStatus(status, VTA_ERR_NULLPTR, &error, verbose);

    printTestName(verbose, "Passing numUop = 0 with uopBuffer = NULL");
    VTAGenericInsn finish;
    memset(&finish, 0, sizeof(finish));

    finish.opcode = VTA_OPCODE_FINISH;

    status = vtaDisassemble(&finish, 1, NULL, 0, outBuffer, sizeof(outBuffer));
    checkStatus(status, VTA_OK, &error, verbose);

    return error;
}


static int dTest_labelReuse(int verbose) 
{
    printHeader("DISASSEMBLER", "Check on Label Generation and Reuse (from main.c)");

    int error;
    error = 0;

    VTAGenericInsn insn_stream[5];
    memset(insn_stream, 0, sizeof(insn_stream));

    VTAUop uop_stream[5];
    memset(uop_stream, 0, sizeof(uop_stream));

    char out_buf[OUT_BUF_SIZE];
    memset(out_buf, 0, sizeof(out_buf));

    printTestName(verbose, "Encoding instructions and UOPs to test the labels");

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

    if (status != VTA_OK) 
    {
        printf((verbose) ? " X! Disassembler returned error %d\n" : "X", status);
        return 1;
    }
    
    if (
        countOccurrences(out_buf, "lbl1_bgn:") != 1 ||
        countOccurrences(out_buf, "lbl1_end:") != 1 ||
        countOccurrences(out_buf, "lbl2_bgn:") != 1 ||
        countOccurrences(out_buf, "lbl2_end:") != 1 
    ) 
    {
        printf((verbose) ? " X! Disassembler returned error %d\n" : "X", status);
        error++;
    } 

    if (countOccurrences(out_buf, "FOR (4, 2) UOP (lbl1_bgn, lbl1_end)") != 2) 
    {
        printf((verbose) ? " X! UOP range [0, 2) did not reuse lbl1.\n" : "X");
        error++;
    }

    if (countOccurrences(out_buf, "FOR (4, 2) UOP (lbl2_bgn, lbl2_end)") != 1) 
    {
        printf((verbose) ? " X! UOP range [1, 4) did not generate lbl2.\n" : "X");
        error++;
    }

    if (error == 0) 
    {
        printf((verbose) ? " ✔! Labels are generated and reused correctly.\n" : "✔\n");
        printf((verbose) ? "Generated output:\n\n%s\n" : "", out_buf );
    }

    return error;
}


static int dTest_outputBuffer(int verbose) 
{
    printHeader("DISASSEMBLER", "Check on output buffer parameters.");

    int error;
    error = 0;

    VTAErr status;

    VTAGenericInsn testInsn;
    memset(&testInsn, 0, sizeof(testInsn));

    char outBuffer[OUT_BUF_SIZE];

    printTestName(verbose, "Passing outBuffer = NULL");
    status = vtaDisassemble(&testInsn, 1, NULL, 0, NULL, sizeof(outBuffer));
    checkStatus(status, VTA_ERR_NULLPTR, &error, verbose);

    printTestName(verbose, "Passing outBufferSize = 0");
    status = vtaDisassemble(&testInsn, 1, NULL, 0, outBuffer, 0);
    checkStatus(status, VTA_ERR_INVALID_INSN_SIZE, &error, verbose);

    testInsn.opcode = VTA_OPCODE_FINISH;

    printTestName(verbose, "Passing too small outBuffer");
    char tinyBuffer[4];
    status = vtaDisassemble(&testInsn, 1, NULL, 0, tinyBuffer, sizeof(tinyBuffer));
    checkStatus(status, VTA_ERR_OUTPUT_BUFFER_FULL, &error, verbose);

    printTestName(verbose, "Passing exactly sized outBuffer");
    const char expected[] = "FINISH\n\n";
    char exactBuffer[sizeof(expected)];
    status = vtaDisassemble(&testInsn, 1, NULL, 0, exactBuffer, sizeof(exactBuffer));
    checkStatus(status, VTA_OK, &error, verbose);

    if (status == VTA_OK && strcmp(exactBuffer, "FINISH\n\n") != 0) 
    {
        printf((verbose) ? " X! Exact-size buffer contains incorrect output.\n\n" : "X");
        error++;
    }

    return error;
}


static int dTest_uopBufferSize(int verbose) 
{
    printHeader("DISASSEMBLER", "Check UOPs indices against numUop.");

    int error;
    error = 0;

    VTAErr status;
    char outBuffer[OUT_BUF_SIZE];

    VTAUop uopBuffer[4];
    memset(uopBuffer, 0, sizeof(uopBuffer));

    VTAGemInsn testGemmInsn;
    memset(&testGemmInsn, 0, sizeof(testGemmInsn));

    testGemmInsn.uop_bgn = 0;
    testGemmInsn.uop_end = 5;

    VTAGenericInsn *insn;
    insn = (VTAGenericInsn *)&testGemmInsn;
    insn->opcode = VTA_OPCODE_GEMM;

    int numUop;
    numUop = 4;

    printTestName(verbose, "Passing uop_end (%u) > numUop (%u)",
        (unsigned)testGemmInsn.uop_end, (unsigned)numUop
    );
    status = vtaDisassemble(insn, 1, uopBuffer, numUop, outBuffer, sizeof(outBuffer));
    checkStatus(status, VTA_ERR_UOP_OUT_OF_BOUNDS, &error, verbose);

    testGemmInsn.uop_bgn = 0;
    testGemmInsn.uop_end = 4;

    printTestName(verbose, "Passing valid range [%u, %u) with numUop = %u",
         (unsigned)testGemmInsn.uop_bgn, (unsigned)testGemmInsn.uop_end, (unsigned)numUop);
    status = vtaDisassemble(insn, 1, uopBuffer, numUop, outBuffer, sizeof(outBuffer));
    checkStatus(status, VTA_OK, &error, verbose);

    return error;
}


static int dTest_finish(int verbose) 
{
    printHeader("DISASSEMBLER", "Check FINISH disassembly.");

    int error;
    error = 0;

    VTAGenericInsn insn;
    memset(&insn, 0, sizeof(insn));

    insn.opcode = VTA_OPCODE_FINISH;

    char outBuffer[OUT_BUF_SIZE];
    memset(outBuffer, 0, sizeof(outBuffer));

    printTestName(verbose, "FINISH works correctly");

    VTAErr status;
    status = vtaDisassemble(&insn, 1, NULL, 0, outBuffer, sizeof(outBuffer));

    if (status != VTA_OK) 
    {
        printf((verbose) ? " X! Unexpected status [%d]" : "X", status);
        return 1;
    }

    if (strcmp(outBuffer, "FINISH\n\n") != 0) 
    {
        printf((verbose) ? " X! Unexpected output:\n%s\n" : "X", outBuffer);
        return 1;
    }

    printf((verbose) ? " ✔! FINISH disassembled correctly.\n" : "✔");
    return 0;
}


static int dTest_noop(int verbose) 
{
    printHeader("DISASSEMBLER", "Check NOOP disassembly");

    printTestName(verbose, "NOOP works correctly");

    VTAMemInsn insn;
    memset(&insn, 0, sizeof(insn));

    insn.opcode = VTA_OPCODE_LOAD;
    insn.x_size = 0;

    char outBuffer[OUT_BUF_SIZE];
    memset(outBuffer, 0, sizeof(outBuffer));

    VTAErr status;
    status = vtaDisassemble((VTAGenericInsn *)&insn, 1, NULL, 0, outBuffer, sizeof(outBuffer));

    return checkOutput("NOOP", status, outBuffer, "NOOP\n\n", verbose);
}


static int dTest_load(int verbose) 
{
    printHeader("DISASSEMBLER", "Check LOAD disassembly");

    int error;
    error = 0;

    VTAMemInsn insn;
    char outBuffer[OUT_BUF_SIZE];

    printTestName(verbose, "UOP memory load");

    memset(&insn, 0, sizeof(insn));
    memset(outBuffer, 0, sizeof(outBuffer));

    insn.opcode         = VTA_OPCODE_LOAD;
    insn.memory_type    = VTA_MEM_ID_UOP;
    insn.sram_base      = 7;
    insn.dram_base      = 11;
    insn.x_size         = 3;

    VTAErr status;
    status = vtaDisassemble((VTAGenericInsn *)&insn, 1, NULL, 0, outBuffer, sizeof(outBuffer));

    error += checkOutput(
        "LOAD from UOP memory", 
        status, 
        outBuffer, 
        "LOAD(UOP[7], MEM[11, 3])\n\n", 
        verbose
    );

    printTestName(verbose, "Normal 2D load");

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
    error += checkOutput(
        "2D LOAD", 
        status, 
        outBuffer, 
        "LOAD(INP[4], MEM[100, 2, 8, 16])\n\n",
        verbose
    );

    return error;
}


static int dTest_store(int verbose) 
{
    printHeader("DISASSEMBLER", "Check STORE disassembly");

    printTestName(verbose, "STORE works correctly");

    VTAMemInsn insn;
    memset(&insn, 0, sizeof(insn));

    char outBuffer[OUT_BUF_SIZE];
    memset(outBuffer, 0, sizeof(outBuffer));

    insn.opcode     = VTA_OPCODE_STORE;
    insn.sram_base  = 5;
    insn.dram_base  = 200;
    insn.y_size     = 2;
    insn.x_size     = 8;
    insn.x_stride   = 16;

    VTAErr status;
    status = vtaDisassemble((VTAGenericInsn *)&insn, 1, NULL, 0, outBuffer, sizeof(outBuffer));

    return checkOutput(
        "STORE", 
        status, 
        outBuffer, 
        "STOR(MEM[200, 2, 8, 16], ACC[5])\n\n",
        verbose
    );
}


static int dTest_alu(int verbose) 
{
    printHeader("DISASSEMBLER", "Check ALU disassembly");

    int error = 0;

    VTAAluInsn insn;
    char outBuffer[OUT_BUF_SIZE];

    VTAUop uopBuffer[4];
    memset(uopBuffer, 0, sizeof(uopBuffer));

    printTestName(verbose, "ALU with immediate");

    memset(&insn, 0, sizeof(insn));
    memset(outBuffer, 0, sizeof(outBuffer));

    insn.opcode     = VTA_OPCODE_ALU;
    insn.alu_opcode = VTA_ALU_OPCODE_MIN;
    insn.uop_bgn    = 0;
    insn.uop_end    = 2;
    insn.iter_out   = 3;
    insn.iter_in    = 4;
    insn.use_imm    = 1;
    insn.imm        = -7;

    VTAErr status;
    status = vtaDisassemble((VTAGenericInsn *)&insn, 1, uopBuffer, 2, outBuffer, sizeof(outBuffer));

    error += checkOutput(
        "ALU with immediate",
        status,
        outBuffer,
        "lbl1_bgn:\n"
        "   0, 0, 0\n"
        "   0, 0, 0\n"
        "lbl1_end:\n"
        "\n"
        "FOR (3, 4) UOP (lbl1_bgn, lbl1_end)\n"
        "ALU.MIN(DST[0:2], -7)\n"
        "\n",
        verbose
    );

    printTestName(verbose, "ALU without immediate");
    memset(&insn, 0, sizeof(insn));
    memset(outBuffer, 0, sizeof(outBuffer));

    insn.opcode     = VTA_OPCODE_ALU;
    insn.alu_opcode = VTA_ALU_OPCODE_ADD;
    insn.uop_bgn    = 1;
    insn.uop_end    = 4;
    insn.iter_out   = 2;
    insn.iter_in    = 5;
    insn.use_imm    = 0;

    status = vtaDisassemble((VTAGenericInsn *)&insn, 1, uopBuffer, 4, outBuffer, sizeof(outBuffer));
    
    error += checkOutput(
        "ALU without immediate",
        status,
        outBuffer,
        "   0, 0, 0\n"
        "lbl1_bgn:\n"
        "   0, 0, 0\n"
        "   0, 0, 0\n"
        "   0, 0, 0\n"
        "lbl1_end:\n"
        "\n"
        "FOR (2, 5) UOP (lbl1_bgn, lbl1_end)\n"
        "ALU.ADD(DST[1:4], SRC[1:4])\n"
        "\n",
        verbose
    );

    return error;
}


static int dTest_gemm(int verbose)
{
    printHeader("DISASSEMBLER", "Check GEMM disassembly");

    int error;
    error = 0;

    VTAGemInsn insn;
    char outBuffer[OUT_BUF_SIZE];

    VTAUop uopBuffer[5];
    memset(uopBuffer, 0, sizeof(uopBuffer));

    printTestName(verbose, "Normal GEMM");

    memset(&insn, 0, sizeof(insn));
    memset(outBuffer, 0, sizeof(outBuffer));

    insn.opcode = VTA_OPCODE_GEMM;
    insn.uop_bgn    = 0;
    insn.uop_end    = 3;
    insn.iter_out   = 2;
    insn.iter_in    = 4;
    insn.reset_reg  = 0;

    VTAErr status = vtaDisassemble(
        (VTAGenericInsn *)&insn,
        1,
        uopBuffer,
        3,
        outBuffer,
        sizeof(outBuffer)
    );

    error += checkOutput(
        "GEMM",
        status,
        outBuffer,
        "lbl1_bgn:\n"
        "   0, 0, 0\n"
        "   0, 0, 0\n"
        "   0, 0, 0\n"
        "lbl1_end:\n"
        "\n"
        "FOR (2, 4) UOP (lbl1_bgn, lbl1_end)\n"
        "GEMM(ACC[0:3], INP[0:3], WGT[0:3])\n"
        "\n",
        verbose
    );


    printTestName(verbose, "GEMM reset");

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
        uopBuffer,
        5,
        outBuffer,
        sizeof(outBuffer)
    );

    error += checkOutput(
        "GEMM.RST",
        status,
        outBuffer,
        "   0, 0, 0\n"
        "   0, 0, 0\n"
        "lbl1_bgn:\n"
        "   0, 0, 0\n"
        "   0, 0, 0\n"
        "   0, 0, 0\n"
        "lbl1_end:\n"
        "\n"
        "FOR (1, 6) UOP (lbl1_bgn, lbl1_end)\n"
        "GEMM.RST(ACC[2:5])\n"
        "\n",
        verbose
    );

    return error;
}



static int dTest_dependencies(int verbose)
{
    printHeader("DISASSEMBLER", "Check POP / PUSH dependecy output");

    int error;
    error = 0;

    char outBuffer[OUT_BUF_SIZE];

    VTAUop gemmUopBuffer[1];
    memset(gemmUopBuffer, 0, sizeof(gemmUopBuffer));

    /*
     * Current disassembler mapping:
     * POP  -> EX->LD
     * PUSH -> LD->EX
     */
    printTestName(verbose, "LOAD dependency");

    VTAMemInsn load;
    memset(&load, 0, sizeof(load));
    memset(outBuffer, 0, sizeof(outBuffer));

    load.opcode         = VTA_OPCODE_LOAD;
    load.memory_type    = VTA_MEM_ID_UOP;
    load.sram_base      = 1;
    load.dram_base      = 2;
    load.x_size         = 3;

    load.pop_prev_dep   = 1;
    load.push_prev_dep  = 1;

    VTAErr status = vtaDisassemble(
        (VTAGenericInsn *)&load,
        1,
        NULL,
        0,
        outBuffer,
        sizeof(outBuffer)
    );

    error += checkOutput(
        "LOAD dependencies",
        status,
        outBuffer,
        "POP (EX->LD)\n"
        "LOAD(UOP[1], MEM[2, 3])\n"
        "PUSH (LD->EX)\n\n",
        verbose
    );


    /*
     * Current disassembler mapping:
     * POP  -> EX->ST
     * PUSH -> ST->EX
    */
    printTestName(verbose, "STORE dependency");

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

    error += checkOutput(
        "STORE dependencies",
        status,
        outBuffer,
        "POP (EX->ST)\n"
        "STOR(MEM[10, 1, 2, 2], ACC[4])\n"
        "PUSH (ST->EX)\n\n",
        verbose
    );


    /*
     * All four dependency bits are active.
     */
    printTestName(verbose, "GEMM dependency");

    VTAGemInsn gemm;
    memset(&gemm, 0, sizeof(gemm));
    memset(outBuffer, 0, sizeof(outBuffer));

    gemm.opcode     = VTA_OPCODE_GEMM;
    gemm.uop_bgn    = 0;
    gemm.uop_end    = 1;
    gemm.iter_out   = 1;
    gemm.iter_in    = 1;

    gemm.pop_prev_dep   = 1;
    gemm.pop_next_dep   = 1;
    gemm.push_prev_dep  = 1;
    gemm.push_next_dep  = 1;

    status = vtaDisassemble(
        (VTAGenericInsn *)&gemm,
        1,
        gemmUopBuffer,
        1,
        outBuffer,
        sizeof(outBuffer)
    );

    error += checkOutput(
        "GEMM dependencies",
        status,
        outBuffer,
        "lbl1_bgn:\n"
        "   0, 0, 0\n"
        "lbl1_end:\n"
        "\n"
        "POP (LD->EX, ST->EX)\n"
        "FOR (1, 1) UOP (lbl1_bgn, lbl1_end)\n"
        "GEMM(ACC[0:1], INP[0:1], WGT[0:1])\n"
        "PUSH (EX->LD, EX->ST)\n"
        "\n",
        verbose
    );

    return error;
}


/*-------------
    TESTS FOR ASSEMBLER
*/

static int aTest_uopDetection(int verbose) 
{
    printHeader("ASSEMBLER", "Check assembler UOP detection");

    printTestName(verbose, "Find the right UOPs and instructions");

    int error;
    error = 0;

    VTAGenericInsn insnBuffer[8];
    memset(insnBuffer, 0, sizeof(insnBuffer));

    VTAUop uopBuffer[8];
    memset(uopBuffer, 0, sizeof(uopBuffer));

    const char *asmCode;
    asmCode =
        "LOAD(INP[0], MEM[100, 2, 8, 16])\n"
        "10, 20, 30\n"
        "FINISH\n";

    int numInsn, numUop;
    numInsn = 0;
    numUop  = 0;

    VTAErr status; 
    status = vtaAssemble(asmCode, insnBuffer, 8, &numInsn, uopBuffer, 8, &numUop);

    if (status != VTA_OK) 
    {
        if (verbose)
        {
            printf(" X! vtaAssemble(...) returned status [%d].\n", status);
            vtaErrorPrint(status, -1);
        }
        else 
        {
            printf("X\n");
        }

        return 1;
    }

    if (numInsn != 2) 
    {
        printf((verbose) ? " X! Expected 2 instructions, got %d.\n" : "X\n", numInsn);
        return 1;
    }

    if (numUop != 1) 
    {
        printf((verbose) ? " X! Expected 1 uop, got %d.\n" : "X\n", numUop);
        return 1;
    }

    if (error == 0) 
    {
        printf((verbose) ? " ✔! Uop detection is independent from instruction params.\n" : "✔\n");
    }

    return error;
}


static int aTest_invalidUop(int verbose) 
{
    printHeader("ASSEMBLER", "Check invalid UOP syntax");

    int error;
    error = 0;

    VTAGenericInsn insnBuffer[8];
    memset(insnBuffer, 0, sizeof(insnBuffer));

    VTAUop uopBuffer[8];
    memset(uopBuffer, 0, sizeof(uopBuffer));

    printTestName(verbose, "Invalid UOP syntax (10, 20), no third argument");

    const char *asmCode;
    asmCode = "10, 20\n";

    int numInsn, numUop;
    numInsn = 0;
    numUop  = 0;

    VTAErr status;
    status = vtaAssemble(asmCode, insnBuffer, 8, &numInsn, uopBuffer, 8, &numUop);

    if (status != VTA_ERR_SYNTAX) 
    {
        if (verbose)
        {
            printf(" X! Expected VTA_ERR_SYNTAX (%d), got %d.\n", VTA_ERR_SYNTAX, status);
            vtaErrorPrint(status, -1);
        }
        else 
        {
            printf("X\n");
        }

        error++;
    } 
    else 
    {
        printf((verbose) ? " ✔! Invalid UOP syntax rejected correctly.\n" : "✔\n");
    }

    printTestName(verbose, "Too many arguments");

    memset(insnBuffer, 0, sizeof(insnBuffer));
    memset(uopBuffer, 0, sizeof(uopBuffer));

    asmCode = "10, 20, 30, 40\n";
    
    numInsn = 0;
    numUop  = 0;

    status = vtaAssemble(asmCode, insnBuffer, 8, &numInsn, uopBuffer, 8, &numUop);

    if (status != VTA_ERR_SYNTAX) 
    {
        if (verbose)
        {
            printf(" X! Expected VTA_ERR_SYNTAX (%d), got %d.\n", VTA_ERR_SYNTAX, status);
            vtaErrorPrint(status, -1);
        }
        else
            printf("X\n");
        
        error++;
    } 
    else 
    {
        printf((verbose) ? " ✔! Extra UOP field rejected correctly.\n" : "✔\n");
    }
    
    return error;
}


static int aTest_simpleInstructions(int verbose)
{
    printHeader("ASSEMBLER", "Check FINISH and NOOP parsing");

    printTestName(verbose, "Valid code (FINISH NOOP)");

    int error;
    error = 0;

    VTAGenericInsn insnBuffer[8];
    memset(insnBuffer, 0, sizeof(insnBuffer));

    VTAUop uopBuffer[8];
    memset(uopBuffer, 0, sizeof(uopBuffer));

    const char *asmCode;
    asmCode =
        "FINISH\n"
        "NOOP\n";

    int numInsn = 0;
    int numUop = 0;

    VTAErr status;
    status = vtaAssemble(asmCode, insnBuffer, 8, &numInsn, uopBuffer, 8, &numUop);

    if (status != VTA_OK) 
    {
        if (verbose)
        {
            printf(" %s! vtaAssemble returned status [%d].\n", FAIL_CHAR, status);
            vtaErrorPrint(status, -1);
        }
        else
            printf("%s\n", FAIL_CHAR);
        
        return 1;
    }

    if (numInsn != 2) 
    {
        printf((verbose) ? " %s! Expected 2 instructions, got %d.\n" : "X\n", FAIL_CHAR, numInsn);
        error++;
    }

    if (numUop != 0) 
    {
        printf((verbose) ? " %s! Expected 0 UOPs, got %d.\n" : "%s\n", FAIL_CHAR, numUop);
        error++;
    }

    if (error == 0)
        printf((verbose) ? " %s! FINISH and NOOP parsed correctly.\n" : "%s\n", PASS_CHAR);

    printTestName(verbose, "Invalid code (FINISH(123))");

    memset(insnBuffer, 0, sizeof(insnBuffer));
    memset(uopBuffer, 0, sizeof(uopBuffer));

    asmCode =
        "FINISH(123)\n";

    numInsn = 0;
    numUop  = 0;

    status = vtaAssemble(asmCode, insnBuffer, 8, &numInsn, uopBuffer, 8, &numUop);

    if (status != VTA_ERR_SYNTAX) 
    {
        if (verbose) 
        {
            printf(" %s! Expected VTA_ERR_SYNTAX (%d), got %d.\n", FAIL_CHAR, VTA_ERR_SYNTAX, status);
            vtaErrorPrint(status, -1);
        }
        else
            printf("%s\n", FAIL_CHAR);

        error++;
    }
    else {
        printf((verbose) ? " %s! Invalid FINISH syntax rejected correctly.\n" : "%s\n", PASS_CHAR);
    }

    return error;
}


static int aTest_loadParsing(int verbose)
{
    printHeader("ASSEMBLER", "Check LOAD parsing");

    int error;
    error = 0;

    VTAGenericInsn insnBuffer[8];
    memset(insnBuffer, 0, sizeof(insnBuffer));

    VTAUop uopBuffer[8];
    memset(uopBuffer, 0, sizeof(uopBuffer));

    printTestName(verbose, "Valid {LOAD(INP[], MEM[,,,])}");

    const char *asmCode =
        "LOAD(INP[4], MEM[100, 2, 8, 16])\n";

    int numInsn, numUop;
    numInsn = 0;
    numUop  = 0;

    VTAErr status;
    status = vtaAssemble(asmCode, insnBuffer, 8, &numInsn, uopBuffer, 8, &numUop);

    if (status != VTA_OK) 
    {
        if (verbose)
        {
            printf(" %s! LOAD returned status [%d].\n", FAIL_CHAR, status);
            vtaErrorPrint(status, -1);
        }
        else
            printf("%s\n", FAIL_CHAR);

        return 1;
    }

    if (numInsn != 1) 
    {
        printf((verbose) ? " %s! Expected 1 instruction, got %d.\n" : "%s\n", FAIL_CHAR, numInsn);
        error++;
    }

    if (numUop != 0) 
    {
        printf((verbose) ? " %s! Expected 0 UOPs, got %d.\n" : "%s\n", FAIL_CHAR, numUop);
        error++;
    }

    if (error == 0) 
    {
        printf((verbose) ? " %s! LOAD syntax parsed correctly.\n" : "%s\n", PASS_CHAR);
    }

    printTestName(verbose, "Invalid buffer {LOAD(XYZ[], MEM[,,,])}");

    memset(insnBuffer, 0, sizeof(insnBuffer));
    memset(uopBuffer, 0, sizeof(uopBuffer));

    asmCode =
        "LOAD(XYZ[0], MEM[100, 2, 8, 16])\n";

    numInsn = 0;
    numUop  = 0;

    status = vtaAssemble(asmCode, insnBuffer, 8, &numInsn, uopBuffer, 8, &numUop);

    if (status != VTA_ERR_UNKNOWN_MNEMONIC) 
    {
        if (verbose) 
        {
            printf(" %s! Expected VTA_ERR_UNKNOWN_MNEMONIC (%d), got %d.\n", FAIL_CHAR, VTA_ERR_UNKNOWN_MNEMONIC, status);
            vtaErrorPrint(status, -1);
        }
        else 
            printf("%s\n", FAIL_CHAR);

        return 1;
    }

    printf((verbose) ? " %s! Invalid LOAD memory type rejected correctly.\n" : "%s\n", PASS_CHAR);

    printTestName(verbose, "Invalid arguments {LOAD(INP[], MEM[,,])}");

    memset(insnBuffer, 0, sizeof(insnBuffer));
    memset(uopBuffer, 0, sizeof(uopBuffer));

    asmCode =
        "LOAD(INP[0], MEM[100, 8])\n";

    numInsn = 0;
    numUop  = 0;

    status = vtaAssemble(asmCode, insnBuffer, 8, &numInsn, uopBuffer, 8, &numUop);

    if (status != VTA_ERR_SYNTAX) 
    {
        if (verbose) 
        {
            printf(" %s! Expected VTA_ERR_SYNTAX (%d), got %d.\n", FAIL_CHAR, VTA_ERR_SYNTAX, status);
            vtaErrorPrint(status, -1); 
        }
        else
            printf("%s\n", FAIL_CHAR);

        return 1;
    }

    printf((verbose) ? " %s! Invalid LOAD arguments rejected correctly.\n" : "%s\n", PASS_CHAR);

    return error;
}


static int aTest_loadEncoding(int verbose) 
{
    printHeader("ASSEMBLER", "Check the correct encoding of LOAD");

    printTestName(verbose, "Correct LOAD instruction");

    int error;
    error = 0;

    VTAGenericInsn  insnBuffer[8];
    memset(insnBuffer, 0, sizeof(insnBuffer));

    VTAUop uopBuffer[8];
    memset(uopBuffer, 0, sizeof(uopBuffer));

    const char *asmCode;
    asmCode = "LOAD(INP[4], MEM[100, 2, 8, 16])\n";

    int numInsn, numUop;
    numInsn = 0;
    numUop  = 0;

    VTAErr status;
    status = vtaAssemble(asmCode, insnBuffer, 8, &numInsn, uopBuffer, 8, &numUop);

    if (status != VTA_OK) 
    {
        if (verbose)
        {
            printf(" %s! vtaAssemble(...) returned status [%d].\n", FAIL_CHAR, status);
            vtaErrorPrint(status, -1);
        }
        else
            printf("%s\n", FAIL_CHAR);

        return 1;
    }

    if (numInsn != 1) 
    {
        printf((verbose) ? " %s! Expected 1 instruction, got %d.\n" : "%s\n", FAIL_CHAR, numInsn);
        error++;
    }

    if (numUop != 0) 
    {
        printf((verbose) ? " %s! Expected 0 UOPs, got %d.\n" : "%s\n", FAIL_CHAR, numUop);
        error++;
    }

    VTAMemInsn *insn;
    insn = (VTAMemInsn *)&insnBuffer[0];

    if (insn->opcode != VTA_OPCODE_LOAD) 
    {
        printf((verbose) ? " %s! Wrong opcode: got %u.\n" : "%s\n", FAIL_CHAR, (unsigned)insn->opcode);
        error++;
    }

    if (insn->memory_type != VTA_MEM_ID_INP) 
    {
        printf((verbose) ? " %s! Wrong memory type, got %u.\n" : "%s\n", FAIL_CHAR, (unsigned)insn->memory_type);
        error++;
    }

    if (insn->sram_base != 4) 
    {
        printf((verbose) ? " %s! Wrong sram_base, got %u.\n" : "%s\n", FAIL_CHAR, (unsigned)insn->sram_base);
        error++;
    }

    if (insn->dram_base != 100) 
    {
        printf((verbose) ? " %s! Wrong dram_base: got %u.\n" : "%s\n", FAIL_CHAR, (unsigned)insn->dram_base);
        error++;
    }

    if (insn->y_size != 2) 
    {
        printf((verbose) ? " %s! Wrong y_size: got %u.\n" : "%s\n", FAIL_CHAR, (unsigned)insn->y_size);
        error++;
    }

    if (insn->x_size != 8) 
    {
        printf((verbose) ? " %s! Wrong x_size: got %u.\n" : "%s\n", FAIL_CHAR, (unsigned)insn->x_size);
        error++;
    }

    if (insn->x_stride != 16) 
    {
        printf((verbose) ? " %s! Wrong x_stride: got %u.\n" : "%s\n", FAIL_CHAR, (unsigned)insn->x_stride);
        error++;
    }

    if (error == 0)
        printf((verbose) ? " %s! LOAD fields encoded correctly.\n" : "%s\n", PASS_CHAR);

    return error;
}


static int aTest_uopEncoding(int verbose)
{
    printHeader("ASSEMBLER", "Check UOP binary encoding");

    printTestName(verbose, "Valid UOP instruction");

    int error;
    error = 0;

    VTAGenericInsn insnBuffer[8];
    memset(insnBuffer, 0, sizeof(insnBuffer));

    VTAUop uopBuffer[8];
    memset(uopBuffer, 0, sizeof(uopBuffer));

    const char *asmCode =
        "10, 20, 30\n"
        "11, 21, 31\n";

    int numInsn, numUop;
    numInsn = 0;
    numUop = 0;

    VTAErr status;
    status = vtaAssemble(asmCode, insnBuffer, 8, &numInsn, uopBuffer, 8, &numUop);

    if (status != VTA_OK) 
    {
        if (verbose) 
        {
            printf(" %s! vtaAssemble returned status [%d].\n", FAIL_CHAR, status);
            vtaErrorPrint(status, -1);
        }
        else
            printf("%s\n", FAIL_CHAR);

        return 1;
    }

    if (numInsn != 0) 
    {
        printf((verbose) ? " %s! Expected 0 instructions, got %d.\n" : "%s\n", FAIL_CHAR, numInsn);
        error++;
    }

    if (numUop != 2) 
    {
        printf((verbose) ? " %s! Expected 2 UOPs, got %d.\n" : "%s\n", FAIL_CHAR, numUop);
        error++;
    }

    if (uopBuffer[0].dst_idx != 10) 
    {
        printf((verbose) ? " %s! UOP[0] wrong dst_idx: got %u.\n" : "%s\n", FAIL_CHAR, (unsigned)uopBuffer[0].dst_idx);
        error++;
    }

    if (uopBuffer[0].src_idx != 20) 
    {
        printf((verbose) ? " %s! UOP[0] wrong src_idx: got %u.\n" : "%s\n", FAIL_CHAR, (unsigned)uopBuffer[0].src_idx);
        error++;
    }

    if (uopBuffer[0].wgt_idx != 30) 
    {
        printf((verbose) ? " %s! UOP[0] wrong wgt_idx: got %u.\n" : "%s\n", FAIL_CHAR, (unsigned)uopBuffer[0].wgt_idx);
        error++;
    }

    if (uopBuffer[1].dst_idx != 11) 
    {
        printf((verbose) ? " %s! UOP[1] wrong dst_idx: got %u.\n" : "%s\n", FAIL_CHAR, (unsigned)uopBuffer[1].dst_idx);
        error++;
    }

    if (uopBuffer[1].src_idx != 21) 
    {
        printf((verbose) ? " %s! UOP[1] wrong src_idx: got %u.\n" : "%s\n", FAIL_CHAR, (unsigned)uopBuffer[1].src_idx);
        error++;
    }

    if (uopBuffer[1].wgt_idx != 31) 
    {
        printf((verbose) ? " %s! UOP[1] wrong wgt_idx: got %u.\n" : "%s\n", FAIL_CHAR, (unsigned)uopBuffer[1].wgt_idx);
        error++;
    }

    if (error == 0)
        printf((verbose) ? " %s! UOP fields encoded correctly.\n" : "%s\n", PASS_CHAR);

    return error;
}


static int aTest_storeEncoding(int verbose)
{
    printHeader("ASSEMBLER", "Check STORE binary encoding");

    int error;
    error = 0;

    VTAGenericInsn insnBuffer[8];
    memset(insnBuffer, 0, sizeof(insnBuffer));

    VTAUop uopBuffer[8];
    memset(uopBuffer, 0, sizeof(uopBuffer));

    printTestName(verbose, "Valid STORE instruction");

    const char *asmCode;
    asmCode =
        "STORE(MEM[200, 2, 8, 16], ACC[5])\n";

    int numInsn, numUop;
    numInsn = 0;
    numUop = 0;

    VTAErr status;
    status = vtaAssemble(asmCode, insnBuffer, 8, &numInsn, uopBuffer, 8, &numUop);

    if (status != VTA_OK) 
    {
        if (verbose) 
        {
            printf(" %s! vtaAssemble returned status [%d].\n", FAIL_CHAR, status);
            vtaErrorPrint(status, -1);
        }
        else
            printf("%s\n", FAIL_CHAR);

        return 1;
    }

    if (numInsn != 1) 
    {
        printf((verbose) ? " %s! Expected 1 instruction, got %d.\n" : "%s\n", FAIL_CHAR, numInsn);
        error++;
    }

    if (numUop != 0) {
        printf((verbose) ? " %s! Expected 0 UOPs, got %d.\n" : "%s\n", FAIL_CHAR, numUop);
        error++;
    }

    VTAMemInsn *insn;
    insn = (VTAMemInsn *)&insnBuffer[0];

    if (insn->opcode != VTA_OPCODE_STORE) 
    {
        printf((verbose) ? " %s! Wrong opcode: got %u.\n" : "%s\n", FAIL_CHAR, (unsigned)insn->opcode);
        error++;
    }

    if (insn->sram_base != 5) 
    {
        printf((verbose) ? " %s! Wrong sram_base: got %u.\n" : "%s\n", FAIL_CHAR, (unsigned)insn->sram_base);
        error++;
    }

    if (insn->dram_base != 200) 
    {
        printf((verbose) ? " %s! Wrong dram_base: got %u.\n" : "%s\n", FAIL_CHAR, (unsigned)insn->dram_base);
        error++;
    }

    if (insn->y_size != 2) 
    {
        printf((verbose) ? " %s! Wrong y_size: got %u.\n" : "%s\n", FAIL_CHAR, (unsigned)insn->y_size);
        error++;
    }

    if (insn->x_size != 8) 
    {
        printf((verbose) ? " %s! Wrong x_size: got %u.\n" : "%s\n", FAIL_CHAR, (unsigned)insn->x_size);
        error++;
    }

    if (insn->x_stride != 16) 
    {
        printf((verbose) ? " %s! Wrong x_stride: got %u.\n" : "%s\n", FAIL_CHAR, (unsigned)insn->x_stride);
        error++;
    }

    if (error == 0)
        printf((verbose) ? " %s! STORE fields encoded correctly.\n" : "%s\n", PASS_CHAR);

    return error;
}


static int aTest_gemmEnconding(int verbose) 
{
    printHeader("ASSEMBLER", "Check GEMM binary encoding");

    int error;
    error = 0;

    VTAGenericInsn  insnBuffer[8];
    memset(insnBuffer, 0, sizeof(insnBuffer));

    VTAUop uopBuffer[8];
    memset(uopBuffer, 0, sizeof(uopBuffer));

    printTestName(verbose, "Valid GEMM instruction");

    const char *asmCode;
    asmCode =
        "lbl1_bgn:\n"
        "10, 20, 30\n"
        "11, 21, 31\n"
        "12, 22, 32\n"
        "lbl1_end:\n"
        "FOR (2, 4) UOP (lbl1_bgn, lbl1_end)\n"
        "GEMM(ACC[0:3], INP[0:3], WGT[0:3])\n";

    int numInsn, numUop;
    numInsn = 0;
    numUop  = 0;

    VTAErr status;
    status = vtaAssemble(asmCode, insnBuffer, 8, &numInsn, uopBuffer, 8, &numUop);

    if (status != VTA_OK) 
    {
        printf((verbose) ? " %s! vtaAssemble returned status [%d].\n" : "%s\n", FAIL_CHAR, status);
        vtaErrorPrint(status, -1);
        return 1;
    }

    if (numInsn < 1) 
    {
        printf((verbose) ? " %s! Expected 1 instruction, got %d.\n" : "%s\n", FAIL_CHAR, numInsn);
        error++;
    }

    if (numUop != 3) 
    {
        printf((verbose) ? " %s! Expected 3 UOPs, got %d.\n" : "%s\n", FAIL_CHAR, numUop);
        error++;
    }

    VTAGemInsn *insn;
    insn = (VTAGemInsn *)&insnBuffer[0];

    if (insn->opcode != VTA_OPCODE_GEMM) 
    {
        printf((verbose) ? " %s! Wrong opcode: got %d.\n" : "%s\n", FAIL_CHAR, (unsigned)insn->opcode);
        error++;
    }

    if (insn->uop_bgn != 0) 
    {
        printf((verbose) ? " %s! Wrong uop_bgn: got %d.\n" : "%s\n", FAIL_CHAR, (unsigned)insn->uop_bgn);
        error++;
    }

    if (insn->uop_end != 3) 
    {
        printf((verbose) ? " %s! Wrong uop_end: got %u.\n" : "%s\n", FAIL_CHAR, (unsigned)insn->uop_end);
        error++;
    }

    if (insn->iter_out != 2) 
    {
        printf((verbose) ? " %s! Wrong iter_out: got %u.\n" : "%s\n", FAIL_CHAR, (unsigned)insn->iter_out);
        error++;
    }

    if (insn->iter_in != 4) 
    {
        printf((verbose) ? " %s! Wrong iter_in: got %u.\n" : "%s\n", FAIL_CHAR, (unsigned)insn->iter_in);
        error++;
    }

    if (insn->reset_reg != 0) 
    {
        printf((verbose) ? " %s! Wrong reset_reg: got %u.\n" : "%s\n", FAIL_CHAR, (unsigned)insn->reset_reg);
        error++;
    }

    if (error == 0)
        printf((verbose) ? " %s! GEMM fields encoded correctly.\n" : "%s\n", PASS_CHAR);

    printTestName(verbose, "GEMM label resolution");

    memset(insnBuffer, 0, sizeof(insnBuffer));
    memset(uopBuffer, 0, sizeof(uopBuffer));

    asmCode = 
        "lbl1_bgn:\n"
        "10, 20, 30\n"
        "11, 21, 31\n"
        "12, 22, 32\n"
        "lbl1_end:\n"
        "FOR (2, 4) UOP (lbl1_bgn, lbl1_end)\n"
        "GEMM(ACC[0:3], INP[0:3], WGT[0:3])\n";

    numInsn = 0;
    numUop  = 0;

    status = vtaAssemble(asmCode, insnBuffer, 8, &numInsn, uopBuffer, 8, &numUop);

    if (status != VTA_OK) 
    {
        if (verbose) 
        {
            printf((verbose) ? " %s! vtaAssemble returned status [%d].\n" : "%s\n", FAIL_CHAR, status);
            vtaErrorPrint(status, -1);
        }
        else
            printf("%s\n", FAIL_CHAR);

        return 1;
    }

    if (numInsn != 1) 
    {
        printf((verbose) ? " %s! Expected 1 instruction, got %d.\n" : "%s\n", FAIL_CHAR, numInsn);
        error++;
    }

    if (numUop != 3) 
    {
        printf((verbose) ? " %s! Expected 3 UOPs, got %d.\n" : "%s\n", FAIL_CHAR, numUop);
        error++;
    }

    insn = (VTAGemInsn *)&insnBuffer[0];

    if (insn->opcode != VTA_OPCODE_GEMM) 
    {
        printf((verbose) ? " %s! Wrong opcode: got %u.\n" : "%s\n", FAIL_CHAR, (unsigned)insn->opcode);
        error++;
    }

    if (insn->uop_bgn != 0) 
    {
        printf((verbose) ? " %s! Expected uop_bgn = 0, got %u.\n" : "%s\n", FAIL_CHAR, (unsigned)insn->uop_bgn);
        error++;
    }

    if (insn->uop_end != 3) 
    {
        printf((verbose) ? " %s! Expected uop_end = 3, got %u.\n" : "%s\n", FAIL_CHAR, (unsigned)insn->uop_end);
        error++;
    }

    if (insn->iter_out != 2) 
    {
        printf((verbose) ? " %s! Expected iter_out = 2, got %u.\n" : "%s\n", FAIL_CHAR, (unsigned)insn->iter_out);
        error++;
    }

    if (insn->iter_in != 4) 
    {
        printf((verbose) ? " %s! Expected iter_in = 4, got %u.\n" : "%s\n", FAIL_CHAR, (unsigned)insn->iter_in);
        error++;
    }

    if (error == 0)
        printf((verbose) ? " %s! GEMM labels resolved correctly.\n" : "%s\n", PASS_CHAR);


    printTestName(verbose, "Check GEMM.RST binary encoding");

    memset(insnBuffer, 0, sizeof(insnBuffer));
    memset(uopBuffer, 0, sizeof(uopBuffer));

    asmCode =
        "lbl1_bgn:\n"
        "10, 20, 30\n"
        "11, 21, 31\n"
        "12, 22, 32\n"
        "lbl1_end:\n"
        "FOR (1, 6) UOP (lbl1_bgn, lbl1_end)\n"
        "GEMM.RST(ACC[0:3])\n";

    numInsn = 0;
    numUop  = 0;

    status = vtaAssemble(asmCode, insnBuffer, 8, &numInsn, uopBuffer, 8, &numUop);

    if (status != VTA_OK) 
    {
        if (verbose) 
        {
            printf((verbose) ? " %s! vtaAssemble returned status [%d].\n" : "%s\n", FAIL_CHAR, status);
            vtaErrorPrint(status, -1);
        }
        else
            printf("%s\n", FAIL_CHAR);

        return 1;
    }

    if (numInsn != 1)
    {
        printf((verbose) ? " %s! Expected 1 instruction, got %d.\n" : "%s\n", FAIL_CHAR, numInsn);
        error++;
    }

    if (numUop != 3) 
    {
        printf((verbose) ? " %s! Expected 3 UOPs, got %d.\n" : "%s\n", FAIL_CHAR, numUop);
        error++;
    }

    insn = (VTAGemInsn *)&insnBuffer[0];

    if (insn->opcode != VTA_OPCODE_GEMM) 
    {
        printf((verbose) ? " %s! Wrong opcode: got %u.\n" : "%s\n", FAIL_CHAR, (unsigned)insn->opcode);
        error++;
    }

    if (insn->reset_reg != 1) 
    {
        printf((verbose) ? " %s! Expected reset_reg = 1, got %u.\n" : "%s\n", FAIL_CHAR, (unsigned)insn->reset_reg);
        error++;
    }

    if (insn->uop_bgn != 0) 
    {
        printf((verbose) ? " %s! Expected uop_bgn = 0, got %u.\n" : "%s\n", FAIL_CHAR, (unsigned)insn->uop_bgn);
        error++;
    }

    if (insn->uop_end != 3) 
    {
        printf((verbose) ? " %s! Expected uop_end = 3, got %u.\n" : "%s\n", FAIL_CHAR, (unsigned)insn->uop_end);
        error++;
    }

    if (insn->iter_out != 1) 
    {
        printf((verbose) ? " %s! Expected iter_out = 1, got %u.\n" : "%s\n", FAIL_CHAR, (unsigned)insn->iter_out);
        error++;
    }

    if (insn->iter_in != 6) 
    {
        printf((verbose) ? " %s! Expected iter_in = 6, got %u.\n" : "%s\n", FAIL_CHAR, (unsigned)insn->iter_in);
        error++;
    }

    if (error == 0)
        printf((verbose) ? " %s! GEMM.RST fields encoded correctly.\n" : "%s\n", PASS_CHAR);

    return error;
}


static int aTest_aluEncoding(int verbose)
{
    printHeader("ASSEMBLER", "Check ALU binary encoding");
    
    printTestName(verbose, "Valid ALU instruction");

    int error;
    error = 0;

    const char *opNames[] = {
        "ALU.MIN",
        "ALU.MAX",
        "ALU.ADD",
        "ALU.SHR"
    };

    uint32_t expectedOpcodes[] = {
        VTA_ALU_OPCODE_MIN,
        VTA_ALU_OPCODE_MAX,
        VTA_ALU_OPCODE_ADD,
        VTA_ALU_OPCODE_SHR
    };

    for (int i = 0; i < 4; i++) 
    {
        VTAGenericInsn insnBuffer[8];
        memset(insnBuffer, 0, sizeof(insnBuffer));

        VTAUop uopBuffer[8];
        memset(uopBuffer, 0, sizeof(uopBuffer));

        char asmCode[256];
        snprintf(
            asmCode,
            sizeof(asmCode),
            "lbl1_bgn:\n"
            "10, 20, 30\n"
            "11, 21, 31\n"
            "12, 22, 32\n"
            "lbl1_end:\n"
            "FOR (3, 5) UOP (lbl1_bgn, lbl1_end)\n"
            "%s(DST[0:3], -7)\n",
            opNames[i]
        );

        int numInsn, numUop;
        numInsn = 0;
        numUop = 0;

        VTAErr status;
        status = vtaAssemble(
            asmCode,
            insnBuffer,
            8,
            &numInsn,
            uopBuffer,
            8,
            &numUop
        );

        if (status != VTA_OK) 
        {
            if (verbose)
            {
                printf((verbose) ? " %s! %s returned status [%d].\n" : "%s\n", FAIL_CHAR, opNames[i], status);
                vtaErrorPrint(status, -1);
            }
            else
                printf("%s\n", FAIL_CHAR);

            error++;
            continue;
        }

        VTAAluInsn *insn = (VTAAluInsn *)&insnBuffer[0];

        if (numInsn != 1) 
        {
            printf((verbose) ? " %s! %s: expected 1 instruction, got %d.\n" : "%s\n", FAIL_CHAR, opNames[i], numInsn);
            error++;
        }

        if (numUop != 3) 
        {
            printf((verbose) ? " %s! %s: expected 3 UOPs, got %d.\n" : "%s\n", FAIL_CHAR, opNames[i], numUop);
            error++;
        }

        if (insn->opcode != VTA_OPCODE_ALU) 
        {
            printf((verbose) ? " %s! %s: wrong opcode: got %u.\n" : "%s\n", FAIL_CHAR, opNames[i], (unsigned)insn->opcode);
            error++;
        }

        if (insn->alu_opcode != expectedOpcodes[i]) 
        {
            printf(
                (verbose) ? " %s! %s: wrong ALU opcode: got %u, expected %u.\n" : "%s\n", FAIL_CHAR,
                opNames[i],
                (unsigned)insn->alu_opcode,
                (unsigned)expectedOpcodes[i]
            );
            error++;
        }

        if (insn->uop_bgn != 0 || insn->uop_end != 3) 
        {
            printf(
                (verbose) ? " %s! %s: wrong UOP range [%u:%u].\n" : "%s\n", FAIL_CHAR,
                opNames[i],
                (unsigned)insn->uop_bgn,
                (unsigned)insn->uop_end
            );
            error++;
        }

        if (insn->iter_out != 3 || insn->iter_in != 5) 
        {
            printf(
                (verbose) ? " %s! %s: wrong iterators (%u, %u).\n" : "%s\n", FAIL_CHAR,
                opNames[i],
                (unsigned)insn->iter_out,
                (unsigned)insn->iter_in
            );
            error++;
        }

        if (insn->reset_reg != 0) 
        {
            printf((verbose) ? " %s! %s: reset_reg should be 0, got %u.\n" : "%s\n", FAIL_CHAR, opNames[i], (unsigned)insn->reset_reg);
            error++;
        }

        if (insn->use_imm != 1) 
        {
            printf((verbose) ? " %s! %s: expected immediate mode.\n" : "%s\n", FAIL_CHAR, opNames[i]);
            error++;
        }

        if (insn->imm != -7) 
        {
            printf((verbose) ? " %s! %s: wrong immediate: got %d.\n" : "%s\n", FAIL_CHAR, opNames[i], (int)insn->imm);
            error++;
        }
    }

    if (error == 0)
        printf((verbose) ? " %s! All ALU opcodes encoded correctly.\n" : "%s\n", PASS_CHAR);

    return error;
}


static int aTest_aluSrcEncoding(int verbose)
{
    printHeader("ASSEMBLER", "Check ALU SRC encoding");

    int error;
    error = 0;

    VTAGenericInsn insnBuffer[8];
    memset(insnBuffer, 0, sizeof(insnBuffer));

    VTAUop uopBuffer[8];
    memset(uopBuffer, 0, sizeof(uopBuffer));

    const char *asmCode;
    asmCode =
        "lbl1_bgn:\n"
        "10, 20, 30\n"
        "11, 21, 31\n"
        "12, 22, 32\n"
        "lbl1_end:\n"
        "FOR (2, 4) UOP (lbl1_bgn, lbl1_end)\n"
        "ALU.ADD(DST[0:3], SRC[0:3])\n";

    int numInsn, numUop;
    numInsn = 0;
    numUop = 0;

    VTAErr status;
    status = vtaAssemble(
        asmCode,
        insnBuffer,
        8,
        &numInsn,
        uopBuffer,
        8,
        &numUop
    );

    if (status != VTA_OK) 
    {
        printf((verbose) ? " %s! ALU.ADD SRC returned status [%d].\n" : "%s\n", FAIL_CHAR, status);
        vtaErrorPrint(status, -1);
        return 1;
    }

    VTAAluInsn *insn = (VTAAluInsn *)&insnBuffer[0];

    if (numInsn != 1) 
    {
        printf((verbose) ? " %s! Expected 1 instruction, got %d.\n" : "%s\n", FAIL_CHAR, numInsn);
        error++;
    }

    if (numUop != 3) 
    {
        printf((verbose) ? " %s! Expected 3 UOPs, got %d.\n" : "%s\n", FAIL_CHAR, numUop);
        error++;
    }

    if (insn->opcode != VTA_OPCODE_ALU) 
    {
        printf((verbose) ? " %s! Wrong opcode: got %u.\n" : "%s\n", FAIL_CHAR, (unsigned)insn->opcode);
        error++;
    }

    if (insn->alu_opcode != VTA_ALU_OPCODE_ADD) 
    {
        printf((verbose) ? " %s! Wrong ALU opcode: got %u.\n" : "%s\n", FAIL_CHAR, (unsigned)insn->alu_opcode);
        error++;
    }

    if (insn->uop_bgn != 0 || insn->uop_end != 3) 
    {
        printf(
            (verbose) ? " %s! Wrong UOP range [%u:%u].\n" : "%s\n", FAIL_CHAR,
            (unsigned)insn->uop_bgn,
            (unsigned)insn->uop_end
        );
        error++;
    }

    if (insn->iter_out != 2 || insn->iter_in != 4) 
    {
        printf(
            (verbose) ? " %s! Wrong iterators (%u, %u).\n" : "%s\n", FAIL_CHAR,
            (unsigned)insn->iter_out,
            (unsigned)insn->iter_in
        );
        error++;
    }

    if (insn->use_imm != 0) 
    {
        printf((verbose) ? " %s! Expected SRC mode, but use_imm is %u.\n" : "%s\n", FAIL_CHAR, (unsigned)insn->use_imm);
        error++;
    }

    if (error == 0)
        printf((verbose) ? " %s! ALU SRC fields encoded correctly.\n" : "%s\n", PASS_CHAR);

    return error;
}


static int aTest_dependencyEncoding(int verbose)
{
    printHeader("ASSEMBLER", "Check POP / PUSH dependency encoding");

    int error;
    error = 0;

    VTAGenericInsn insnBuffer[8];
    memset(insnBuffer, 0, sizeof(insnBuffer));

    VTAUop uopBuffer[8];
    memset(uopBuffer, 0, sizeof(uopBuffer));

    /*
     * 17.1 LOAD
     *
     * POP  EX->LD  -> pop_prev_dep
     * PUSH LD->EX  -> push_prev_dep
     */
    printTestName(verbose, "Dependencies on LOAD");

    const char *loadCode;
    loadCode =
        "POP (EX->LD)\n"
        "LOAD(INP[4], MEM[100, 2, 8, 16])\n"
        "PUSH (LD->EX)\n";

    int numInsn, numUop;
    numInsn = 0;
    numUop  = 0;

    VTAErr status;
    status = vtaAssemble(
        loadCode,
        insnBuffer,
        8,
        &numInsn,
        uopBuffer,
        8,
        &numUop
    );

    if (status != VTA_OK) 
    {
        if (verbose) 
        {
            printf((verbose) ? " %s! LOAD dependency encoding returned %d.\n" : "%s\n", FAIL_CHAR, status);
            vtaErrorPrint(status, -1);
        }
        else
            printf("%s\n", FAIL_CHAR);

        return 1;
    }

    VTAMemInsn *load;
    load = (VTAMemInsn *)&insnBuffer[0];

    if (load->pop_prev_dep != 1) 
    {
        printf((verbose) ? " %s! LOAD pop_prev_dep expected 1, got %u.\n" : "%s\n", FAIL_CHAR,
               (unsigned)load->pop_prev_dep);
        error++;
    }

    if (load->pop_next_dep != 0) 
    {
        printf((verbose) ? " %s! LOAD pop_next_dep expected 0, got %u.\n" : "%s\n", FAIL_CHAR,
               (unsigned)load->pop_next_dep);
        error++;
    }

    if (load->push_prev_dep != 1) 
    {
        printf((verbose) ? " %s! LOAD push_prev_dep expected 1, got %u.\n" : "%s\n", FAIL_CHAR,
               (unsigned)load->push_prev_dep);
        error++;
    }

    if (load->push_next_dep != 0) 
    {
        printf((verbose) ? " %s! LOAD push_next_dep expected 0, got %u.\n" : "%s\n", FAIL_CHAR,
               (unsigned)load->push_next_dep);
        error++;
    }

    /*
     * 17.2 STORE
     *
     * POP  EX->ST  -> pop_prev_dep
     * PUSH ST->EX  -> push_prev_dep
     */
    printTestName(verbose, "Dependencies on STORE");

    memset(insnBuffer, 0, sizeof(insnBuffer));
    memset(uopBuffer, 0, sizeof(uopBuffer));

    const char *storeCode;
    storeCode =
        "POP (EX->ST)\n"
        "STORE(MEM[200, 2, 8, 16], ACC[5])\n"
        "PUSH (ST->EX)\n";

    numInsn = 0;
    numUop  = 0;

    status = vtaAssemble(
        storeCode,
        insnBuffer,
        8,
        &numInsn,
        uopBuffer,
        8,
        &numUop
    );

    if (status != VTA_OK) 
    {
        if (verbose) 
        {
            printf((verbose) ? " %s! STORE dependency encoding returned %d.\n" : "%s\n", FAIL_CHAR, status);
            vtaErrorPrint(status, -1);
        }
        else
            printf("%s\n", FAIL_CHAR);

        return 1;
    }

    VTAMemInsn *store;
    store = (VTAMemInsn *)&insnBuffer[0];

    if (store->pop_prev_dep != 1) 
    {
        printf((verbose) ? " %s! STORE pop_prev_dep expected 1, got %u.\n" : "%s\n", FAIL_CHAR,
               (unsigned)store->pop_prev_dep);
        error++;
    }

    if (store->pop_next_dep != 0) 
    {
        printf((verbose) ? " %s! STORE pop_next_dep expected 0, got %u.\n" : "%s\n", FAIL_CHAR,
               (unsigned)store->pop_next_dep);
        error++;
    }

    if (store->push_prev_dep != 1) 
    {
        printf((verbose) ? " %s! STORE push_prev_dep expected 1, got %u.\n" : "%s\n", FAIL_CHAR,
               (unsigned)store->push_prev_dep);
        error++;
    }

    if (store->push_next_dep != 0) 
    {
        printf((verbose) ? " %s! STORE push_next_dep expected 0, got %u.\n" : "%s\n", FAIL_CHAR,
               (unsigned)store->push_next_dep);
        error++;
    }

    /*
     * 17.3 GEMM
     *
     * POP:
     *   LD->EX -> pop_prev_dep
     *   ST->EX -> pop_next_dep
     *
     * PUSH:
     *   EX->LD -> push_prev_dep
     *   EX->ST -> push_next_dep
     */
    printTestName(verbose, "Dependencies on GEMM");
    
    memset(insnBuffer, 0, sizeof(insnBuffer));
    memset(uopBuffer, 0, sizeof(uopBuffer));

    const char *gemmCode;
    gemmCode =
        "lbl_bgn:\n"
        "10, 20, 30\n"
        "11, 21, 31\n"
        "12, 22, 32\n"
        "lbl_end:\n"
        "POP (LD->EX, ST->EX)\n"
        "FOR (2, 4) UOP (lbl_bgn, lbl_end)\n"
        "GEMM(ACC[0:3], INP[0:3], WGT[0:3])\n"
        "PUSH (EX->LD, EX->ST)\n";

    numInsn = 0;
    numUop  = 0;

    status = vtaAssemble(
        gemmCode,
        insnBuffer,
        8,
        &numInsn,
        uopBuffer,
        8,
        &numUop
    );

    if (status != VTA_OK) 
    {
        if (verbose)
        {
            printf((verbose) ? " %s! GEMM dependency encoding returned %d.\n" : "%s\n", FAIL_CHAR, status);
            vtaErrorPrint(status, -1);
        }

        return 1;
    }

    VTAGemInsn *gemm;
    gemm = (VTAGemInsn *)&insnBuffer[0];

    if (gemm->pop_prev_dep != 1) 
    {
        printf((verbose) ? " %s! GEMM pop_prev_dep expected 1, got %u.\n" : "%s\n", FAIL_CHAR,
               (unsigned)gemm->pop_prev_dep);
        error++;
    }

    if (gemm->pop_next_dep != 1) 
    {
        printf((verbose) ? " %s! GEMM pop_next_dep expected 1, got %u.\n" : "%s\n", FAIL_CHAR,
               (unsigned)gemm->pop_next_dep);
        error++;
    }

    if (gemm->push_prev_dep != 1) 
    {
        printf((verbose) ? " %s! GEMM push_prev_dep expected 1, got %u.\n" : "%s\n", FAIL_CHAR,
               (unsigned)gemm->push_prev_dep);
        error++;
    }

    if (gemm->push_next_dep != 1) 
    {
        printf((verbose) ? " %s! GEMM push_next_dep expected 1, got %u.\n" : "%s\n", FAIL_CHAR,
               (unsigned)gemm->push_next_dep);
        error++;
    }

    printTestName(verbose, "Dependencies on ALU");

    memset(insnBuffer, 0, sizeof(insnBuffer));
    memset(uopBuffer, 0, sizeof(uopBuffer));

    const char *aluCode;
    aluCode =
        "lbl_bgn:\n"
        "10, 20, 30\n"
        "11, 21, 31\n"
        "12, 22, 32\n"
        "lbl_end:\n"
        "POP (LD->EX, ST->EX)\n"
        "FOR (2, 4) UOP (lbl_bgn, lbl_end)\n"
        "ALU.ADD(DST[0:3], 7)\n"
        "PUSH (EX->LD, EX->ST)\n";

    numInsn = 0;
    numUop  = 0;

    status = vtaAssemble(
        aluCode,
        insnBuffer,
        8,
        &numInsn,
        uopBuffer,
        8,
        &numUop
    );

    if (status != VTA_OK) 
    {
        if (verbose) 
        {
            printf((verbose) ? " %s! ALU dependency encoding returned %d.\n" : "%s\n", FAIL_CHAR, status);
            vtaErrorPrint(status, -1);
        }
        else
            printf("%s\n", FAIL_CHAR);

        return 1;
    }

    VTAAluInsn *alu;
    alu = (VTAAluInsn *)&insnBuffer[0];

    if (alu->pop_prev_dep != 1) 
    {
        printf((verbose) ? " %s! ALU pop_prev_dep expected 1, got %u.\n" : "%s\n", FAIL_CHAR,
               (unsigned)alu->pop_prev_dep);
        error++;
    }

    if (alu->pop_next_dep != 1) 
    {
        printf((verbose) ? " %s! ALU pop_next_dep expected 1, got %u.\n" : "%s\n", FAIL_CHAR,
               (unsigned)alu->pop_next_dep);
        error++;
    }

    if (alu->push_prev_dep != 1) 
    {
        printf((verbose) ? " %s! ALU push_prev_dep expected 1, got %u.\n" : "%s\n", FAIL_CHAR,
               (unsigned)alu->push_prev_dep);
        error++;
    }

    if (alu->push_next_dep != 1) 
    {
        printf((verbose) ? " %s! ALU push_next_dep expected 1, got %u.\n" : "%s\n", FAIL_CHAR,
               (unsigned)alu->push_next_dep);
        error++;
    }

    if (error == 0)
        printf((verbose) ? " %s! POP/PUSH dependencies encoded correctly.\n" : "%s\n", PASS_CHAR);

    return error;
}


/*-----------
    ROUND-TRIP TESTS
*/


static int rtTest_roundtripLoad(int verbose)
{
    printHeader("ROUND-TRIP", "Check LOAD assemble / disassemble");

    int error;
    error = 0;

    VTAGenericInsn firstInsnBuffer[8];
    VTAGenericInsn secondInsnBuffer[8];

    VTAUop firstUopBuffer[8];
    VTAUop secondUopBuffer[8];

    memset(firstInsnBuffer, 0, sizeof(firstInsnBuffer));
    memset(secondInsnBuffer, 0, sizeof(secondInsnBuffer));
    memset(firstUopBuffer, 0, sizeof(firstUopBuffer));
    memset(secondUopBuffer, 0, sizeof(secondUopBuffer));

    const char *asmCode;
    asmCode =
        "POP (EX->LD)\n"
        "LOAD(INP[4], MEM[100, 2, 8, 16])\n"
        "PUSH (LD->EX)\n";

    int firstNumInsn;
    int firstNumUop;

    firstNumInsn = 0;
    firstNumUop = 0;

    VTAErr status;
    status = vtaAssemble(
        asmCode,
        firstInsnBuffer,
        8,
        &firstNumInsn,
        firstUopBuffer,
        8,
        &firstNumUop
    );

    if (status != VTA_OK) 
    {
        if (verbose)
        {
            printf((verbose) ? " %s! First vtaAssemble returned status [%d].\n" : "%s\n", FAIL_CHAR, status); 
            vtaErrorPrint(status, -1);
        }
        else
            printf("%s\n", FAIL_CHAR);

        return 1;
    }

    char disassembled[OUT_BUF_SIZE];
    memset(disassembled, 0, sizeof(disassembled));

    status = vtaDisassemble(
        firstInsnBuffer,
        firstNumInsn,
        firstUopBuffer,
        firstNumUop,
        disassembled,
        sizeof(disassembled)
    );

    if (status != VTA_OK) 
    {
        if (verbose) 
        {
            printf((verbose) ? " %s! vtaDisassemble returned status [%d].\n" : "%s\n", FAIL_CHAR, status);
            vtaErrorPrint(status, -1);
        }
        else
            printf("%s\n", FAIL_CHAR);

        return 1;
    }

    printf((verbose) ? " Generated assembly:\n%s\n" : "", disassembled);

    int secondNumInsn;
    int secondNumUop;

    secondNumInsn = 0;
    secondNumUop = 0;

    status = vtaAssemble(
        disassembled,
        secondInsnBuffer,
        8,
        &secondNumInsn,
        secondUopBuffer,
        8,
        &secondNumUop
    );

    if (status != VTA_OK) 
    {
        if (verbose)
        {
            printf((verbose) ? " %s! Second vtaAssemble returned status [%d].\n" : "%s\n", FAIL_CHAR, status);
            vtaErrorPrint(status, -1);
        }
        else
            printf("%s\n", FAIL_CHAR);

        return 1;
    }

    if (firstNumInsn != secondNumInsn) 
    {
        printf((verbose) ? " %s! Instruction count changed: %d -> %d.\n" : "%s\n", FAIL_CHAR, firstNumInsn, secondNumInsn);
        error++;
    }

    if (firstNumUop != secondNumUop) 
    {
        printf((verbose) ? " %s! UOP count changed: %d -> %d.\n" : "%s\n", FAIL_CHAR, firstNumUop, secondNumUop);
        error++;
    }

    VTAMemInsn *firstInsn;
    VTAMemInsn *secondInsn;

    firstInsn = (VTAMemInsn *)&firstInsnBuffer[0];
    secondInsn = (VTAMemInsn *)&secondInsnBuffer[0];

    if (firstInsn->opcode != secondInsn->opcode) 
    {
        printf((verbose) ? " %s! Opcode changed.\n" : "%s\n", FAIL_CHAR);
        error++;
    }

    if (firstInsn->memory_type != secondInsn->memory_type) 
    {
        printf((verbose) ? " %s! Memory type changed.\n" : "%s\n", FAIL_CHAR);
        error++;
    }

    if (firstInsn->sram_base != secondInsn->sram_base) 
    {
        printf((verbose) ? " %s! SRAM base changed.\n" : "%s\n", FAIL_CHAR);
        error++;
    }

    if (firstInsn->dram_base != secondInsn->dram_base) 
    {
        printf((verbose) ? " %s! DRAM base changed.\n" : "%s\n", FAIL_CHAR);
        error++;
    }

    if (firstInsn->y_size != secondInsn->y_size) 
    {
        printf((verbose) ? " %s! Y size changed.\n" : "%s\n", FAIL_CHAR);
        error++;
    }

    if (firstInsn->x_size != secondInsn->x_size) 
    {
        printf((verbose) ? " %s! X size changed.\n" : "%s\n", FAIL_CHAR);
        error++;
    }

    if (firstInsn->x_stride != secondInsn->x_stride) 
    {
        printf((verbose) ? " %s! X stride changed.\n" : "%s\n", FAIL_CHAR);
        error++;
    }

    if (firstInsn->pop_prev_dep != secondInsn->pop_prev_dep) 
    {
        printf((verbose) ? " %s! POP dependency changed.\n" : "%s\n", FAIL_CHAR);
        error++;
    }

    if (firstInsn->push_prev_dep != secondInsn->push_prev_dep) 
    {
        printf((verbose) ? " %s! PUSH dependency changed.\n" : "%s\n", FAIL_CHAR);
        error++;
    }

    if (error == 0)
        printf((verbose) ? " %s! LOAD round-trip preserved the instruction.\n" : "%s\n", PASS_CHAR);

    return error;
}


static int rtTest_roundtripGemm(int verbose)
{
    printHeader("ROUND-TRIP", "Check GEMM assemble / disassemble");

    int error;
    error = 0;

    VTAGenericInsn firstInsnBuffer[8];
    VTAGenericInsn secondInsnBuffer[8];

    VTAUop firstUopBuffer[8];
    VTAUop secondUopBuffer[8];

    memset(firstInsnBuffer, 0, sizeof(firstInsnBuffer));
    memset(secondInsnBuffer, 0, sizeof(secondInsnBuffer));
    memset(firstUopBuffer, 0, sizeof(firstUopBuffer));
    memset(secondUopBuffer, 0, sizeof(secondUopBuffer));

    /*
     * Start with 3 UOPs and one GEMM using them.
     * Factor fields remain zero because the current disassembler
     * does not emit them explicitly.
     */
    const char *asmCode;
    asmCode =
        "lbl1_bgn:\n"
        "10, 20, 30\n"
        "11, 21, 31\n"
        "12, 22, 32\n"
        "lbl1_end:\n"
        "\n"
        "POP (LD->EX, ST->EX)\n"
        "FOR (2, 4) UOP (lbl1_bgn, lbl1_end)\n"
        "GEMM(ACC[0:3], INP[0:3], WGT[0:3])\n"
        "PUSH (EX->LD, EX->ST)\n";

    int firstNumInsn;
    int firstNumUop;

    firstNumInsn = 0;
    firstNumUop = 0;

    VTAErr status;
    status = vtaAssemble(
        asmCode,
        firstInsnBuffer,
        8,
        &firstNumInsn,
        firstUopBuffer,
        8,
        &firstNumUop
    );

    if (status != VTA_OK) 
    {
        if (verbose)
        {
            printf((verbose) ? " %s! First vtaAssemble returned status [%d].\n" : "%s\n", FAIL_CHAR, status);
            vtaErrorPrint(status, -1);
        }
        else
            printf("%s\n", FAIL_CHAR);

        return 1;
    }

    if (firstNumInsn != 1) 
    {
        printf((verbose) ? " %s! Expected 1 instruction, got %d.\n" : "%s\n", FAIL_CHAR, firstNumInsn);
        error++;
    }

    if (firstNumUop != 3) 
    {
        printf((verbose) ? " %s! Expected 3 UOPs, got %d.\n" : "%s\n", FAIL_CHAR, firstNumUop);
        error++;
    }

    char disassembled[OUT_BUF_SIZE];
    memset(disassembled, 0, sizeof(disassembled));

    status = vtaDisassemble(
        firstInsnBuffer,
        firstNumInsn,
        firstUopBuffer,
        firstNumUop,
        disassembled,
        sizeof(disassembled)
    );

    if (status != VTA_OK) 
    {
        if (verbose)
        {
            printf((verbose) ? " %s! vtaDisassemble returned status [%d].\n" : "%s\n", FAIL_CHAR, status);
            vtaErrorPrint(status, -1);
        }
        else
            printf("%s\n", FAIL_CHAR);

        return 1;
    }

    printf((verbose) ? " Generated assembly:\n%s\n" : "", disassembled);

    int secondNumInsn;
    int secondNumUop;

    secondNumInsn = 0;
    secondNumUop = 0;

    status = vtaAssemble(
        disassembled,
        secondInsnBuffer,
        8,
        &secondNumInsn,
        secondUopBuffer,
        8,
        &secondNumUop
    );

    if (status != VTA_OK) 
    {
        if (verbose)
        {
            printf((verbose) ? " %s! Second vtaAssemble returned status [%d].\n" : "%s\n", FAIL_CHAR, status);
            vtaErrorPrint(status, -1);
        }
        else
            printf("%s\n", FAIL_CHAR);

        return 1;
    }

    if (firstNumInsn != secondNumInsn) 
    {
        printf((verbose) ? " %s! Instruction count changed: %d -> %d.\n" : "%s\n", FAIL_CHAR, firstNumInsn, secondNumInsn);
        error++;
    }

    if (firstNumUop != secondNumUop) 
    {
        printf((verbose) ? " %s! UOP count changed: %d -> %d.\n" : "%s\n", FAIL_CHAR, firstNumUop, secondNumUop);
        error++;
    }

    /*
     * Compare the GEMM instruction.
     */
    VTAGemInsn *firstGemm;
    VTAGemInsn *secondGemm;

    firstGemm = (VTAGemInsn *)&firstInsnBuffer[0];
    secondGemm = (VTAGemInsn *)&secondInsnBuffer[0];

    if (firstGemm->opcode != secondGemm->opcode) 
    {
        printf((verbose) ? " %s! Opcode changed.\n" : "%s\n", FAIL_CHAR);
        error++;
    }

    if (firstGemm->reset_reg != secondGemm->reset_reg) 
    {
        printf((verbose) ? " %s! reset_reg changed.\n" : "%s\n", FAIL_CHAR);
        error++;
    }

    if (firstGemm->uop_bgn != secondGemm->uop_bgn) 
    {
        printf((verbose) ? " %s! uop_bgn changed.\n" : "%s\n", FAIL_CHAR);
        error++;
    }

    if (firstGemm->uop_end != secondGemm->uop_end) 
    {
        printf((verbose) ? " %s! uop_end changed.\n" : "%s\n", FAIL_CHAR);
        error++;
    }

    if (firstGemm->iter_out != secondGemm->iter_out) 
    {
        printf((verbose) ? " %s! iter_out changed.\n" : "%s\n", FAIL_CHAR);
        error++;
    }

    if (firstGemm->iter_in != secondGemm->iter_in) 
    {
        printf((verbose) ? " %s! iter_in changed.\n" : "%s\n", FAIL_CHAR);
        error++;
    }

    if (firstGemm->dst_factor_out != secondGemm->dst_factor_out) 
    {
        printf((verbose) ? " %s! dst_factor_out changed.\n" : "%s\n", FAIL_CHAR);
        error++;
    }

    if (firstGemm->src_factor_out != secondGemm->src_factor_out) 
    {
        printf((verbose) ? " %s! src_factor_out changed.\n" : "%s\n", FAIL_CHAR);
        error++;
    }

    if (firstGemm->wgt_factor_out != secondGemm->wgt_factor_out) 
    {
        printf((verbose) ? " %s! wgt_factor_out changed.\n" : "%s\n", FAIL_CHAR);
        error++;
    }

    if (firstGemm->pop_prev_dep != secondGemm->pop_prev_dep) 
    {
        printf((verbose) ? " %s! pop_prev_dep changed.\n" : "%s\n", FAIL_CHAR);
        error++;
    }

    if (firstGemm->pop_next_dep != secondGemm->pop_next_dep) 
    {
        printf((verbose) ? " %s! pop_next_dep changed.\n" : "%s\n", FAIL_CHAR);
        error++;
    }

    if (firstGemm->push_prev_dep != secondGemm->push_prev_dep) 
    {
        printf((verbose) ? " %s! push_prev_dep changed.\n" : "%s\n", FAIL_CHAR);
        error++;
    }

    if (firstGemm->push_next_dep != secondGemm->push_next_dep) 
    {
        printf((verbose) ? " %s! push_next_dep changed.\n" : "%s\n", FAIL_CHAR);
        error++;
    }

    /*
     * Compare all UOPs.
     */
    for (int i = 0; i < firstNumUop && i < secondNumUop; i++) 
    {
        if (firstUopBuffer[i].dst_idx != secondUopBuffer[i].dst_idx) 
        {
            printf((verbose) ? " %s! UOP[%d] dst_idx changed: %u -> %u.\n" : "%s\n", FAIL_CHAR,
                i,
                (unsigned)firstUopBuffer[i].dst_idx,
                (unsigned)secondUopBuffer[i].dst_idx
            );
            error++;
        }

        if (firstUopBuffer[i].src_idx != secondUopBuffer[i].src_idx) 
        {
            printf((verbose) ? " %s! UOP[%d] src_idx changed: %u -> %u.\n" : "%s\n", FAIL_CHAR,
                i,
                (unsigned)firstUopBuffer[i].src_idx,
                (unsigned)secondUopBuffer[i].src_idx
            );
            error++;
        }

        if (firstUopBuffer[i].wgt_idx != secondUopBuffer[i].wgt_idx) 
        {
            printf((verbose) ? " %s! UOP[%d] wgt_idx changed: %u -> %u.\n" : "%s\n", FAIL_CHAR,
                i,
                (unsigned)firstUopBuffer[i].wgt_idx,
                (unsigned)secondUopBuffer[i].wgt_idx
            );
            error++;
        }
    }

    if (error == 0)
        printf((verbose) ? " %s! GEMM round-trip preserved instruction and UOPs.\n" : "%s\n", PASS_CHAR);

    return error;
}






/* ---------
    TEST RUNNERS
*/

static void disassembler_runTests(int *error, int verbose) 
{
    *error += dTest_nullPointers(verbose);
    *error += dTest_invalidOPCode(verbose);
    *error += dTest_uopOutOfBounds(verbose);
    *error += dTest_nullUopBuffer(verbose);
    *error += dTest_labelReuse(verbose);
    *error += dTest_outputBuffer(verbose);
    *error += dTest_uopBufferSize(verbose);
    *error += dTest_finish(verbose);
    *error += dTest_noop(verbose);
    *error += dTest_load(verbose);
    *error += dTest_store(verbose);
    *error += dTest_alu(verbose);
    *error += dTest_gemm(verbose);
    *error += dTest_dependencies(verbose);
}


static void assembler_runTests(int *error, int verbose) 
{
    *error += aTest_uopDetection(verbose);
    *error += aTest_invalidUop(verbose);
    *error += aTest_simpleInstructions(verbose);
    *error += aTest_loadParsing(verbose);
    *error += aTest_loadEncoding(verbose);
    *error += aTest_uopEncoding(verbose);
    *error += aTest_storeEncoding(verbose);
    *error += aTest_gemmEnconding(verbose);
    *error += aTest_aluEncoding(verbose);
    *error += aTest_aluSrcEncoding(verbose);
    *error += aTest_dependencyEncoding(verbose);
}


static void roundTrip_runTests(int *error, int verbose) 
{
    *error += rtTest_roundtripLoad(verbose);
    *error += rtTest_roundtripGemm(verbose);
}


int main(void) 
{
    int verbose, error;
    verbose = 0;
    error   = 0;

    disassembler_runTests(&error, verbose);
    assembler_runTests(&error, verbose);
    roundTrip_runTests(&error, verbose);

    if (error > 0)
        printf("\nX - NOT ALL TESTS HAVE PASSED (%d failures).\n\n", error);
    else
        printf("\n✔ - ALL TEST HAVE BEEN SUCCESSFULLY HANDLED!\n\n");

    printf(">> TEST SUITE COMPLETED.\n\n");
    return 0;
}
