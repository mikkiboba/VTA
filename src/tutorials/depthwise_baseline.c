/*
    informazioni sulle dimensioni richeiste dal codice
    trovate in /tvm/3rdparty/vta-hw/config/vta_config.json

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

    le misure sono in LOG2 -> quindi per esempio per gli input richiede valori a 2^3 = 8 bit

*/


/* Esempio di moltiplicazione tra matrici

    [1 1 2      [1      [5
             X   2   =
     2 3 1]      1]      9]

    Poi vengono aggiunti i padding per renderle matrixi 2x16, 16x16, 2x16
*/


#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include "vta/runtime/runtime.h"
#include "vta/hw_spec_const.h"

#define BLOCK 16 // -> obbligatorio a 16 perché LOG_BLOCK = 4 => 2^4 = 16
#define BATCH 1
#define KERNEL_TAPS 9

#define INPUT_COLUMNS 3
#define OUTPUT_COLOMUNS 1

// * definizione degli stage, per la concorrenza
#define LOAD 1
#define COMPUTE 2
#define STORE 3

static void* GEMM_cache_handle = NULL;

static void *RESET_cache_handle = NULL;
static void *DEPTH_cache_handle = NULL;

/*! \brief Funzione per stampare le matrici.
    \param array[] Array da stampare.
    \param rows Quante righe ha l'array.
    \param cols Quante colonne ha l'array.
*/
void printArray(int8_t array[], int rows, int cols) {
    for (int r = 0; r < rows; r++) {
        for (int c = 0; c < cols; c++)
            printf("%d ", array[cols * r + c]);
        printf("\n");
    }
}


int reset_init(void *signature)
{
    VTAUopPush(0, 1, 1, 0, 0, 0, 0, 0);
    return 0;
}


int depthwise_init(void *signature)
{
    /*
     * 9 iterazioni
     *
     * dst_factor = 0 -> accumuliamo sempre nello stesso ACC
     * src_factor = 1 -> passiamo al prossimo tile dell'inp
     * wgt_factor = 1 -> passiamo alla prossima matrice diag
     */
    VTAUopLoopBegin(KERNEL_TAPS, 0, 1, 1);
        VTAUopPush(0, 0, 0, 0, 0, 0, 0, 0);
    VTAUopLoopEnd();

    return 0;
}


/*! \brief Inizializzazione operazioni da effettuare su input e pesi. Dice come scorrere sui dati all'interno della SRAM interna.
    \param signature
*/
int gemm_init(void* signature) {
    // * Iterazione su BATCH (=2), ad ogni op. scorre acc e inp di 1
    // ! matrice dei pesi non scorre perché condivisa tra i batch (credo? ho capito di impostare il valore a 0 facendo trial-and-error)
    VTAUopLoopBegin(BATCH, 1, 1, 0);
        // * microistruzione -> reset è inizializzato a 0 quindi dovrebbe essere un acc += inp x weight
        // ! suppongo che il reset sia a 0 perché nel simulatore l'accumulatore sia già azzerato di default(?)
        VTAUopPush(0, 0, 0, 0, 0, 0, 0, 0);
    VTAUopLoopEnd();

    return 0;
}

int main(void) {

    // * inizializzazione dei buffer
    void* inputBuffer;
    inputBuffer = VTABufferAlloc(KERNEL_TAPS * BATCH * BLOCK * sizeof(int8_t));

    void* weightBuffer;
    weightBuffer = VTABufferAlloc(KERNEL_TAPS * BLOCK * BLOCK * sizeof(int8_t));

    void* outputBuffer;
    outputBuffer = VTABufferAlloc(KERNEL_TAPS * BATCH * BLOCK * sizeof(int8_t));


    // * riempimento delle matrici di input e pesi
    // ! VTA_MEMCPY_H2D è MEMORIA HOST-TO-DEVICE -> copia dentro la memoria del dispositivo

    int8_t input[KERNEL_TAPS * BATCH * BLOCK];

    for (int tap = 0; tap < KERNEL_TAPS; tap++)
        for (int i = 0; i < BLOCK; i++)
            input[tap * BLOCK + i] = tap + 1;

    VTABufferCopy(input, 0, inputBuffer, 0, sizeof(input), VTA_MEMCPY_H2D);

    printf("Array di input:\n");
    printArray(input, BATCH, BLOCK);


    int8_t weight[KERNEL_TAPS * BLOCK * BLOCK];

    memset(weight, 0, sizeof(weight));

    for (int tap = 0; tap < KERNEL_TAPS; tap++)
        for (int i = 0; i < BLOCK; i++)
        {
            int channelWeigth;
            channelWeigth = (i % 2) + 1;

            int offset;
            offset = tap * BLOCK * BLOCK + i * BLOCK + i;

            weight[offset] = channelWeigth;
        }

    VTABufferCopy(weight, 0, weightBuffer, 0, sizeof(weight), VTA_MEMCPY_H2D);

    printf("Array di pesi:\n");
    printArray(weight, BLOCK, BLOCK);



    VTACommandHandle handle = VTATLSCommandHandle();
    // VTASetDebugMode(handle, VTA_DEBUG_DUMP_INSN | VTA_DEBUG_DUMP_UOP);


    // * sposta i dati dalla memoria del device (dram) alla sram dell'accelleratore
    // * 2 righe di blocchi per l'input e una per i pesi, quelle mi servono
    VTALoadBuffer2D(handle, inputBuffer, 0, 1, KERNEL_TAPS, 1, 0, 0, 0, 0, 0, VTA_MEM_ID_INP);
    VTALoadBuffer2D(handle, weightBuffer, 0, 1, KERNEL_TAPS, 1, 0, 0, 0, 0, 0, VTA_MEM_ID_WGT);

    VTADepPush(handle, LOAD, COMPUTE);
    VTADepPop(handle, LOAD, COMPUTE);


    // * qui avrei potuto usare una signature con un reset value inizializzato a 0 da passare alla funzione
    VTAPushGEMMOp(&RESET_cache_handle, reset_init, NULL, 0);
    VTAPushGEMMOp(&DEPTH_cache_handle, depthwise_init, NULL, 0);

    VTADepPush(handle, COMPUTE, STORE);
    VTADepPop(handle, COMPUTE, STORE);


    // * sposta i dati dalla sram dell'acc alla dram
    VTAStoreBuffer2D(handle, 0, VTA_MEM_ID_OUT, outputBuffer, 0, 1, BATCH, 1);

    // * esegue operazioni, il secondo valore è il wait time massimo della cpu di attesa dell'esecuzione delle operazioni
    VTASynchronize(handle, 1000000);

    // * facciamo arrivare i dati alla cpu
    int8_t output[BATCH * BLOCK];
    VTABufferCopy(outputBuffer, 0, output, 0, sizeof(output), VTA_MEMCPY_D2H);

    printf("Array di output:\n");
    printArray(output, BATCH, BLOCK);

    int errors;
    errors = 0;

    for (int i = 0; i < BLOCK; i++)
    {
        int channelWeight;
        channelWeight = (i % 2) + 1;

        int expected;
        expected = 45 * channelWeight;

        printf("Channel %2d: got %3d, expected %3d\n", i, output[i], expected);

        if (output[i] != expected)
            errors++;
    }

    if (errors == 0)
        printf("PASS: 3x3 depthwise accumulation works.\n");
    else
        printf("FAIL: %d incorrect values.\n", errors);

    VTABufferFree(inputBuffer);
    VTABufferFree(weightBuffer);
    VTABufferFree(outputBuffer);

    return 0;
}
