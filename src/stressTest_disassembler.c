#include <stdio.h>
#include <vta/hw_spec.h>

void disassemble(const VTAGenericInsn* insnBuffer, int numInsn, const VTAUop* uopBuffer);


static void printHeader(const char* testName) {
    printf("\n----\n");
    printf(" Stress test: %s", testName);
    printf("\n----\n");
}


void test_nullPointers(void) {
    printHeader("1. Check on NULL pointers and numInsn <= 0.");

    printf(" 1.1 - Passing insnBuffer = NULL, numInsn = 5...\n");
    disassemble(NULL, 5, NULL);
    printf(" PASS! Crashes avoided.\n\n");

    printf(" 1.2 - Passing numInsn = 0...");
    VTAGenericInsn testInsn;
    disassemble(&testInsn, 0, NULL);
    printf(" PASS! Crashes avoided.\n\n");

    printf(" 1.3 - Passing numInsn = -5...");
    disassemble(&testInsn, -5, NULL);
    printf(" PASS! Crashes avoided.\n\n");
}


int main(void) {
    printf("- STRESS TEST FOR VTA DISASSEMBLER.");

    test_nullPointers();

    printf("✔ ALL TEST HAVE BEEN SUCCESSFULLY PASSED!");

    return 0;
}