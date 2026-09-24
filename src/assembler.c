#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

#include "vta.h"


/*!
 * \brief Max size for a token.
 *
 * \note 63 + 1 ('\\n')
*/
#define TEXT_SIZE 64

/*!
 * \brief Max dimension for label table.
*/
#define MAX_LABELS 128


/*!
 * \brief Token recognised by the assembler.
*/
typedef enum {
    /*!
     * \brief Mnemonics, registers, labels.
     *
     * (e.g. "LOAD", "ACC", "lbl1_bgn")
    */
    TOKEN_IDENTIFIER, 

    /*!
     * \brief Integer values
    */
    TOKEN_INT,

    /*!
     * \brief Punctuation characters.
     * 
     * (e.g. `(`, `[`, `:`, `,`)
    */
    TOKEN_PUNCTUATION,

    /*!
     * \brief End of line for uops.
    */
    TOKEN_EOL,

    /*!
     * \brief End of file/string.
    */
    TOKEN_EOF,

    /*!
     * \brief Invalid character.
    */
    TOKEN_ERROR,
} VTATokenType;


typedef enum {
    ASM_LOAD,
    ASM_STORE,
    ASM_GEMM,
    ASM_GEMM_RST,
    ASM_ALU,
    ASM_FINISH,
    ASM_NOOP
} VTAAsm;


typedef struct {
    int pop_prev;
    int pop_next;
    int push_prev;
    int push_next;
} VTADependencies;


typedef struct {
    VTAAsm kind;
    VTADependencies deps;

    union {
        VTAMemInsn mem;
        VTAGemInsn gemm;
        VTAAluInsn alu;
    } data;
} VTAParsedInsn;


/*!
 * \brief Token representing a single fragment of analised text.
 *
 * Store information on the type of token, what text it exactly contains, 
 * and the value.
 * 
 * \note 
 * - The text information must be of max length `TEXT_SIZE` (including `\n`).
 * 
 * - If the value is `TOKEN_INT`, it already contains the converted value.
*/
typedef struct {
    /*!
     * \brief Type of the analised text.
     * 
     * Defined by the enum `VTATokenType`.
    */
    VTATokenType type;

    /*!
     * \brief Analised text.
     *
     * Must be length `TEXT_SIZE`.
    */
    char text[TEXT_SIZE];

    /*!
     * \brief Value related to the analised text.
     * 
     * If `type == TOKEN_INT`, it contains the already converted value.
    */
    int value;
} VTAToken;


/*!
 * \brief Current state of the parser.
 *
 * Stores informations on the state of the parser's cursor and the line 
 * number it is working on.
*/
typedef struct {
    /*! 
     * \brief Pointer to the current character in input
    */
    const char* cursor;

    /*!
     * \brief Current line in input
    */
    int lineNum;
} VTAParserContext;


/*!
 * \brief Single label in the Label Table (LT).
 *
 * Store information on a single label to insert in the LT, such as its name, 
 * the micro-operation's index, and the line number on the input.
*/
typedef struct {
    char name[TEXT_SIZE];
    uint32_t uopIdx;
    int lineNum;
} VTALabel;


/*!
 * \brief Label table (LT).
 *
 * Store information on every label found in the input text. See struct `VTALabel`.
*/
typedef struct {
    VTALabel labels[MAX_LABELS];
    int count;
} VTALT;


// * forward declarations
static void skipWhiteSpaceAndComments(VTAParserContext *ctx);
static VTAToken getNextToken(VTAParserContext *ctx);
static VTAToken peekNextToken(const VTAParserContext *ctx);


/*!
 * \brief Initialize the label table (LT).
 *
 * Set table count to zero.
 * 
 * \param table Table to use.
*/
static void LT_init(VTALT *table) {
    table->count = 0;
}


/*!
 * \brief Add a new label to the label table (LT).
 * 
 * \param table     Label table.
 * \param name      Name of the label.
 * \param uopIdx    Index of the micro-instruction.
 * \param lineNum   Line number in the input file.
 * 
 * \return Status of type (VTAErr).
*/
static VTAErr LT_add(VTALT *table, const char *name, uint32_t uopIdx, int lineNum) {
    // * check for duplicates
    for (int i = 0; i < table->count; i++) {
        if (strcmp(table->labels[i].name, name) == 0)
            return VTA_ERR_SYNTAX;
    }

    if (table->count >= MAX_LABELS) 
        return VTA_ERR_NO_MEM_LABELS;

    strncpy(table->labels[table->count].name, name, TEXT_SIZE-1);

    table->labels[table->count].name[TEXT_SIZE-1]   = '\0';
    table->labels[table->count].uopIdx              = uopIdx;
    table->labels[table->count].lineNum             = lineNum;

    table->count++;

    return VTA_OK;
}


