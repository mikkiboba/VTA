#include <stdio.h>
#include <vta/hw_spec.h>
#include <string.h>

#include "disassmbler.h"


static void printHeader(const char* testName) {
    printf("\n----\n");
    printf(" Stress test: %s", testName);
    printf("\n----\n");
}


void checkStatus(VTAErr status, VTAErr expectedStatus, int* errorCount) {
    if (status == expectedStatus) {
        if (status != VTA_OK) errorPrint(status);
        printf(" PASS! Crashes avoided and correct error returned.\n\n");
    } else {
        printf(" FAIL! Expected status: [%d] but got [%d].\n\n", expectedStatus, status);
        *errorCount += 1;
    }
}


int test_nullPointers(void) {
    printHeader("1. Check on NULL pointers and numInsn <= 0.");

    int error;
    error = 0;

    VTAErr status;

    printf(" 1.1 - Passing insnBuffer = NULL, numInsn = 5\n");
    status = disassemble(NULL, 5, NULL);
    checkStatus(status, VTA_ERR_NULLPTR, &error);

    printf(" 1.2 - Passing numInsn = 0\n");
    VTAGenericInsn testInsn;
    status = disassemble(&testInsn, 0, NULL);
    checkStatus(status, VTA_ERR_INVALID_INSN_SIZE, &error);

    printf(" 1.3 - Passing numInsn = -5\n");
    status = disassemble(&testInsn, -5, NULL);
    checkStatus(status, VTA_ERR_INVALID_INSN_SIZE, &error);
}


int test_invalidOPCode(void) {
    printHeader("2. Check on invalid/unknown OPCodes.");

    int error;
    error = 0;

    VTAErr status;

    VTAGenericInsn testInsns;
    testInsns.opcode = 7;

    printf(" 2.1 - Passing instruction with unknown opcode (%d)\n", testInsns.opcode);
    status = disassemble(&testInsns, 1, NULL);
    checkStatus(status, VTA_ERR_INVALID_OPCODE, &error);

    return error;
}


int test_uopOutOfBounds(void) {
    printHeader("3. Check on if UOP indices are out of bounds.");

    int error;
    error = 0;

    VTAErr status;

    VTAGemInsn testGemmInsn;
    VTAGenericInsn* insn;
    insn = (VTAGenericInsn*)&testGemmInsn;

    insn->opcode = VTA_OPCODE_GEMM;
    testGemmInsn.uop_bgn = 0;
    testGemmInsn.uop_end = VTA_ACC_BUFF_DEPTH + 1;

    printf(" 3.1 - Passing GEMM instruction with uop_end (%d) > VTA_UOP_BUFF_DEPTH (%d)\n", testGemmInsn.uop_end, VTA_INP_BUFF_DEPTH);
    status = disassemble(insn, 1, NULL);
    checkStatus(status, VTA_ERR_UOP_OOB, &error);

    testGemmInsn.uop_bgn = 10;
    testGemmInsn.uop_end = 9;

    printf(" 3.2 - Passing GEMM instruction with uop_bgn (%d) > uop_end (%d)\n", testGemmInsn.uop_bgn, testGemmInsn.uop_end);
    status = disassemble(insn, 1, NULL);
    checkStatus(status, VTA_ERR_UOP_OOB, &error);

    return error;
}


int test_nullUopBuffer(void) {
    printHeader("4. Check on GEMM/ALU instructions with uopBuffer = NULL.");

    int error;
    error = 0;

    VTAErr status;


    VTAGemInsn testGemmInsn;
    memset(&testGemmInsn, 0, sizeof(testGemmInsn));
    VTAGenericInsn* insn;
    insn = (VTAGenericInsn*)&testGemmInsn;

    insn->opcode = VTA_OPCODE_GEMM;
    testGemmInsn.uop_bgn = 0;
    testGemmInsn.uop_end = 4;
    testGemmInsn.iter_out = 1;
    testGemmInsn.iter_in = 1;

    printf(" 4.1 - Disassembling GEMM with uopBuffer = NULL\n");
    status = disassemble(insn, 1, NULL);
    checkStatus(status, VTA_OK, &error);

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

    if (error > 0)
        printf("X - NOT ALL TESTS HAVE PASSED (%d failures).\n", error);
    else
        printf("✔ - ALL TEST HAVE BEEN SUCCESSFULLY HANDLED!\n");

    return 0;
}