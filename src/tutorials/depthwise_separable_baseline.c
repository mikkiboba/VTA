/*
    Configurazione VTA usata dal simulatore:
    /tvm/3rdparty/vta-hw/config/vta_config.json

    {
        "TARGET" : "sim",
        "HW_VER" : "0.0.2",
        "LOG_INP_WIDTH" : 3,
        "LOG_WGT_WIDTH" : 3,
        "LOG_ACC_WIDTH" : 5,
        "LOG_BATCH" : 0,
        "LOG_BLOCK" : 4,
        "LOG_UOP_BUFF_SIZE" : 15,
        "LOG_INP_BUFF_SIZE" : 15,
        "LOG_WGT_BUFF_SIZE" : 18,
        "LOG_ACC_BUFF_SIZE" : 17
    }

    Le misure sono in LOG2:
    input e pesi sono a 8 bit, l'accumulatore è a 32 bit,
    il batch è 1 e il block size è 16.
*/


#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include "vta/runtime/runtime.h"
#include "vta/hw_spec_const.h"

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

// * definizione degli stage della pipeline VTA
#define LOAD 1
#define COMPUTE 2
#define STORE 3

static void *RESET_cache_handle = NULL;
static void *DEPTH_cache_handle = NULL;
static void *POINT_cache_handle = NULL;


/*! \brief Funzione per stampare le matrici.
    \param array[] Array da stampare.
    \param rows Quante righe ha l'array.
    \param cols Quante colonne ha l'array.
*/
void printArray(int8_t array[], int rows, int cols) {
    for (int r = 0; r < rows; r++) {
        for (int c = 0; c < cols; c++)
            printf("%2d ", array[cols * r + c]);
        printf("\n");
    }
}


int countValidTaps(int outPixel)
{
    int oy, ox;
    oy = outPixel / OUTPUT_W;
    ox = outPixel % OUTPUT_W;

    int validTaps;
    validTaps = 0;

    for (int ky = 0; ky < KERNEL; ky++)
        for (int kx = 0; kx < KERNEL; kx++)
        {
            int inY, inX;
            inY = oy * STRIDE_H + ky - PAD_TOP;
            inX = ox * STRIDE_W + kx - PAD_LEFT;

            if (inY >= 0 && inY < INPUT_H && inX >= 0 && inY < INPUT_W)
                validTaps++;
        }

    return validTaps;
}


