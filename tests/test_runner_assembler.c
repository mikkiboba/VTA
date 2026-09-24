#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include <vta/hw_spec.h>

#include "vta.h"


#define OUT_BUF_SIZE 2048


static void printHeaderA(const char *testName) {
    printf("\n----\n");
    printf(" [ASSEMBLER]: %s", testName);
    printf("\n----\n");
}


static int test_uopDetection(void) {
    printHeaderA("1. Check assembler UOP detection.");

    int error;
    error = 0;

    VTAGenericInsn  insnBuffer[8];
    VTAUop          uopBuffer[8];

    memset(insnBuffer, 0, sizeof(insnBuffer));
    memset(uopBuffer, 0, sizeof(uopBuffer));

    const char *asmCode;
    asmCode =
        "LOAD(BUF[0], MEM[100, 2, 8, 16])\n"
        "10, 20, 30\n"
        "FINISH\n";

    int numInsn, numUop;
    numInsn = 0;
    numUop  = 0;

    VTAErr status; 
    status = vtaAssemble(asmCode, insnBuffer, 8, &numInsn, uopBuffer, 8, &numUop);

    if (status != VTA_OK) {
        printf(" FAIL! vtaAssemble(...) returned status [%d].\n\n", status);
        vtaErrorPrint(status, -1);
        return 1;
    }

    if (numInsn != 2) {
        printf(" FAIL! Expected 2 instructions, got %d.\n\n", numInsn);
        return 1;
    }

    if (numUop != 1) {
        printf(" FAIL! Expected 1 uop, got %d.\n\n", numUop);
        return 1;
    }

    if (error == 0) {
        printf(" PASS! Uop detection is independent from instruction params.\n\n");
    }

    return error;
}


void executeTestsAssembler(int *error) {
    *error += test_uopDetection();
}


int _executeA(void) {
    int error;
    error = 0;

    printf("[ASSEMBLER] STRESS TEST FOR VTA ASSEMBLER.");

    error += test_uopDetection();

    if (error > 0)
        printf("X - NOT ALL TESTS HAVE PASSED (%d failures).\n", error);
    else
        printf("✔ - ALL TEST HAVE BEEN SUCCESSFULLY HANDLED!\n");

    return error;
}