/*
 * TUTORIAL C
 *
 * C keeps all B improvements and removes the depthwise -> pointwise
 * DRAM round trip by introducing ACC_TO_INP:
 *
 *   GEMM_DIAG -> ACC SRAM -> ACC_TO_INP -> INP SRAM -> pointwise GEMM
 *
 * Results for the current test:
 *
 *   DRAM depthwise weights: 2304 B -> 144 B
 *   on-chip depthwise WGT:  2304 B -> 144 B
 *   depthwise MACs:         9216 -> 576
 *   DW->PW DRAM traffic:    128 B -> 0 B
 *
 * Current limitation:
 * ACC_TO_INP performs direct int32 -> int8 conversion. Proper configurable
 * requantization is intentionally left for a later phase.
 */


#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include <inttypes.h>
#include <vta/driver.h>

#include "vta/hw_spec_const.h"
#include "runtime.h"
#include "vta.h"


#define BLOCK 16
#define BATCH 1
#define INPUT_H 4
#define INPUT_W 4
#define KERNEL 3
#define KERNEL_TAPS (KERNEL * KERNEL)
#define STRIDE_H 1
#define STRIDE_W 1
#define PAD_TOP 0
#define PAD_BOTTOM 0
#define PAD_LEFT 0
#define PAD_RIGHT 0
#define OUTPUT_H (((INPUT_H + PAD_TOP + PAD_BOTTOM - KERNEL) / STRIDE_H) + 1)
#define OUTPUT_W (((INPUT_W + PAD_LEFT + PAD_RIGHT - KERNEL) / STRIDE_W) + 1)
#define OUTPUT_PIXELS (OUTPUT_H * OUTPUT_W)

#define LOAD 1
#define COMPUTE 2
#define STORE 3

static void *RESET_cache_handle = NULL;
static void *DEPTH_cache_handle = NULL;
static void *POINT_cache_handle = NULL;

static void printArray(int8_t array[], int rows, int cols)
{
    for (int r = 0; r < rows; r++) {
        for (int c = 0; c < cols; c++)
            printf("%2d ", array[cols * r + c]);
        printf("\n");
    }
}

static int countValidTaps(int outPixel)
{
    int oy = outPixel / OUTPUT_W;
    int ox = outPixel % OUTPUT_W;
    int validTaps = 0;

    for (int ky = 0; ky < KERNEL; ky++)
        for (int kx = 0; kx < KERNEL; kx++) {
            int inY = oy * STRIDE_H + ky - PAD_TOP;
            int inX = ox * STRIDE_W + kx - PAD_LEFT;

            if (inY >= 0 && inY < INPUT_H &&
                inX >= 0 && inX < INPUT_W)
                validTaps++;
        }

    return validTaps;
}

static int reset_init(void *signature)
{
    VTAUopLoopBegin(OUTPUT_PIXELS, 1, 0, 0);
        VTAUopPush(0, 1, 0, 0, 0, 0, 0, 0);
    VTAUopLoopEnd();
    return 0;
}

static int depthwise_init(void *signature)
{
    VTAUopLoopBegin(OUTPUT_PIXELS, 1, KERNEL_TAPS, 0);
        VTAUopLoopBegin(KERNEL_TAPS, 0, 1, 1);
            VTAUopPush(0, 0, 0, 0, 0, 0, 0, 0);
        VTAUopLoopEnd();
    VTAUopLoopEnd();
    return 0;
}

static int pointwise_init(void *signature)
{
    VTAUopLoopBegin(OUTPUT_PIXELS, 1, 1, 0);
        VTAUopPush(0, 0, 0, 0, 0, 0, 0, 0);
    VTAUopLoopEnd();
    return 0;
}

static void buildInput(int8_t *input, int8_t *image)
{
    for (int oy = 0; oy < OUTPUT_H; oy++)
        for (int ox = 0; ox < OUTPUT_W; ox++) {
            int outPixel = oy * OUTPUT_W + ox;

            for (int ky = 0; ky < KERNEL; ky++)
                for (int kx = 0; kx < KERNEL; kx++) {
                    int tap = ky * KERNEL + kx;
                    int inY = oy * STRIDE_H + ky - PAD_TOP;
                    int inX = ox * STRIDE_W + kx - PAD_LEFT;

                    int value = 0;
                    if (inY >= 0 && inY < INPUT_H &&
                        inX >= 0 && inX < INPUT_W)
                        value = image[inY * INPUT_W + inX];

                    for (int c = 0; c < BLOCK; c++) {
                        int index =
                            outPixel * KERNEL_TAPS * BLOCK +
                            tap * BLOCK + c;

                        if (inY < 0 || inY >= INPUT_H ||
                            inX < 0 || inX >= INPUT_W)
                            input[index] = 0;
                        else
                            input[index] = value + (c % 2);
                    }
                }
        }
}