void printBaselineStats(void)
{
    int inputOriginalBytes;
    inputOriginalBytes = INPUT_H * INPUT_W * BATCH * BLOCK * sizeof(int8_t);

    int inputPackedBytes;
    inputPackedBytes = OUTPUT_PIXELS * KERNEL_TAPS * BATCH * BLOCK * sizeof(int8_t);

    int depthwiseIdealWeightBytes;
    depthwiseIdealWeightBytes = KERNEL_TAPS * BLOCK * sizeof(int8_t);

    int depthwiseVTAWeightBytes;
    depthwiseVTAWeightBytes = KERNEL_TAPS * BLOCK * BLOCK * sizeof(int8_t);

    int depthwiseIdealMACs;
    depthwiseIdealMACs = OUTPUT_PIXELS * KERNEL_TAPS * BLOCK;

    int validTaps;
    validTaps = 0;

    for (int p = 0; p < OUTPUT_PIXELS; p++)
        validTaps += countValidTaps(p);

    int depthwiseValidMACs;
    depthwiseValidMACs = validTaps * BLOCK;

    int depthwiseVTAMACs;
    depthwiseVTAMACs = OUTPUT_PIXELS * KERNEL_TAPS * BLOCK * BLOCK;

    int intermediateBytes;
    intermediateBytes = OUTPUT_PIXELS * BATCH * BLOCK * sizeof(int8_t);

    int pointwiseWeightBytes;
    pointwiseWeightBytes = BLOCK * BLOCK * sizeof(int8_t);

    int pointwiseMACs;
    pointwiseMACs = OUTPUT_PIXELS * BLOCK * BLOCK;

    printf("\nBaseline statistics:\n");

    printf("\nInput:\n");
    printf("Original input:                 %d bytes\n", inputOriginalBytes);
    printf("Packed depthwise input:         %d bytes\n", inputPackedBytes);
    printf("Packed/original ratio:          %.2fx\n", (double)inputPackedBytes / inputOriginalBytes);

    printf("\nDepthwise weights:\n");
    printf("Ideal depthwise weights:        %d bytes\n", depthwiseIdealWeightBytes);
    printf("VTA dense weights:              %d bytes\n", depthwiseVTAWeightBytes);
    printf("Weight overhead:                %.2fx\n", (double)depthwiseVTAWeightBytes / depthwiseIdealWeightBytes);

    printf("\nDepthwise computation:\n");
    printf("Ideal depthwise MACs:           %d\n", depthwiseIdealMACs);
    printf("Valid input MACs:               %d\n", depthwiseValidMACs);
    printf("VTA dense GEMM MACs:            %d\n", depthwiseVTAMACs);
    printf("Dense GEMM overhead:            %.2fx\n", (double)depthwiseVTAMACs / depthwiseIdealMACs);

    printf("\nDepthwise -> pointwise:\n");
    printf("Intermediate size:              %d bytes\n", intermediateBytes);
    printf("Intermediate STORE:             %d bytes\n", intermediateBytes);
    printf("Intermediate LOAD:              %d bytes\n", intermediateBytes);
    printf("Intermediate DRAM traffic:      %d bytes\n", intermediateBytes * 2);

    printf("\nPointwise:\n");
    printf("Pointwise weights:              %d bytes\n", pointwiseWeightBytes);
    printf("Pointwise GEMM MACs:            %d\n", pointwiseMACs);

    printf("\n");
}

/*! \brief Azzera i tile dell'accumulatore usati per i pixel di output. */
int reset_init(void *signature)
{
    VTAUopLoopBegin(OUTPUT_PIXELS, 1, 0, 0);

        VTAUopPush(0, 1, 0, 0, 0, 0, 0, 0);

    VTAUopLoopEnd();

    return 0;
}


/*! \brief Esegue la depthwise convolution accumulando i tap del kernel per ogni pixel di output. */
int depthwise_init(void *signature)
{
    /*
     * Outer loop:
     * cambia il pixel di output.
     *
     * dst += 1
     * src += KERNEL_TAPS
     * wgt += 0 -> riusa gli stessi filtri
     */
    VTAUopLoopBegin(OUTPUT_PIXELS, 1, KERNEL_TAPS, 0);

        /*
         * Inner loop:
         * scorre i tap della finestra KERNEL x KERNEL.
         *
         * dst += 0 -> accumula sullo stesso output
         * src += 1
         * wgt += 1
         */
        VTAUopLoopBegin(KERNEL_TAPS, 0, 1, 1);

            VTAUopPush(0, 0, 0, 0, 0, 0, 0, 0);

        VTAUopLoopEnd();

    VTAUopLoopEnd();

    return 0;
}


int pointwise_init(void *signature)
{
    /*
     * Scorre i pixel di output
     *
     * dst += 1
     * src += 1
     * wgt += 0 -> stessa matrice dei pesi per tutti i pixel
     */
    VTAUopLoopBegin(OUTPUT_PIXELS, 1, 1, 0);

        VTAUopPush(0, 0, 0, 0, 0, 0, 0, 0);

    VTAUopLoopEnd();

    return 0;
}


