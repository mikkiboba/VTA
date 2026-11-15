/*
 * End-to-end check for the experimental VTA simulator:
 * VTA assembly -> vtaAssemble -> VTADeviceRun -> numerical output.
 *
 * Expected configuration: VTA_BATCH=1, VTA_BLOCK_IN=VTA_BLOCK_OUT=16,
 * 8-bit inputs/weights/outputs, 32-bit accumulators.
 *
 * IMPORTANT: src/assembler.c's UOP LOAD parser must encode
 * y_size = 1 and x_stride = x_size, not leave y_size = 0.
 */

#include <inttypes.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include <vta/driver.h>
#include "vta.h"

#define N_INSN_MAX 16
#define N_UOP_MAX 8

#if VTA_BATCH != 1 || VTA_BLOCK_IN != 16 || VTA_BLOCK_OUT != 16
#error "This example requires VTA_BATCH=1 and VTA_BLOCK_IN=VTA_BLOCK_OUT=16"
#endif

#if VTA_INP_WIDTH != 8 || VTA_WGT_WIDTH != 8 || VTA_OUT_WIDTH != 8 || VTA_ACC_WIDTH != 32
#error "This example requires int8 inputs/weights/outputs and int32 accumulators"
#endif

static int get_dram_index(void *buffer, uint64_t elem_size,
                          const char *name, uint32_t *index_out)
{
    uint64_t phy_addr = (uint64_t)VTAMemGetPhyAddr(buffer);
    if (elem_size == 0 || (phy_addr % elem_size) != 0) {
        fprintf(stderr, "%s: physical address is not element-aligned\n", name);
        return -1;
    }
    if ((phy_addr / elem_size) > UINT32_MAX) {
        fprintf(stderr, "%s: DRAM index exceeds 32-bit encoding\n", name);
        return -1;
    }
    *index_out = (uint32_t)(phy_addr / elem_size);
    return 0;
}