static void buildDepthwiseWeights(int8_t *weight)
{
    for (int tap = 0; tap < KERNEL_TAPS; tap++)
        for (int c = 0; c < BLOCK; c++)
            weight[tap * BLOCK + c] = 1;
}

static void buildPointwiseWeights(int8_t *weight)
{
    memset(weight, 0, BLOCK * BLOCK * sizeof(int8_t));

    for (int c = 0; c < BLOCK; c += 2) {
        weight[c * BLOCK + c] = 1;
        weight[c * BLOCK + c + 1] = -1;
        weight[(c + 1) * BLOCK + c] = -1;
        weight[(c + 1) * BLOCK + c + 1] = 1;
    }
}

static int validateDepthwise(int8_t *depthOutput, int8_t *image)
{
    int errors = 0;

    for (int oy = 0; oy < OUTPUT_H; oy++)
        for (int ox = 0; ox < OUTPUT_W; ox++) {
            int p = oy * OUTPUT_W + ox;
            int expected = 0;

            for (int ky = 0; ky < KERNEL; ky++)
                for (int kx = 0; kx < KERNEL; kx++) {
                    int inY = oy * STRIDE_H + ky - PAD_TOP;
                    int inX = ox * STRIDE_W + kx - PAD_LEFT;

                    if (inY >= 0 && inY < INPUT_H &&
                        inX >= 0 && inX < INPUT_W)
                        expected += image[inY * INPUT_W + inX];
                }

            printf("Output pixel %d, expected %d:\n", p, expected);

            for (int c = 0; c < BLOCK; c++) {
                int got = depthOutput[p * BLOCK + c];
                int expectedChannel =
                    expected + ((c % 2) * countValidTaps(p));

                printf("%2d ", got);
                if (got != expectedChannel)
                    errors++;
            }
            printf("\n");
        }

    if (errors == 0)
        printf("PASS: depthwise convolution works.\n");
    else
        printf("FAIL: %d incorrect depthwise values.\n", errors);

    return errors;
}

static int validatePointwise(int8_t *finalOutput)
{
    int errors = 0;

    printf("Pointwise output:\n");

    for (int p = 0; p < OUTPUT_PIXELS; p++) {
        printf("Output pixel %d:\n", p);

        for (int c = 0; c < BLOCK; c++) {
            int got = finalOutput[p * BLOCK + c];
            int expected =
                (c % 2 == 0) ? -countValidTaps(p) : countValidTaps(p);

            printf("%2d ", got);
            if (got != expected)
                errors++;
        }
        printf("\n");
    }

    if (errors == 0)
        printf("PASS: depthwise separable convolution works.\n");
    else
        printf("FAIL: %d incorrect values after pointwise.\n", errors);

    return errors;
}


static void printStats(void)
{
    int compactWeights = KERNEL_TAPS * BLOCK * sizeof(int8_t);
    int denseWeights =
        KERNEL_TAPS * BLOCK * BLOCK * sizeof(int8_t);
    int intermediate =
        OUTPUT_PIXELS * BATCH * BLOCK * sizeof(int8_t);
    int denseMACs =
        OUTPUT_PIXELS * KERNEL_TAPS * BLOCK * BLOCK;
    int diagMACs =
        OUTPUT_PIXELS * KERNEL_TAPS * BLOCK;

    printf("\nC statistics:\n");
    printf("Baseline dense DRAM weights:    %d bytes\n", denseWeights);
    printf("C compact DRAM weights:         %d bytes\n", compactWeights);
    printf("Baseline dense SRAM footprint:  %d bytes\n", denseWeights);
    printf("C compact SRAM footprint:       %d bytes\n", compactWeights);

    printf("\nDepthwise computation:\n");
    printf("Baseline dense GEMM MACs:       %d\n", denseMACs);
    printf("C diagonal GEMM MACs:           %d\n", diagMACs);
    printf("MAC reduction:                  %.2fx\n",
           (double)denseMACs / diagMACs);

    printf("\nDepthwise -> pointwise:\n");
    printf("Logical intermediate size:      %d bytes\n", intermediate);
    printf("Baseline intermediate STORE:    %d bytes\n", intermediate);
    printf("Baseline intermediate LOAD:     %d bytes\n", intermediate);
    printf("Baseline DRAM traffic:          %d bytes\n", intermediate * 2);
    printf("C intermediate STORE:           0 bytes\n");
    printf("C intermediate LOAD:            0 bytes\n");
    printf("C intermediate DRAM traffic:    0 bytes\n");
    printf("DRAM traffic reduction:         100.00%%\n");

    printf("\nSimulator measured MACs:\n");
    printf("Dense scalar MACs:              %llu\n",
           (unsigned long long)VTAGetDenseMACCounter());
    printf("Diagonal scalar MACs:           %llu\n",
           (unsigned long long)VTAGetDiagMACCounter());
}