void depthwise_part(
    VTACommandHandle handle,
    int8_t* inputBuffer,
    int8_t* weightBuffer,
    int8_t* intermediateBuffer,
    int8_t* image
)
{
    // VTASetDebugMode(handle, VTA_DEBUG_DUMP_INSN | VTA_DEBUG_DUMP_UOP);


    // * caricamento degli input e dei pesi dalla DRAM alla SRAM di VTA
    VTALoadBuffer2D(handle, inputBuffer, 0, 1, OUTPUT_PIXELS * KERNEL_TAPS, 1, 0, 0, 0, 0, 0, VTA_MEM_ID_INP);
    VTALoadBuffer2D(handle, weightBuffer, 0, 1, KERNEL_TAPS, 1, 0, 0, 0, 0, 0, VTA_MEM_ID_WGT);

    VTADepPush(handle, LOAD, COMPUTE);
    VTADepPop(handle, LOAD, COMPUTE);


    // * azzeramento dell'accumulatore e depthwise convolution
    VTAPushGEMMOp(&RESET_cache_handle, reset_init, NULL, 0);
    VTAPushGEMMOp(&DEPTH_cache_handle, depthwise_init, NULL, 0);

    VTADepPush(handle, COMPUTE, STORE);
    VTADepPop(handle, COMPUTE, STORE);


    // * copia dei tile di output dalla SRAM di VTA alla DRAM
    VTAStoreBuffer2D(handle, 0, VTA_MEM_ID_OUT, intermediateBuffer, 0, 1, OUTPUT_PIXELS, 1);

    // * invio delle istruzioni a VTA e attesa del completamento
    VTASynchronize(handle, 1000000);

    // * copia del risultato dalla memoria del dispositivo alla CPU
    int8_t depthOutput[OUTPUT_PIXELS * BATCH * BLOCK];
    VTABufferCopy(intermediateBuffer, 0, depthOutput, 0, sizeof(depthOutput), VTA_MEMCPY_D2H);


    /*
     * Reference CPU.
     *
     * Usa le stesse formule di stride e padding della depthwise
     * per calcolare il risultato atteso.
     */
    int expected[OUTPUT_PIXELS];

    for (int oy = 0; oy < OUTPUT_H; oy++)
        for (int ox = 0; ox < OUTPUT_W; ox++)
        {
            int outPixel;
            outPixel = oy * OUTPUT_W + ox;

            int sum;
            sum = 0;

            for (int ky = 0; ky < KERNEL; ky++)
                for (int kx = 0; kx < KERNEL; kx++)
                {
                    int inY, inX;
                    inY = oy * STRIDE_H + ky - PAD_TOP;
                    inX = ox * STRIDE_W + kx - PAD_LEFT;

                    if (inY >= 0 && inY < INPUT_H && inX >= 0 && inX < INPUT_W)
                        sum += image[inY * INPUT_W + inX];
                }

            expected[outPixel] = sum;
        }


    int errors;
    errors = 0;

    for (int p = 0; p < OUTPUT_PIXELS; p++)
    {
        printf("Output pixel %d, expected %d:\n", p, expected[p]);

        for (int c = 0; c < BLOCK; c++)
        {
            int got;
            got = depthOutput[p * BLOCK + c];

            printf("%2d ", got);

            int expectedChannel;
            expectedChannel = expected[p] + ((c % 2) * countValidTaps(p));

            if (got != expectedChannel)
                errors++;
        }

        printf("\n");
    }

    if (errors == 0)
        printf("PASS: depthwise convolution works.\n");
    else
        printf("FAIL: %d incorrect values.\n", errors);
}