/*!
 * Find a label in the label table (LT).
 * 
 * \param table Label table.
 * \param name Name of the label to find.
 * 
 * \return UOP index if found (>= 0); -1 otherwise.
*/
static int LT_find(const VTALT *table, const char *name) {
    for (int i = 0; i < table->count; i++) {
        if (strcmp(table->labels[i].name, name) == 0)
            return (int)table->labels[i].uopIdx;
    }

    return -1;
}


/*!
 * \brief Get the next token without moving the cursor.
 *
 * \param ctx Parser context with cursor.
*/
static VTAToken peekNextToken(const VTAParserContext *ctx) {
    VTAParserContext tempCtx = *ctx;
    return getNextToken(&tempCtx);
}


/*!
 * \brief Go on with the text, skipping white spaces and comments.
 * 
 * Count line when encountering `\n` and move the cursor when encountering white spaces and comments.
 * 
 * \note
 * - Current supported comment formats: `#` (python-like), `//` (C-like).
 * 
 * \param ctx Input context.
*/
static void skipWhiteSpaceAndComments(VTAParserContext *ctx) {
    while (*ctx->cursor != '\0') {
        if (*ctx->cursor == '\n')
            break;
        
        if (isspace((unsigned char)*ctx->cursor)) {
            ctx->cursor++;
            continue;
        }

        if (*ctx->cursor == '#' || (*ctx->cursor == '/' && *(ctx->cursor + 1) == '/')) {
            while (*ctx->cursor != '\0' && *ctx->cursor != '\n')
                ctx->cursor++;
            
            continue;
        }

        break;
    }
}


/*!
 * \brief Read next token in input string.
 *
 * Remove white spaces and comments, check if is either a digit, identificator, special character
 * and produce an output `VTAToken`.
 * 
 * \param ctx Context parser from the input.
 * 
 * \return Next token.
*/
static VTAToken getNextToken(VTAParserContext *ctx) {
    VTAToken token;
    memset(&token, 0, sizeof(VTAToken));

    skipWhiteSpaceAndComments(ctx);

    if (*ctx->cursor == '\n') {
        token.type = TOKEN_EOL;
        strcpy(token.text, "\\n");

        ctx->cursor++;
        ctx->lineNum++;

        return token;
    }

    if (*ctx->cursor == '\0') {
        token.type = TOKEN_EOF;
        return token;
    }

    if (
        isdigit((unsigned char)*ctx->cursor) || 
        (*ctx->cursor == '-' && isdigit((unsigned char)*(ctx->cursor +1)))
    ) {
        token.type = TOKEN_INT;

        int i;
        i = 0;

        if (*ctx->cursor == '-')
            token.text[i++] = *ctx->cursor++;

        while (isdigit((unsigned char)*ctx->cursor) && i < TEXT_SIZE - 1)
            token.text[i++] = *ctx->cursor++;

        token.text[i] = '\0';

        token.value = atoi(token.text);

        return token;
    }

    if (isalpha((unsigned char)*ctx->cursor) || *ctx->cursor == '_') {
        token.type = TOKEN_IDENTIFIER;

        int i;
        i = 0;

        while (
            (isalnum((unsigned char)*ctx->cursor)   || 
            *ctx->cursor == '_'                     || 
            *ctx->cursor == '.')                    && 
            i < TEXT_SIZE-1
        ) {
            token.text[i++] = *ctx->cursor++;
        }
        
        token.text[i] = '\0';

        return token;
    }

    if (*ctx->cursor == '-' && *(ctx->cursor + 1) == '>') {
        token.type = TOKEN_PUNCTUATION;

        strcpy(token.text, "->");
        ctx->cursor += 2;

        return token;
    }

    if (strchr("()[],:", *ctx->cursor) != NULL) {
        token.type = TOKEN_PUNCTUATION;

        token.text[0] = *ctx->cursor++;
        token.text[1] = '\0';

        return token;
    }

    token.type = TOKEN_ERROR;
    
    token.text[0] = *ctx->cursor++;
    token.text[1] = '\0';
    
    return token;
}


