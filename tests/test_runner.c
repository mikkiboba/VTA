#include "test_runner_assembler.c"
#include "test_runner_disassembler.c"


int main(void) {
    int error;
    error = 0;

    executeTestsDisassembler(&error);
    executeTestsAssembler(&error);

    if (error > 0)
        printf("X - NOT ALL TESTS HAVE PASSED (%d failures).\n", error);
    else
        printf("✔ - ALL TEST HAVE BEEN SUCCESSFULLY HANDLED!\n");

    return error;
}