void pointwise_part(
    VTACommandHandle handle,
    int8_t* pointwiseWeightBuffer,
    int8_t* finalOutputBuffer,
    int8_t* image
)
{
    VTALoadBuffer2D(handle, pointwiseWeightBuffer, 0, 1, 1, 1, 0, 0, 0, 0, 0, VTA_MEM_ID_WGT);

    VTADepPush(handle, LOAD, COMPUTE);
    VTADepPop(handle, LOAD, COMPUTE);

    VTAPushGEMMOp(&RESET_cache_handle, reset_init, NULL, 0);
    VTAPushGEMMOp(&POINT_cache_handle, pointwise_init, NULL, 0);

    VTADepPush(handle, COMPUTE, STORE);
    VTADepPop(handle, COMPUTE, STORE);

    VTAStoreBuffer2D(handle, 0, VTA_MEM_ID_OUT, finalOutputBuffer, 0, 1, OUTPUT_PIXELS, 1);

    VTASynchronize(handle, 1000000);

    int8_t finalOutput[OUTPUT_PIXELS * BATCH * BLOCK];
    VTABufferCopy(finalOutputBuffer, 0, finalOutput, 0, sizeof(finalOutput), VTA_MEMCPY_D2H);

    int expected[OUTPUT_PIXELS];

    for (int oy = 0; oy < OUTPUT_H; oy++)
        for (int ox = 0; ox < OUTPUT_W; ox++)
        {
            int outPixel;
            outPixel = oy * OUTPUT_W + ox;

            int sum;
            sum = 0;

            for (int ky = 0; ky < KERNEL; ky++)
                for (int kx = 0; kx < KERNEL; kx++)
                {
                    int inY, inX;
                    inY = oy * STRIDE_H + ky - PAD_TOP;
                    inX = ox * STRIDE_W + kx - PAD_LEFT;

                    if (inY >= 0 && inY < INPUT_H && inX >= 0 && inX < INPUT_W)
                        sum += image[inY * INPUT_W + inX];
                }

            expected[outPixel] = sum;
        }

    int errors;
    errors = 0;

    printf("Pointwise output:\n");

    for (int p = 0; p < OUTPUT_PIXELS; p++)
    {
        printf("Output pixel %d, expected %d:\n", p, expected[p]);

        for (int c = 0; c < BLOCK; c++)
        {
            int got;
            got = finalOutput[p * BLOCK + c];

            printf("%2d ", got);

            int expectedChannel;
            expectedChannel = (c % 2 == 0) ? -countValidTaps(p) : countValidTaps(p);

            if (got != expectedChannel)
                errors++;
        }

        printf("\n");
    }

    if (errors == 0)
        printf("PASS: depthwise separable convolution works.\n");
    else
        printf("FAIL: %d incorrect values after pointwise.\n", errors);
}