/*!
 * \brief Fills the label table.
 *
 * \param asmCode
*/
static VTAErr makeLT(
    const char  *asmCode,
    VTALT       *table,
    int         maxInsn,
    int         maxUop,
    int         *estimatedInsn,
    int         *estimatedUop,
    int         *errLine
) {
    VTAParserContext ctx;
    ctx.cursor  = asmCode;
    ctx.lineNum = 1;

    LT_init(table);

    uint32_t currentUopIdx;
    currentUopIdx = 0;

    int IC;
    IC = 0;

    while (1) {
        VTAToken token;
        token = getNextToken(&ctx);

        if (token.type == TOKEN_EOF)
            break;
        
        if (token.type == TOKEN_ERROR) {
            *errLine = ctx.lineNum;
            return VTA_ERR_SYNTAX;
        }

        if (token.type == TOKEN_IDENTIFIER) {
            VTAToken nextToken;
            nextToken = peekNextToken(&ctx);

            if (nextToken.type == TOKEN_PUNCTUATION && strcmp(nextToken.text, ":") == 0) {
                getNextToken(&ctx); // consumes ":"

                VTAErr error;
                error = LT_add(table, token.text, currentUopIdx, ctx.lineNum);

                if (error != VTA_OK) {
                    *errLine = ctx.lineNum;
                    return error;
                }

                continue;
            }

            if (
                strcmp(token.text, "LOAD")   == 0 || strcmp(token.text, "STORE")    == 0 ||
                strcmp(token.text, "STOR")   == 0 || strcmp(token.text, "GEMM")     == 0 ||
                strcmp(token.text, "ALU")    == 0 || strncmp(token.text, "ALU.", 4) == 0 ||
                strcmp(token.text, "FINISH") == 0 || strcmp(token.text, "NOOP")     == 0 ||
                strcmp(token.text, "PUSH")   == 0 || strcmp(token.text, "POP")      == 0       
            ) {
                IC++;
                if (IC > maxInsn) {
                    *errLine = ctx.lineNum;
                    return VTA_ERR_INSN_BUFFER_FULL;
                }
            }
        }
        else if (token.type == TOKEN_INT) {
            // * a sequence like "0, 0, 0" represents an uop
            currentUopIdx++;

            if ((int)currentUopIdx > maxUop) {
                *errLine = ctx.lineNum;
                return VTA_ERR_UOP_BUFFER_FULL;
            }

            // * consume all other nums and ','
            while (1) {
                VTAToken nextToken;
                nextToken = peekNextToken(&ctx);

                if (
                    nextToken.type == TOKEN_INT ||
                    (nextToken.type == TOKEN_PUNCTUATION && strcmp(nextToken.text, ",") == 0)
                ) 
                    getNextToken(&ctx);
                else
                    break;
            }
        }
    }

    *estimatedInsn  = IC;
    *estimatedUop   = (int)currentUopIdx;

    return VTA_OK;
}


VTAErr vtaAssemble(
    const char      *asmCode,
    VTAGenericInsn  *insnBuffer,
    int             maxInsn,
    int             *numInsn,
    VTAUop          *uopBuffer,
    int             maxUop,
    int             *numUop
) {
    if (
        asmCode     == NULL || 
        insnBuffer  == NULL || 
        numInsn     == NULL || 
        uopBuffer   == NULL ||
        numUop      == NULL
    )
        return VTA_ERR_NULLPTR;
    if (maxInsn <= 0 || maxUop <= 0)
        return VTA_ERR_INVALID_INSN_SIZE;

    *numInsn = 0;
    *numUop  = 0;

    VTALT labelTable;
    int estimatedInsn, estimatedUop, errLine;
    estimatedInsn   = 0;
    estimatedUop    = 0;
    errLine         = 1;

    // *: Step 1: scan to make label table
    VTAErr status;
    status = makeLT(asmCode, &labelTable, maxInsn, maxUop, &estimatedInsn, &estimatedUop, &errLine);
    if (status != VTA_OK) 
        return status;
    
    // TODO: Step 2: binary stuff


    *numInsn = estimatedInsn;
    *numUop  = estimatedUop;

    return VTA_OK;
}