static int getDramIndex(
    void *buffer,
    uint32_t elemBytes,
    uint32_t *index
)
{
    uint64_t address = (uint64_t)VTAMemGetPhyAddr(buffer);

    if (
        elemBytes == 0 ||
        address % elemBytes != 0 ||
        address / elemBytes > UINT32_MAX
    )
        return -1;

    *index = (uint32_t)(address / elemBytes);
    return 0;
}


static int loadAssembly(
    const char *path,
    char *output,
    size_t outputSize,
    const uint32_t addresses[5]
)
{
    static const char *tokens[5] = {
        "@UOP_ADDR@",
        "@INP_ADDR@",
        "@DIAG_ADDR@",
        "@PW_ADDR@",
        "@OUT_ADDR@"
    };

    char source[4096];

    FILE *file = fopen(path, "rb");
    if (!file) {
        perror(path);
        return -1;
    }

    size_t size = fread(
        source, 1, sizeof(source) - 1, file
    );
    int readError = ferror(file);
    fclose(file);

    if (readError || size == sizeof(source) - 1) {
        fprintf(stderr, "Cannot read assembly or file too large\n");
        return -1;
    }

    source[size] = '\0';

    size_t offset = 0;
    unsigned found[5] = {0};

    for (const char *p = source; *p;) {
        int match = -1;

        for (int i = 0; i < 5; ++i) {
            size_t len = strlen(tokens[i]);

            if (strncmp(p, tokens[i], len) == 0) {
                match = i;
                break;
            }
        }

        if (match >= 0) {
            if (offset >= outputSize)
                return -1;

            int written = snprintf(
                output + offset,
                outputSize - offset,
                "%" PRIu32,
                addresses[match]
            );

            if (written < 0 ||
                (size_t)written >= outputSize - offset)
                return -1;

            offset += (size_t)written;
            p += strlen(tokens[match]);
            found[match]++;
        }
        else {
            if (offset + 1 >= outputSize)
                return -1;

            output[offset++] = *p++;
        }
    }

    output[offset] = '\0';

    for (int i = 0; i < 5; ++i) {
        if (!found[i]) {
            fprintf(stderr, "Missing placeholder: %s\n", tokens[i]);
            return -1;
        }
    }

    return 0;
}