int main(void)
{
    int exit_code = 1;
    VTADeviceHandle device = NULL;
    void *input_dram = NULL;
    void *weight_dram = NULL;
    void *output_dram = NULL;
    void *uop_dram = NULL;
    void *insn_dram = NULL;

    int8_t input[VTA_BLOCK_IN];
    int8_t weights[VTA_BLOCK_IN * VTA_BLOCK_OUT] = {0};
    int8_t output[VTA_BLOCK_OUT] = {0};
    int expected = 0;

    for (int i = 0; i < VTA_BLOCK_IN; ++i) {
        input[i] = (int8_t)(1 + i % 4);
        weights[i] = 1; /* WGT row 0 = [1,1,...,1] */
        expected += input[i] * weights[i];
    }

    input_dram  = VTAMemAlloc(sizeof(input), 0);
    weight_dram = VTAMemAlloc(sizeof(weights), 0);
    output_dram = VTAMemAlloc(sizeof(output), 0);
    uop_dram    = VTAMemAlloc(N_UOP_MAX * sizeof(VTAUop), 0);
    insn_dram   = VTAMemAlloc(N_INSN_MAX * sizeof(VTAGenericInsn), 0);

    if (!input_dram || !weight_dram || !output_dram || !uop_dram || !insn_dram) {
        fprintf(stderr, "VTAMemAlloc failed\n");
        goto cleanup;
    }

    VTAMemCopyFromHost(input_dram, input, sizeof(input));
    VTAMemCopyFromHost(weight_dram, weights, sizeof(weights));
    VTAMemCopyFromHost(output_dram, output, sizeof(output));
    memset(uop_dram, 0, N_UOP_MAX * sizeof(VTAUop));
    memset(insn_dram, 0, N_INSN_MAX * sizeof(VTAGenericInsn));

    uint32_t input_idx, weight_idx, output_idx, uop_idx;
    /* ACC_8BIT LOAD uses VTA_ACC_ELEM_BYTES / 4 bytes per tile. */
    if (get_dram_index(input_dram, VTA_ACC_ELEM_BYTES / 4, "input", &input_idx) ||
        get_dram_index(weight_dram, VTA_WGT_ELEM_BYTES, "weights", &weight_idx) ||
        get_dram_index(output_dram, VTA_OUT_ELEM_BYTES, "output", &output_idx) ||
        get_dram_index(uop_dram, VTA_UOP_ELEM_BYTES, "UOP", &uop_idx)) {
        goto cleanup;
    }

    char asm_code[2048];
    int chars = snprintf(
        asm_code, sizeof(asm_code),
        /* The zero-length UOP range is used for a post-STORE barrier. */
        "barrier_begin:\n"
        "barrier_end:\n"
        "dot_begin:\n"
        "1, 0, 0\n"  /* dst ACC[1], src ACC[0], WGT row 0 */
        "dot_end:\n"
        "\n"
        "LOAD(UOP[0], MEM[%" PRIu32 ", 1])\n"
        "LOAD(ACC_8BIT[0], MEM[%" PRIu32 ", 1, 1, 1])\n"
        "LOAD(WGT[0], MEM[%" PRIu32 ", 1, 1, 1])\n"
        "PUSH (LD->EX)\n"
        "POP (LD->EX)\n"
        "FOR (1, 1) UOP (dot_begin, dot_end)\n"
        "ALU.DOT(DST[0:1], SRC[0:1])\n"
        "PUSH (EX->ST)\n"
        "POP (EX->ST)\n"
        "STOR(MEM[%" PRIu32 ", 1, 1, 1], ACC[1])\n"
        "PUSH (ST->EX)\n"
        /* Consume STORE completion before FINISH can execute. */
        "POP (ST->EX)\n"
        "FOR (1, 1) UOP (barrier_begin, barrier_end)\n"
        "GEMM.RST(ACC[0:0])\n"
        "FINISH\n",
        uop_idx, input_idx, weight_idx, output_idx
    );
    if (chars < 0 || (size_t)chars >= sizeof(asm_code)) {
        fprintf(stderr, "Assembly text truncated\n");
        goto cleanup;
    }

    printf("Assembled program:\n%s\n", asm_code);

    int n_insn = 0, n_uop = 0;
    VTAErr status = vtaAssemble(
        asm_code,
        (VTAGenericInsn *)insn_dram, N_INSN_MAX, &n_insn,
        (VTAUop *)uop_dram, N_UOP_MAX, &n_uop
    );
    if (status != VTA_OK) {
        fprintf(stderr, "vtaAssemble failed (status=%d)\n", (int)status);
        vtaErrorPrint(status, -1);
        goto cleanup;
    }
    if (n_insn != 7 || n_uop != 1) {
        fprintf(stderr, "Unexpected counts: instructions=%d (expected 7), UOPs=%d (expected 1)\n",
                n_insn, n_uop);
        goto cleanup;
    }

    device = VTADeviceAlloc();
    if (!device) {
        fprintf(stderr, "VTADeviceAlloc failed\n");
        goto cleanup;
    }
    int run_status = VTADeviceRun(
        device, VTAMemGetPhyAddr(insn_dram), (uint32_t)n_insn, 1000000
    );
    if (run_status != 0) {
        fprintf(stderr, "VTADeviceRun returned %d\n", run_status);
        goto cleanup;
    }

    VTAMemCopyToHost(output, output_dram, sizeof(output));

    printf("Expected output: [%d, 0, 0, ...]\n", expected);
    printf("Actual output:   [");
    for (int i = 0; i < VTA_BLOCK_OUT; ++i)
        printf("%s%d", i == 0 ? "" : ", ", (int)output[i]);
    printf("]\n");

    if (output[0] != expected) {
        fprintf(stderr, "FAIL: lane 0 is %d, expected %d\n", output[0], expected);
        goto cleanup;
    }
    for (int i = 1; i < VTA_BLOCK_OUT; ++i) {
        if (output[i] != 0) {
            fprintf(stderr, "FAIL: lane %d is %d, expected 0\n", i, output[i]);
            goto cleanup;
        }
    }

    puts("PASS: assembled ALU.DOT executed correctly in experimental VTA");
    exit_code = 0;

cleanup:
    if (device) VTADeviceFree(device);
    if (input_dram) VTAMemFree(input_dram);
    if (weight_dram) VTAMemFree(weight_dram);
    if (output_dram) VTAMemFree(output_dram);
    if (uop_dram) VTAMemFree(uop_dram);
    if (insn_dram) VTAMemFree(insn_dram);
    return exit_code;
}