int main(void)
{

    // * buffer nella memoria accessibile da VTA
    void *inputBuffer;
    inputBuffer = VTABufferAlloc(OUTPUT_PIXELS * KERNEL_TAPS * BATCH * BLOCK * sizeof(int8_t));

    void *weightBuffer;
    weightBuffer = VTABufferAlloc(KERNEL_TAPS * BLOCK * BLOCK * sizeof(int8_t));

    void *intermediateBuffer;
    intermediateBuffer = VTABufferAlloc(OUTPUT_PIXELS * BATCH * BLOCK * sizeof(int8_t));

    void *pointwiseWeightBuffer;
    pointwiseWeightBuffer = VTABufferAlloc(BLOCK * BLOCK * sizeof(int8_t));

    void *finalOutputBuffer;
    finalOutputBuffer = VTABufferAlloc(OUTPUT_PIXELS * BATCH * BLOCK * sizeof(int8_t));


    /*
     * Immagine di partenza.
     *
     * Con INPUT_H = 4 e INPUT_W = 4:
     *
     *  1  2  3  4
     *  5  6  7  8
     *  9 10 11 12
     * 13 14 15 16
     */
    int8_t image[INPUT_H * INPUT_W];

    for (int y = 0; y < INPUT_H; y++)
        for (int x = 0; x < INPUT_W; x++)
            image[y * INPUT_W + x] = y * INPUT_W + x + 1;

    printf("Immagine di input:\n");
    printArray(image, INPUT_H, INPUT_W);


    /*
     * Costruzione delle finestre del kernel.
     *
     * Le coordinate dell'input vengono calcolate come:
     *
     * inY = oy * STRIDE_H + ky - PAD_TOP
     * inX = ox * STRIDE_W + kx - PAD_LEFT
     *
     * Se la coordinata cade fuori dall'immagine,
     * il valore viene considerato uguale a 0.
     */
    int8_t input[OUTPUT_PIXELS * BATCH * KERNEL_TAPS * BLOCK];

    for (int oy = 0; oy < OUTPUT_H; oy++)
        for (int ox = 0; ox < OUTPUT_W; ox++)
        {
            int outPixel;
            outPixel = oy * OUTPUT_W + ox;

            for (int ky = 0; ky < KERNEL; ky++)
                for (int kx = 0; kx < KERNEL; kx++)
                {
                    int tap;
                    tap = ky * KERNEL + kx;

                    int inY, inX;
                    inY = oy * STRIDE_H + ky - PAD_TOP;
                    inX = ox * STRIDE_W + kx - PAD_LEFT;

                    int value;
                    value = 0;

                    if (inY >= 0 && inY < INPUT_H && inX >= 0 && inX < INPUT_W)
                        value = image[inY * INPUT_W + inX];

                    for (int c = 0; c < BLOCK; c++)
                    {
                        int index;
                        index = (outPixel * KERNEL_TAPS * BLOCK) + (tap * BLOCK) + c;

                        if (inY < 0 || inY >= INPUT_H || inX < 0 || inX >= INPUT_W)
                            input[index] = 0;
                        else
                            input[index] = value + (c % 2);
                    }
                }
        }

    VTABufferCopy(input, 0, inputBuffer, 0, sizeof(input), VTA_MEMCPY_H2D);

    printf("Primo tile di input:\n");
    printArray(input, BATCH, BLOCK);


    /*
     * Costruzione dei KERNEL_TAPS tile di pesi.
     * Ogni tile è una matrice diagonale BLOCK x BLOCK.
     */
    int8_t weight[KERNEL_TAPS * BLOCK * BLOCK];

    memset(weight, 0, sizeof(weight));

    for (int tap = 0; tap < KERNEL_TAPS; tap++)
        for (int c = 0; c < BLOCK; c++)
        {
            int index;
            index = (tap * BLOCK * BLOCK) + (c * BLOCK) + c;

            weight[index] = 1;
        }

    VTABufferCopy(weight, 0, weightBuffer, 0, sizeof(weight), VTA_MEMCPY_H2D);

    printf("Prima matrice dei pesi:\n");
    printArray(weight, BLOCK, BLOCK);

    int8_t pointwiseWeight[BLOCK * BLOCK];

    memset(pointwiseWeight, 0, sizeof(pointwiseWeight));

    for (int c = 0; c < BLOCK; c += 2)
    {
        pointwiseWeight[c * BLOCK + c]              =  1;
        pointwiseWeight[c * BLOCK + c + 1]          = -1;

        pointwiseWeight[(c + 1) * BLOCK + c]        = -1;
        pointwiseWeight[(c + 1) * BLOCK + c + 1]    =  1;
    }

    VTABufferCopy(pointwiseWeight, 0, pointwiseWeightBuffer, 0, sizeof(pointwiseWeight), VTA_MEMCPY_H2D);

    printf("Pointwise weight matrix:\n");
    printArray(pointwiseWeight, BLOCK, BLOCK);

    VTACommandHandle handle;
    handle = VTATLSCommandHandle();

    depthwise_part(handle, inputBuffer, weightBuffer, intermediateBuffer, image);

    printf("Reloading depthwise output into INPUT SRAM...\n");

    VTALoadBuffer2D(handle, intermediateBuffer, 0, 1, OUTPUT_PIXELS, 1, 0, 0, 0, 0, 0, VTA_MEM_ID_INP);

    VTASynchronize(handle, 1000000);

    printf("Depthwise output relaoded into INPUT SRAM.\n");

    pointwise_part(handle, pointwiseWeightBuffer, finalOutputBuffer, image);

    printBaselineStats();

    VTABufferFree(inputBuffer);
    VTABufferFree(weightBuffer);
    VTABufferFree(intermediateBuffer);
    VTABufferFree(pointwiseWeightBuffer);
    VTABufferFree(finalOutputBuffer);

    return 0;
}