int main(int argc, char **argv)
{
    if (argc != 2)
    {
        fprintf(stderr, "Usage: %s <assmbly-file>\n", argv[0]);
        return 1;
    }
    enum { MAX_INSN = 32, MAX_UOP = 16 };

    int result = 1;
    VTADeviceHandle device = NULL;

    void *inputMem = NULL;
    void *diagMem = NULL;
    void *pointMem = NULL;
    void *outputMem = NULL;
    void *uopMem = NULL;
    void *insnMem = NULL;

    int8_t image[INPUT_H * INPUT_W];
    int8_t input[OUTPUT_PIXELS * KERNEL_TAPS * BLOCK];
    int8_t diagWeights[KERNEL_TAPS * BLOCK];
    int8_t pointWeights[BLOCK * BLOCK];
    int8_t output[OUTPUT_PIXELS * BLOCK] = {0};

    for (int i = 0; i < INPUT_H * INPUT_W; ++i)
        image[i] = i + 1;

    buildInput(input, image);
    buildDepthwiseWeights(diagWeights);
    buildPointwiseWeights(pointWeights);

    inputMem = VTAMemAlloc(sizeof(input), 0);
    diagMem = VTAMemAlloc(sizeof(diagWeights), 0);
    pointMem = VTAMemAlloc(sizeof(pointWeights), 0);
    outputMem = VTAMemAlloc(sizeof(output), 0);
    uopMem = VTAMemAlloc(MAX_UOP * sizeof(VTAUop), 0);
    insnMem = VTAMemAlloc(MAX_INSN * sizeof(VTAGenericInsn), 0);

    if (!inputMem || !diagMem || !pointMem ||
        !outputMem || !uopMem || !insnMem)
    {
        fprintf(stderr, "DRAM allocation failed\n");
        goto cleanup;
    }

    VTAMemCopyFromHost(inputMem, input, sizeof(input));
    VTAMemCopyFromHost(diagMem, diagWeights, sizeof(diagWeights));
    VTAMemCopyFromHost(pointMem, pointWeights, sizeof(pointWeights));
    VTAMemCopyFromHost(outputMem, output, sizeof(output));

    memset(uopMem, 0, MAX_UOP * sizeof(VTAUop));
    memset(insnMem, 0, MAX_INSN * sizeof(VTAGenericInsn));

    uint32_t inpAddr, diagAddr, pointAddr, outAddr, uopAddr;

    if (
        getDramIndex(inputMem, VTA_INP_ELEM_BYTES, &inpAddr) ||
        getDramIndex(diagMem, VTA_WGT_DIAG_ELEM_BYTES, &diagAddr) ||
        getDramIndex(pointMem, VTA_WGT_ELEM_BYTES, &pointAddr) ||
        getDramIndex(outputMem, VTA_OUT_ELEM_BYTES, &outAddr) ||
        getDramIndex(uopMem, VTA_UOP_ELEM_BYTES, &uopAddr)
    )
    {
        fprintf(stderr, "Invalid DRAM address\n");
        goto cleanup;
    }

    /*
     * Full depthwise separable convolution expressed
     * through our custom VTA assembly.
     *
     * UOP 0: reset ACC
     * UOP 1: depthwise GEMM.DIAG
     * UOP 2: pointwise GEMM
     *
     * The last zero-length range is a synchronization barrier.
     */
    char asmCode[4096];

    uint32_t addresses[5] = {
        uopAddr,
        inpAddr,
        diagAddr,
        pointAddr,
        outAddr
    };

    if (loadAssembly(
            argv[1],
            asmCode,
            sizeof(asmCode),
            addresses
        ) != 0)
    {
        fprintf(stderr, "Failed to load assembly\n");
        goto cleanup;
    }

    printf("\n===== VTA ASSEMBLY =====\n%s\n", asmCode);

    printf("\n===== VTA ASSEMBLY PROGRAM =====\n%s\n", asmCode);

    int numInsn = 0;
    int numUop = 0;

    VTAErr status = vtaAssemble(
        asmCode,
        (VTAGenericInsn *)insnMem,
        MAX_INSN,
        &numInsn,
        (VTAUop *)uopMem,
        MAX_UOP,
        &numUop
    );

    if (status != VTA_OK)
    {
        fprintf(stderr, "Assembly failed: %d\n", status);
        vtaErrorPrint(status, -1);
        goto cleanup;
    }

    printf(
        "Assembled instructions: %d\n"
        "Assembled UOPs:         %d\n",
        numInsn, numUop
    );

    if (numInsn != 12 || numUop != 3)
    {
        fprintf(stderr, "Unexpected instruction/UOP count\n");
        goto cleanup;
    }

    device = VTADeviceAlloc();

    if (!device)
    {
        fprintf(stderr, "VTADeviceAlloc failed\n");
        goto cleanup;
    }

    printf("\n===== EXECUTING VTA PROGRAM =====\n");

    int runStatus = VTADeviceRun(
        device,
        VTAMemGetPhyAddr(insnMem),
        (uint32_t)numInsn,
        1000000
    );

    if (runStatus != 0)
    {
        fprintf(stderr, "VTADeviceRun failed: %d\n", runStatus);
        goto cleanup;
    }

    VTAMemCopyToHost(output, outputMem, sizeof(output));

    printf("\n===== NUMERICAL VALIDATION =====\n");

    int errors = validatePointwise(output);

    if (errors != 0)
    {
        fprintf(stderr, "FAIL: %d incorrect output values\n", errors);
        goto cleanup;
    }

    printf("\n===== COMPUTATION STATISTICS =====\n");

    printStats();

    if (VTAGetDiagMACCounter() != 576 ||
        VTAGetDenseMACCounter() != 1024)
    {
        fprintf(stderr, "Unexpected simulator MAC counts\n");
        goto cleanup;
    }

    printf("\nPASS: Full assembly depthwise separable convolution\n");

    result = 0;

cleanup:
    if (device) VTADeviceFree(device);

    if (inputMem) VTAMemFree(inputMem);
    if (diagMem) VTAMemFree(diagMem);
    if (pointMem) VTAMemFree(pointMem);
    if (outputMem) VTAMemFree(outputMem);
    if (uopMem) VTAMemFree(uopMem);
    if (insnMem) VTAMemFree(insnMem);

    return result;
}
