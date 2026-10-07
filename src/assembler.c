#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <stdint.h>

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
 * \brief Number of factors for GEMM operations.
*/
#define N_GEMM_FACTORS 6

/*!
 * \brief Number of factors for ALU operations.
*/
#define N_ALU_FACTORS 4

/*!
 * \brief Number of factors for the MEM padding for VTAMemInsn.
*/
#define N_MEM_PADDING_FACTORS 4


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
    int ex_ld;
    int ex_st;
    int ld_ex;
    int st_ex;
} VTADependenciesTransition;


typedef struct {
    VTAAsm kind;
    VTADependencies deps;

    char uopBgnLabel[TEXT_SIZE];
    char uopEndLabel[TEXT_SIZE];

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
    const char *cursor;

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
static void     skipWhiteSpaceAndComments(VTAParserContext *ctx);
static VTAToken getNextToken(VTAParserContext *ctx);
static VTAToken peekNextToken(const VTAParserContext *ctx);
static VTAToken peekNextLine(const VTAParserContext *ctx);

static VTAErr parseLoad(VTAParserContext *ctx, VTAParsedInsn *parsedInsn);
static VTAErr parseStore(VTAParserContext *ctx, VTAParsedInsn *parsedInsn);
static VTAErr parseFor(VTAParserContext *ctx, VTAParsedInsn *parsedInsn, int isGemm);
static VTAErr parseGemm(VTAParserContext *ctx, VTAParsedInsn *parsedInsn);
static VTAErr parseGemmRst(VTAParserContext *ctx, VTAParsedInsn *parsedInsn);
static VTAErr parseAlu(VTAParserContext *ctx, VTAParsedInsn *parsedInsn);
static VTAErr parseAluOpcode(const char *text, uint32_t *opcode);
static VTAErr parseForInstruction(VTAParserContext *ctx, VTAParsedInsn *parsedInsn);

static VTAErr parseInstruction(
    VTAParserContext *ctx,
    const VTAToken *token,
    VTAParsedInsn *parsedInsn
);

/*!
 * \brief Helper function to check if the next token is the expected one.
 *
 * \param ctx Context parser.
 * \param expectedType What kind of token is expected.
 * \param expectedText What exact text is expected.
*/
static VTAErr expectToken(VTAParserContext *ctx, VTATokenType expectedType, const char *expectedText) {
    VTAToken token;
    token = getNextToken(ctx);

    if (token.type != expectedType) 
        return VTA_ERR_SYNTAX;

    if (expectedText != NULL && strcmp(token.text, expectedText) != 0)
        return VTA_ERR_SYNTAX;

    return VTA_OK;
}


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

        token.value = atoi(token.text); // ! we could have overflow here

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
 * \brief TODO:
*/
static VTAErr skipToEOL(VTAParserContext *ctx) {
    while (1) {
        VTAToken token;
        token = getNextToken(ctx);

        if (token.type == TOKEN_EOL || token.type == TOKEN_EOF)
            return VTA_OK;
        
        if (token.type == TOKEN_ERROR) 
            return VTA_ERR_SYNTAX;
    }
}


/*!
 * \brief Get the first token of the next line without moving the cursor.
 *
 * \param ctx Parser context with cursor
*/
static VTAToken peekNextLine(const VTAParserContext *ctx) {
    VTAParserContext tempCtx;
    VTAToken token;
    VTAErr status;

    tempCtx = *ctx;
    memset(&token, 0, sizeof(token));

    status = skipToEOL(&tempCtx);
    if (status != VTA_OK) {
        token.type = TOKEN_ERROR;
        return token;
    }

    return getNextToken(&tempCtx);
}


/*!
 * \brief Parse the micro-operation line. It consumes the EOL/EOF token.
 * \note Expect: INT, INT, INT EOL
 * \note Example: 10, 20, 30
 * 
 * \param ctx Context parser from the input.
 * \param firstToken TODO:
 * \param parsedUop TODO:
*/
static VTAErr parseUopLine(
    VTAParserContext    *ctx,
    VTAToken            firstToken,
    VTAUop              *parsedUop
) {
    memset(parsedUop, 0, sizeof(*parsedUop));

    if (firstToken.type != TOKEN_INT)
        return VTA_ERR_SYNTAX;
    
    if (firstToken.value < 0)
        return VTA_ERR_OUT_OF_RANGE;

    parsedUop->dst_idx = (uint32_t)firstToken.value;

    VTAToken token;
    token = getNextToken(ctx);

    if (token.type != TOKEN_PUNCTUATION || strcmp(token.text, ",") != 0) 
        return VTA_ERR_SYNTAX;

    token = getNextToken(ctx);
    if (token.type != TOKEN_INT)
        return VTA_ERR_SYNTAX;
    
    if (token.value < 0)
        return VTA_ERR_OUT_OF_RANGE;

    parsedUop->src_idx = (uint32_t)token.value;

    token = getNextToken(ctx);

    if (token.type != TOKEN_PUNCTUATION || strcmp(token.text, ",") != 0) 
        return VTA_ERR_SYNTAX;

    token = getNextToken(ctx);
    if (token.type != TOKEN_INT)
        return VTA_ERR_SYNTAX;
    
    if (token.value < 0) 
        return VTA_ERR_OUT_OF_RANGE;

    parsedUop->wgt_idx = (uint32_t)token.value;

    token = getNextToken(ctx);
    if (token.type != TOKEN_EOL && token.type != TOKEN_EOF)
        return VTA_ERR_SYNTAX;

    return VTA_OK;
}


/*!
 * \brief Parse the end of an uop. It must end with a "end of line"/"end of file" token.
 * 
 * \param ctx Context for the input.
*/
static VTAErr parseInstructionEnd(VTAParserContext *ctx) {
    VTAToken token;
    token = getNextToken(ctx);

    if (token.type == TOKEN_EOL || token.type == TOKEN_EOF)
        return VTA_OK;

    return VTA_ERR_SYNTAX;
}


static VTAErr parseRange(VTAParserContext *ctx, uint32_t *start, uint32_t *end) {
    VTAErr status;
    status = expectToken(ctx, TOKEN_PUNCTUATION, "[");
    if (status != VTA_OK)
        return status;

    VTAToken token;
    token = getNextToken(ctx);

    if (token.type != TOKEN_INT)
        return VTA_ERR_SYNTAX;
    if (token.value < 0)
        return VTA_ERR_OUT_OF_RANGE;
    
    *start = (uint32_t)token.value;

    status = expectToken(ctx, TOKEN_PUNCTUATION, ":");
    if (status != VTA_OK)
        return status;

    token = getNextToken(ctx);

    if (token.type != TOKEN_INT)
        return VTA_ERR_SYNTAX;
    if (token.value < 0)
        return VTA_ERR_OUT_OF_RANGE;
    
    *end = (uint32_t)token.value;

    if (*start > *end) 
        return VTA_ERR_OUT_OF_RANGE;
    
    status = expectToken(ctx, TOKEN_PUNCTUATION, "]");
    
    return status;
}


static VTAErr parseMemPadding(VTAParserContext *ctx, VTAParsedInsn *parsedInsn) {
    VTAErr status;
    status = expectToken(ctx, TOKEN_PUNCTUATION, "(");
    if (status != VTA_OK)
        return status;
    
    uint32_t values[N_MEM_PADDING_FACTORS];

    for (int i = 0; i < N_MEM_PADDING_FACTORS; i++) {
        VTAToken token;
        token = getNextToken(ctx);

        if (token.type != TOKEN_INT)
            return VTA_ERR_SYNTAX;

        if (token.value < 0) 
            return VTA_ERR_OUT_OF_RANGE;

        uint64_t maxValue;
        maxValue = (UINT64_C(1) << VTA_MEMOP_PAD_BIT_WIDTH) - 1;

        if ((uint64_t)token.value > maxValue)
            return VTA_ERR_OUT_OF_RANGE;

        values[i] = (uint32_t)token.value;

        if (i < N_MEM_PADDING_FACTORS - 1) {
            status = expectToken(ctx, TOKEN_PUNCTUATION, ",");
            if (status != VTA_OK)
                return status;
        }
    }

    status = expectToken(ctx, TOKEN_PUNCTUATION, ")");
    if (status != VTA_OK)
        return status;

    parsedInsn->data.mem.y_pad_0 = values[0];
    parsedInsn->data.mem.y_pad_1 = values[1];
    parsedInsn->data.mem.x_pad_0 = values[2];
    parsedInsn->data.mem.x_pad_1 = values[3];

    return VTA_OK;
}


static VTAErr parseOptionalMemPadding(VTAParserContext *ctx, VTAParsedInsn *parsedInsn) {
    VTAToken token;
    token = peekNextToken(ctx);

    if (
        token.type == TOKEN_EOL ||
        token.type == TOKEN_EOF 
    )
        return parseInstructionEnd(ctx);

    token = getNextToken(ctx);

    if (
        token.type != TOKEN_IDENTIFIER ||
        strcmp(token.text, "PADDING") != 0
    )
        return VTA_ERR_SYNTAX;

    VTAErr status;
    status = parseMemPadding(ctx, parsedInsn);

    if (status != VTA_OK)
        return status;

    return parseInstructionEnd(ctx);
}


static VTAErr parseLoad(VTAParserContext *ctx, VTAParsedInsn *parsedInsn) {
    memset(parsedInsn, 0, sizeof(*parsedInsn));

    parsedInsn->kind            = ASM_LOAD;
    parsedInsn->data.mem.opcode = VTA_OPCODE_LOAD;

    VTAErr status;
    status = expectToken(ctx, TOKEN_PUNCTUATION, "(");
    if (status != VTA_OK)
        return status;

    VTAToken token;
    token = getNextToken(ctx);

    if (token.type != TOKEN_IDENTIFIER) 
        return VTA_ERR_SYNTAX;
    
    if (strcmp(token.text, "UOP") == 0)
        parsedInsn->data.mem.memory_type = VTA_MEM_ID_UOP;
    else if (strcmp(token.text, "INP") == 0)
        parsedInsn->data.mem.memory_type = VTA_MEM_ID_INP;
    else if (strcmp(token.text, "WGT") == 0)
        parsedInsn->data.mem.memory_type = VTA_MEM_ID_WGT;
    else if (strcmp(token.text, "ACC") == 0)
        parsedInsn->data.mem.memory_type = VTA_MEM_ID_ACC;
    else
        return VTA_ERR_UNKNOWN_MNEMONIC;

    status = expectToken(ctx, TOKEN_PUNCTUATION, "[");
    if (status != VTA_OK)
        return status;

    token = getNextToken(ctx);

    if (token.type != TOKEN_INT)
        return VTA_ERR_SYNTAX;

    if (token.value < 0) 
        return VTA_ERR_OUT_OF_RANGE;
    
    parsedInsn->data.mem.sram_base = (uint32_t)token.value;

    status = expectToken(ctx, TOKEN_PUNCTUATION, "]");
    if (status != VTA_OK)
        return VTA_ERR_SYNTAX;
    
    status = expectToken(ctx, TOKEN_PUNCTUATION, ",");
    if (status != VTA_OK)
        return status;

    status = expectToken(ctx, TOKEN_IDENTIFIER, "MEM");
    if (status != VTA_OK)
        return status;

    status = expectToken(ctx, TOKEN_PUNCTUATION, "[");
    if (status != VTA_OK)
        return VTA_ERR_SYNTAX;

    token = getNextToken(ctx);

    if (token.type != TOKEN_INT)
        return VTA_ERR_SYNTAX;

    if (token.value < 0) 
        return VTA_ERR_OUT_OF_RANGE;

    parsedInsn->data.mem.dram_base = (uint32_t)token.value;

    if (parsedInsn->data.mem.memory_type == VTA_MEM_ID_UOP) {
        status = expectToken(ctx, TOKEN_PUNCTUATION, ",");
        if (status != VTA_OK)
            return status;

        token = getNextToken(ctx);
        
        if (token.type != TOKEN_INT)
            return VTA_ERR_SYNTAX;
        if (token.value <= 0) 
            return VTA_ERR_OUT_OF_RANGE;
        
        parsedInsn->data.mem.x_size = (uint32_t)token.value;

        status = expectToken(ctx, TOKEN_PUNCTUATION, "]");
        if (status != VTA_OK)
            return status;
    } else {
        status = expectToken(ctx, TOKEN_PUNCTUATION, ",");
        if (status != VTA_OK)
            return status;

        token = getNextToken(ctx);

        if (token.type != TOKEN_INT) 
            return VTA_ERR_SYNTAX;
        if (token.value <= 0)
            return VTA_ERR_OUT_OF_RANGE;

        parsedInsn->data.mem.y_size = (uint32_t)token.value;

        status = expectToken(ctx, TOKEN_PUNCTUATION, ",");
        if (status != VTA_OK)
            return status;
        
        token = getNextToken(ctx);

        if (token.type != TOKEN_INT)
            return VTA_ERR_SYNTAX;
        if (token.value <= 0)
            return VTA_ERR_OUT_OF_RANGE;
        
        parsedInsn->data.mem.x_size = (uint32_t)token.value;

        status = expectToken(ctx, TOKEN_PUNCTUATION, ",");
        if (status != VTA_OK)
            return status;

        token = getNextToken(ctx);

        if (token.type != TOKEN_INT)
            return VTA_ERR_SYNTAX;
        if (token.value < 0)
            return VTA_ERR_OUT_OF_RANGE;

        parsedInsn->data.mem.x_stride = (uint32_t)token.value;
        
        
        status = expectToken(ctx, TOKEN_PUNCTUATION, "]");
        if (status != VTA_OK)
            return status;
    }

    status = expectToken(ctx, TOKEN_PUNCTUATION, ")");
    if (status != VTA_OK)
        return VTA_ERR_SYNTAX;

    return parseOptionalMemPadding(ctx, parsedInsn);
}


static VTAErr parseStore(VTAParserContext *ctx, VTAParsedInsn *parsedInsn) {
    memset(parsedInsn, 0, sizeof(*parsedInsn));

    parsedInsn->kind                    = ASM_STORE;
    parsedInsn->data.mem.opcode         = VTA_OPCODE_STORE;
    parsedInsn->data.mem.memory_type    = VTA_MEM_ID_OUT;

    VTAErr status;

    status = expectToken(ctx, TOKEN_PUNCTUATION, "(");
    if (status != VTA_OK)
        return status;

    status = expectToken(ctx, TOKEN_IDENTIFIER, "MEM");
    if (status != VTA_OK)
        return status;

    status = expectToken(ctx, TOKEN_PUNCTUATION, "[");
    if (status != VTA_OK)
        return status;

    VTAToken token;
    token = getNextToken(ctx); // * dram_base

    if (token.type != TOKEN_INT)
        return VTA_ERR_SYNTAX;
    if (token.value < 0)
        return VTA_ERR_OUT_OF_RANGE;

    parsedInsn->data.mem.dram_base = (uint32_t)token.value;

    status = expectToken(ctx, TOKEN_PUNCTUATION, ",");
    if (status != VTA_OK)
        return status;

    token = getNextToken(ctx); // * y_size

    if (token.type != TOKEN_INT)
        return VTA_ERR_SYNTAX;
    if (token.value <= 0)
        return VTA_ERR_OUT_OF_RANGE;

    parsedInsn->data.mem.y_size = (uint32_t)token.value;

    status = expectToken(ctx, TOKEN_PUNCTUATION, ",");
    if (status != VTA_OK)
        return status;

    token = getNextToken(ctx); // * x_size
    
    if (token.type != TOKEN_INT)
        return VTA_ERR_SYNTAX;
    if (token.value <= 0)
        return VTA_ERR_OUT_OF_RANGE;
    
    parsedInsn->data.mem.x_size = (uint32_t)token.value;

    status = expectToken(ctx, TOKEN_PUNCTUATION, ",");
    if (status != VTA_OK)
        return status;
    
    token = getNextToken(ctx); // * x_stride

    if (token.type != TOKEN_INT)
        return VTA_ERR_SYNTAX;
    if (token.value < 0)
        return VTA_ERR_OUT_OF_RANGE;

    parsedInsn->data.mem.x_stride = (uint32_t)token.value;

    status = expectToken(ctx, TOKEN_PUNCTUATION, "]");
    if (status != VTA_OK)
        return status;

    status = expectToken(ctx, TOKEN_PUNCTUATION, ",");
    if (status != VTA_OK) 
        return status;

    status = expectToken(ctx, TOKEN_IDENTIFIER, "ACC");
    if (status != VTA_OK)
        return status;

    status = expectToken(ctx, TOKEN_PUNCTUATION, "[");
    if (status != VTA_OK)
        return status;
    
    token = getNextToken(ctx); // * sram_base
    
    if (token.type != TOKEN_INT)
        return VTA_ERR_SYNTAX;
    if (token.value < 0)
        return VTA_ERR_OUT_OF_RANGE;

    parsedInsn->data.mem.sram_base = (uint32_t)token.value;

    status = expectToken(ctx, TOKEN_PUNCTUATION, "]");
    if (status != VTA_OK)
        return status;

    status = expectToken(ctx, TOKEN_PUNCTUATION, ")");
    if (status != VTA_OK)
        return status;

    return parseOptionalMemPadding(ctx, parsedInsn);
}


static VTAErr parseGemmFactors(VTAParserContext *ctx, VTAParsedInsn *parsedInsn) {
    VTAErr status;
    status = expectToken(ctx, TOKEN_PUNCTUATION, "(");
    if (status != VTA_OK)
        return status;

    uint32_t values[N_GEMM_FACTORS];
    uint32_t widths[N_GEMM_FACTORS] = {
        VTA_LOG_ACC_BUFF_DEPTH,
        VTA_LOG_ACC_BUFF_DEPTH,
        VTA_LOG_INP_BUFF_DEPTH,
        VTA_LOG_INP_BUFF_DEPTH,
        VTA_LOG_WGT_BUFF_DEPTH,
        VTA_LOG_WGT_BUFF_DEPTH,
    };

    for (int i = 0; i < N_GEMM_FACTORS; i++) {
        VTAToken token;
        token = getNextToken(ctx);

        if (token.type != TOKEN_INT)
            return VTA_ERR_SYNTAX;
        if (token.value < 0)
            return VTA_ERR_OUT_OF_RANGE;

        uint64_t maxValue;
        maxValue = (UINT64_C(1) << widths[i]) - 1;

        if ((uint64_t)token.value > maxValue)
            return VTA_ERR_OUT_OF_RANGE;

        values[i] = (uint32_t)token.value;

        if (i < N_GEMM_FACTORS - 1) {
            status = expectToken(ctx, TOKEN_PUNCTUATION, ",");
            if (status != VTA_OK)
                return status;
        }
    }

    status = expectToken(ctx, TOKEN_PUNCTUATION, ")");
    if (status != VTA_OK)
        return status;

    parsedInsn->data.gemm.dst_factor_out = values[0];
    parsedInsn->data.gemm.dst_factor_in  = values[1];
    parsedInsn->data.gemm.src_factor_out = values[2];
    parsedInsn->data.gemm.src_factor_in  = values[3];
    parsedInsn->data.gemm.wgt_factor_out = values[4];
    parsedInsn->data.gemm.wgt_factor_in  = values[5];

    return VTA_OK;
}


static VTAErr parseAluFactors(VTAParserContext *ctx, VTAParsedInsn *parsedInsn) {
    VTAErr status;
    status = expectToken(ctx, TOKEN_PUNCTUATION, "(");
    if (status != VTA_OK)
        return status;

    uint32_t values[N_ALU_FACTORS];

    for (int i = 0; i < N_ALU_FACTORS; i++) {
        VTAToken token;
        token = getNextToken(ctx);

        if (token.type != TOKEN_INT)
            return VTA_ERR_SYNTAX;
        
        if (token.value < 0) 
            return VTA_ERR_OUT_OF_RANGE;

        uint64_t maxValue;
        maxValue = (UINT64_C(1) << VTA_LOG_ACC_BUFF_DEPTH - 1);

        if ((uint64_t)token.value > maxValue)
            return VTA_ERR_OUT_OF_RANGE;

        values[i] = (uint32_t)token.value;
        
        if (i < N_ALU_FACTORS - 1) {
            status = expectToken(ctx, TOKEN_PUNCTUATION, ",");
            if (status != VTA_OK)
                return status;
        }
    }

    status = expectToken(ctx, TOKEN_PUNCTUATION, ")");
    if (status != VTA_OK)
        return status;

    parsedInsn->data.alu.dst_factor_out = values[0];
    parsedInsn->data.alu.dst_factor_in  = values[1];
    parsedInsn->data.alu.src_factor_out = values[2];
    parsedInsn->data.alu.src_factor_in  = values[3];

    return VTA_OK;
}


static VTAErr parseFor(VTAParserContext *ctx, VTAParsedInsn *parsedInsn, int isGemm) {
    VTAErr status;
    status = expectToken(ctx, TOKEN_PUNCTUATION, "(");
    if (status != VTA_OK)
        return status;

    VTAToken token;
    token = getNextToken(ctx);
    if (token.type != TOKEN_INT)
        return VTA_ERR_SYNTAX;
    if (token.value <= 0) 
        return VTA_ERR_OUT_OF_RANGE;
    
    if (isGemm)
        parsedInsn->data.gemm.iter_out = (uint32_t)token.value;
    else
        parsedInsn->data.alu.iter_out  = (uint32_t)token.value;

    status = expectToken(ctx, TOKEN_PUNCTUATION, ",");
    if (status != VTA_OK)
        return status;
    
    token = getNextToken(ctx);
    if (token.type != TOKEN_INT)
        return VTA_ERR_SYNTAX;
    if (token.value <= 0)
        return VTA_ERR_OUT_OF_RANGE;

    if (isGemm)
        parsedInsn->data.gemm.iter_in = (uint32_t)token.value;
    else
        parsedInsn->data.alu.iter_in  = (uint32_t)token.value;

    status = expectToken(ctx, TOKEN_PUNCTUATION, ")");
    if (status != VTA_OK)
        return status;

    status = expectToken(ctx, TOKEN_IDENTIFIER, "UOP");
    if (status != VTA_OK)
        return status;

    status = expectToken(ctx, TOKEN_PUNCTUATION, "(");
    if (status != VTA_OK)
        return status;

    token = getNextToken(ctx);
    if (token.type != TOKEN_IDENTIFIER)
        return VTA_ERR_SYNTAX;
    
    strncpy(parsedInsn->uopBgnLabel, token.text, TEXT_SIZE - 1);
    parsedInsn->uopBgnLabel[TEXT_SIZE-1] = '\0';

    status = expectToken(ctx, TOKEN_PUNCTUATION, ",");
    if (status != VTA_OK)
        return status;

    token = getNextToken(ctx);
    if (token.type != TOKEN_IDENTIFIER)
        return VTA_ERR_SYNTAX;
    
    strncpy(parsedInsn->uopEndLabel, token.text, TEXT_SIZE - 1);
    parsedInsn->uopEndLabel[TEXT_SIZE-1] = '\0';

    status = expectToken(ctx, TOKEN_PUNCTUATION, ")");
    if (status != VTA_OK)
        return status;

    // * factors part
    VTAToken nextToken;
    nextToken = peekNextToken(ctx);

    if (
        nextToken.type == TOKEN_EOL ||
        nextToken.type == TOKEN_EOF
    )
        return parseInstructionEnd(ctx);
    
    nextToken = getNextToken(ctx);

    if (
        nextToken.type != TOKEN_IDENTIFIER      ||
        strcmp(nextToken.text, "FACTORS") != 0
    )
        return VTA_ERR_SYNTAX;

    if (isGemm)
        status = parseGemmFactors(ctx, parsedInsn);
    else
        status = parseAluFactors(ctx, parsedInsn);

    if (status != VTA_OK)
        return status;

    return parseInstructionEnd(ctx);
}


static VTAErr parseGemm(VTAParserContext *ctx, VTAParsedInsn *parsedInsn) {
    VTAErr status;
    status = expectToken(ctx, TOKEN_PUNCTUATION, "(");
    if (status != VTA_OK)
        return status;

    status = expectToken(ctx, TOKEN_IDENTIFIER, "ACC");
    if (status != VTA_OK)
        return status;

    uint32_t accBgn, accEnd;
    status = parseRange(ctx, &accBgn, &accEnd);
    if (status != VTA_OK)
        return status;

    status = expectToken(ctx, TOKEN_PUNCTUATION, ",");
    if (status != VTA_OK)
        return status;
    
    status = expectToken(ctx, TOKEN_IDENTIFIER, "INP");
    if (status != VTA_OK)
        return status;

    uint32_t inpBgn, inpEnd;
    status = parseRange(ctx, &inpBgn, &inpEnd);
    if (status != VTA_OK)
        return status;

    status = expectToken(ctx, TOKEN_PUNCTUATION, ",");
    if (status != VTA_OK)
        return status;

    status = expectToken(ctx, TOKEN_IDENTIFIER, "WGT");
    if (status != VTA_OK)
        return status;

    uint32_t wgtBgn, wgtEnd;
    status = parseRange(ctx, &wgtBgn, &wgtEnd);
    if (status != VTA_OK)
        return status;

    // * the 3 ranges must be the same UOP range
    if (
        accBgn != inpBgn ||
        accBgn != wgtBgn ||
        accEnd != inpEnd ||
        accEnd != wgtEnd
    ) {
        return VTA_ERR_SYNTAX;
    }

    status = expectToken(ctx, TOKEN_PUNCTUATION, ")");
    if (status != VTA_OK)
        return status;

    status = parseInstructionEnd(ctx);
    if (status != VTA_OK)
        return status;

    parsedInsn->data.gemm.uop_bgn = accBgn;
    parsedInsn->data.gemm.uop_end = accEnd;

    return VTA_OK;
}


static VTAErr parseGemmRst(VTAParserContext *ctx, VTAParsedInsn *parsedInsn) {
    VTAErr status;
    status = expectToken(ctx, TOKEN_PUNCTUATION, "(");
    if (status != VTA_OK)
        return status;

    status = expectToken(ctx, TOKEN_IDENTIFIER, "ACC");
    if (status != VTA_OK)
        return status;

    uint32_t uopBgn, uopEnd;
    status = parseRange(ctx, &uopBgn, &uopEnd);
    if (status != VTA_OK)
        return status;

    status = expectToken(ctx, TOKEN_PUNCTUATION, ")");
    if (status != VTA_OK)
        return status;

    status = parseInstructionEnd(ctx);
    if (status != VTA_OK)
        return status;

    parsedInsn->data.gemm.uop_bgn = uopBgn;
    parsedInsn->data.gemm.uop_end = uopEnd;

    return VTA_OK;
}


static VTAErr resolveUopLabels(
    const VTALT *labelTable,
    VTAParsedInsn *parsedInsn
) {
    int uopBgn;
    int uopEnd;

    uopBgn = LT_find(
        labelTable,
        parsedInsn->uopBgnLabel
    );

    if (uopBgn < 0)
        return VTA_ERR_LABEL_NOT_FOUND;

    uopEnd = LT_find(
        labelTable,
        parsedInsn->uopEndLabel
    );

    if (uopEnd < 0)
        return VTA_ERR_LABEL_NOT_FOUND;

    if (uopBgn > uopEnd)
        return VTA_ERR_OUT_OF_RANGE;

    if (
        parsedInsn->kind == ASM_GEMM ||
        parsedInsn->kind == ASM_GEMM_RST
    ) {
        if (
            parsedInsn->data.gemm.uop_bgn != (uint32_t)uopBgn ||
            parsedInsn->data.gemm.uop_end != (uint32_t)uopEnd
        )
            return VTA_ERR_SYNTAX;

        parsedInsn->data.gemm.uop_bgn = (uint32_t)uopBgn;
        parsedInsn->data.gemm.uop_end = (uint32_t)uopEnd;
        return VTA_OK;
    }

    if (parsedInsn->kind == ASM_ALU) {
        if (
            parsedInsn->data.alu.uop_bgn != (uint32_t)uopBgn ||
            parsedInsn->data.alu.uop_end != (uint32_t)uopEnd
        )
            return VTA_ERR_SYNTAX;

        parsedInsn->data.alu.uop_bgn = (uint32_t)uopBgn;
        parsedInsn->data.alu.uop_end = (uint32_t)uopEnd;
        return VTA_OK;
    }

    return VTA_ERR_SYNTAX;
}


static VTAErr parseAlu(
    VTAParserContext *ctx,
    VTAParsedInsn *parsedInsn
) {
    VTAErr status;

    status = expectToken(
        ctx,
        TOKEN_PUNCTUATION,
        "("
    );

    if (status != VTA_OK)
        return status;

    status = expectToken(
        ctx,
        TOKEN_IDENTIFIER,
        "DST"
    );

    if (status != VTA_OK)
        return status;

    uint32_t dstBgn;
    uint32_t dstEnd;

    status = parseRange(
        ctx,
        &dstBgn,
        &dstEnd
    );

    if (status != VTA_OK)
        return status;

    status = expectToken(
        ctx,
        TOKEN_PUNCTUATION,
        ","
    );

    if (status != VTA_OK)
        return status;

    VTAToken token;
    token = getNextToken(ctx);

    /*
     * Immediate form:
     *
     * ALU.ADD(DST[0:2], -7)
     */
    if (token.type == TOKEN_INT) {
        if (token.value < INT16_MIN || token.value > INT16_MAX)
            return VTA_ERR_OUT_OF_RANGE;

        parsedInsn->data.alu.use_imm = 1;
        parsedInsn->data.alu.imm = (int16_t)token.value;
    /*
     * Source form:
     *
     * ALU.ADD(DST[0:2], SRC[0:2])
     */
    } else if (
        token.type == TOKEN_IDENTIFIER &&
        strcmp(token.text, "SRC") == 0
    ) {
        parsedInsn->data.alu.use_imm = 0;

        uint32_t srcBgn;
        uint32_t srcEnd;

        status = parseRange(
            ctx,
            &srcBgn,
            &srcEnd
        );

        if (status != VTA_OK)
            return status;

        if (
            dstBgn != srcBgn ||
            dstEnd != srcEnd
        )
            return VTA_ERR_SYNTAX;
    } else {
        return VTA_ERR_SYNTAX;
    }

    status = expectToken(
        ctx,
        TOKEN_PUNCTUATION,
        ")"
    );

    if (status != VTA_OK)
        return status;

    status = parseInstructionEnd(ctx);

    if (status != VTA_OK)
        return status;

    parsedInsn->data.alu.uop_bgn = dstBgn;
    parsedInsn->data.alu.uop_end = dstEnd;

    return VTA_OK;
}


static VTAErr parseAluOpcode(
    const char *text,
    uint32_t *opcode
) {
    if (strcmp(text, "ALU.MIN") == 0) {
        *opcode = VTA_ALU_OPCODE_MIN;
        return VTA_OK;
    }

    if (strcmp(text, "ALU.MAX") == 0) {
        *opcode = VTA_ALU_OPCODE_MAX;
        return VTA_OK;
    }

    if (strcmp(text, "ALU.ADD") == 0) {
        *opcode = VTA_ALU_OPCODE_ADD;
        return VTA_OK;
    }

    if (strcmp(text, "ALU.SHR") == 0) {
        *opcode = VTA_ALU_OPCODE_SHR;
        return VTA_OK;
    }

    if (strcmp(text, "ALU.MUL") == 0) {
        *opcode = VTA_ALU_OPCODE_MUL;
        return VTA_OK;
    }

    return VTA_ERR_UNKNOWN_MNEMONIC;
}


static int isAluMnemonic(const char *text) {
    return strcmp(text, "ALU.MIN") == 0 ||
           strcmp(text, "ALU.MAX") == 0 ||
           strcmp(text, "ALU.ADD") == 0 ||
           strcmp(text, "ALU.SHR") == 0 ||
           strcmp(text, "ALU.MUL") == 0 ;
}

/*!
 * \brief Parse the complete two-line FOR instruction.
 *
 * First line:
 * FOR (iter_out, iter_in) UOP (label_bgn, label_end)
 *
 * Second line:
 * GEMM(...), GEMM.RST(...) or ALU.<OP>(...)
 */
static VTAErr parseForInstruction(
    VTAParserContext *ctx,
    VTAParsedInsn *parsedInsn
) {
    VTAToken nextLineToken;
    VTAToken token;
    VTAErr status;
    int isGemm;

    memset(parsedInsn, 0, sizeof(*parsedInsn));

    nextLineToken = peekNextLine(ctx);

    if (nextLineToken.type != TOKEN_IDENTIFIER)
        return VTA_ERR_SYNTAX;

    isGemm =
        strcmp(nextLineToken.text, "GEMM") == 0 ||
        strcmp(nextLineToken.text, "GEMM.RST") == 0;

    if (!isGemm && !isAluMnemonic(nextLineToken.text))
        return VTA_ERR_UNKNOWN_MNEMONIC;

    status = parseFor(
        ctx,
        parsedInsn,
        isGemm
    );

    if (status != VTA_OK)
        return status;

    token = getNextToken(ctx);

    if (token.type != TOKEN_IDENTIFIER)
        return VTA_ERR_SYNTAX;

    if (strcmp(token.text, "GEMM") == 0) {
        parsedInsn->kind = ASM_GEMM;
        parsedInsn->data.gemm.opcode = VTA_OPCODE_GEMM;
        parsedInsn->data.gemm.reset_reg = 0;

        return parseGemm(
            ctx,
            parsedInsn
        );
    }

    if (strcmp(token.text, "GEMM.RST") == 0) {
        parsedInsn->kind = ASM_GEMM_RST;
        parsedInsn->data.gemm.opcode = VTA_OPCODE_GEMM;
        parsedInsn->data.gemm.reset_reg = 1;

        return parseGemmRst(
            ctx,
            parsedInsn
        );
    }

    uint32_t aluOpcode;
    status = parseAluOpcode(token.text, &aluOpcode);
    if (status != VTA_OK)
        return status;

    parsedInsn->kind                = ASM_ALU;
    parsedInsn->data.alu.opcode     = VTA_OPCODE_ALU;
    parsedInsn->data.alu.reset_reg  = 0;
    parsedInsn->data.alu.alu_opcode = aluOpcode;

    return parseAlu(ctx, parsedInsn);
}

/*!
 * \todo 
*/
static VTAErr parseInstruction(VTAParserContext *ctx, const VTAToken *token, VTAParsedInsn *parsedInsn) {
    if (strcmp(token->text, "FINISH") == 0) {
        memset(parsedInsn, 0, sizeof(*parsedInsn));

        parsedInsn->kind            = ASM_FINISH;
        parsedInsn->data.mem.opcode = VTA_OPCODE_FINISH;

        return parseInstructionEnd(ctx);
    }

    if (strcmp(token->text, "NOOP") == 0) {
        memset(parsedInsn, 0, sizeof(*parsedInsn));

        parsedInsn->kind            = ASM_NOOP;
        parsedInsn->data.mem.opcode = VTA_OPCODE_LOAD;
        parsedInsn->data.mem.x_size = 0;

        return parseInstructionEnd(ctx);
    }

    if (strcmp(token->text, "LOAD") == 0)
        return parseLoad(ctx, parsedInsn);

    if (strcmp(token->text, "STORE") == 0 || strcmp(token->text, "STOR") == 0)
        return parseStore(ctx, parsedInsn);

    if (strcmp(token->text, "FOR") == 0) 
        return parseForInstruction(ctx, parsedInsn);

    return VTA_ERR_UNKNOWN_MNEMONIC;
}


static VTAErr emitInstruction(
    const VTAParsedInsn *parsedInsn,
    VTAGenericInsn      *insnBuffer,
    int                  maxInsn,
    int                 *numInsn
) {
    if (*numInsn >= maxInsn)
        return VTA_ERR_INSN_BUFFER_FULL;

    switch (parsedInsn->kind) {
        case ASM_LOAD:
        case ASM_STORE:
        case ASM_FINISH:
        case ASM_NOOP:
            memcpy(
                &insnBuffer[*numInsn],
                &parsedInsn->data.mem,
                sizeof(VTAMemInsn)
            );
            (*numInsn)++;
            return VTA_OK;

        case ASM_GEMM:
        case ASM_GEMM_RST:
            memcpy(
                &insnBuffer[*numInsn],
                &parsedInsn->data.gemm,
                sizeof(VTAGemInsn)
            );
            (*numInsn)++;
            return VTA_OK;

        case ASM_ALU:
            memcpy(
                &insnBuffer[*numInsn],
                &parsedInsn->data.alu,
                sizeof(VTAAluInsn)
            );
            (*numInsn)++;
            return VTA_OK;

        default:
            return VTA_ERR_UNKNOWN_MNEMONIC;
    }
}


static VTAErr parseDependencyLine(
    VTAParserContext            *ctx, 
    const VTAToken              *firstToken, 
    VTADependenciesTransition   *edges
) {
    memset(edges, 0, sizeof(*edges));

    if (strcmp(firstToken->text, "POP") != 0 && strcmp(firstToken->text, "PUSH") != 0)
        return VTA_ERR_SYNTAX;

    VTAErr status;
    status = expectToken(ctx, TOKEN_PUNCTUATION, "(");
    if (status != VTA_OK)
        return status;

    while (1) {
        char source[8], target[8];

        VTAToken token;
        token = getNextToken(ctx);
        if (token.type != TOKEN_IDENTIFIER) 
            return VTA_ERR_SYNTAX;
        
        strncpy(source, token.text, sizeof(source)-1);
        source[sizeof(source) - 1] = '\0';

        status = expectToken(ctx, TOKEN_PUNCTUATION, "->");
        if (status != VTA_OK)
            return VTA_ERR_SYNTAX;

        token = getNextToken(ctx);
        if (token.type != TOKEN_IDENTIFIER)
            return VTA_ERR_SYNTAX;

        strncpy(target, token.text, sizeof(target) - 1);
        target[sizeof(target) - 1] = '\0';

        if (strcmp(source, "EX") == 0 && strcmp(target, "LD") == 0) {
            if (edges->ex_ld)
                return VTA_ERR_SYNTAX;
            edges->ex_ld = 1;
        } else if (strcmp(source, "EX") == 0 && strcmp(target, "ST") == 0) {
            if (edges->ex_st)
                return VTA_ERR_SYNTAX;
            edges->ex_st = 1;
        } else if (strcmp(source, "LD") == 0 && strcmp(target, "EX") == 0) {
            if (edges->ld_ex)
                return VTA_ERR_SYNTAX;
            edges->ld_ex = 1;
        } else if (strcmp(source, "ST") == 0 && strcmp(target, "EX") == 0) {
            if (edges->st_ex)
                return VTA_ERR_SYNTAX;
            edges->st_ex = 1;
        } else {
            return VTA_ERR_SYNTAX;
        }

        token = getNextToken(ctx);
        if (token.type == TOKEN_PUNCTUATION && strcmp(token.text, ",") == 0)
            continue;
        
        if (token.type == TOKEN_PUNCTUATION && strcmp(token.text, ")") == 0)
            break;

        return VTA_ERR_SYNTAX;
    }   

    return parseInstructionEnd(ctx);
}


static VTAErr applyDependencies(
    VTAParsedInsn                   *parsedInsn,
    const VTADependenciesTransition *popEdges,
    const VTADependenciesTransition *pushEdges
) {
    memset(&parsedInsn->deps, 0, sizeof(parsedInsn->deps));

    switch (parsedInsn->kind) {
        case ASM_LOAD:
            if (
                popEdges->ex_st  ||
                popEdges->ld_ex  ||
                popEdges->st_ex  ||
                pushEdges->ex_ld ||
                pushEdges->ex_st ||
                pushEdges->st_ex 
            )
                return VTA_ERR_SYNTAX;

            parsedInsn->deps.pop_prev  = popEdges->ex_ld;
            parsedInsn->deps.push_prev = pushEdges->ld_ex;

            parsedInsn->data.mem.pop_prev_dep  = parsedInsn->deps.pop_prev;
            parsedInsn->data.mem.push_prev_dep = parsedInsn->deps.push_prev;

            break;

        case ASM_STORE:
            if (
                popEdges->ex_ld  ||
                popEdges->ld_ex  ||
                popEdges->st_ex  ||
                pushEdges->ex_ld ||
                pushEdges->ex_st ||
                pushEdges->ld_ex
            )
                return VTA_ERR_SYNTAX;
            
            parsedInsn->deps.pop_prev  = popEdges->ex_st;
            parsedInsn->deps.push_prev = pushEdges->st_ex;

            parsedInsn->data.mem.pop_prev_dep  = parsedInsn->deps.pop_prev;
            parsedInsn->data.mem.push_prev_dep = parsedInsn->deps.push_prev;

            break;

        case ASM_GEMM:
        case ASM_GEMM_RST:
        case ASM_ALU:
            parsedInsn->deps.pop_prev  = popEdges->ld_ex;
            parsedInsn->deps.pop_next  = popEdges->st_ex;
            parsedInsn->deps.push_prev = pushEdges->ex_ld;
            parsedInsn->deps.push_next = pushEdges->ex_st;

            if (parsedInsn->kind == ASM_ALU) {
                parsedInsn->data.alu.pop_prev_dep  = parsedInsn->deps.pop_prev;
                parsedInsn->data.alu.pop_next_dep  = parsedInsn->deps.pop_next;
                parsedInsn->data.alu.push_prev_dep = parsedInsn->deps.push_prev;
                parsedInsn->data.alu.push_next_dep = parsedInsn->deps.push_next;
            } else {
                parsedInsn->data.gemm.pop_prev_dep  = parsedInsn->deps.pop_prev;
                parsedInsn->data.gemm.pop_next_dep  = parsedInsn->deps.pop_next;
                parsedInsn->data.gemm.push_prev_dep = parsedInsn->deps.push_prev;
                parsedInsn->data.gemm.push_next_dep = parsedInsn->deps.push_next;
            }

            break;
        
        case ASM_NOOP:
        case ASM_FINISH:
            if (
                popEdges->ex_ld  ||
                popEdges->ex_st  ||
                popEdges->ld_ex  ||
                popEdges->st_ex  ||
                pushEdges->ex_ld ||
                pushEdges->ex_st ||
                pushEdges->ld_ex ||
                pushEdges->st_ex
            )
                return VTA_ERR_SYNTAX;

            break;
        default:
            return VTA_ERR_SYNTAX;
    }

    return VTA_OK;
}


/*!
 * \brief Fills the label table.
 *
 * \param asmCode
*/
static VTAErr makeLT(
    const char  *asmCode,
    VTALT       *table,
    int          maxInsn,
    int          maxUop,
    int          *estimatedInsn,
    int          *estimatedUop,
    int          *errLine
) {
    VTAParserContext ctx;

    ctx.cursor = asmCode;
    ctx.lineNum = 1;

    LT_init(table);

    uint32_t currentUopIdx;
    currentUopIdx = 0;

    int IC;
    IC = 0;

    VTADependenciesTransition pendingPop;
    memset(&pendingPop, 0, sizeof(pendingPop));

    while (1) {
        VTAToken token;
        token = getNextToken(&ctx);

        if (token.type == TOKEN_EOF)
            break;

        if (token.type == TOKEN_EOL)
            continue;

        if (token.type == TOKEN_ERROR) {
            *errLine = ctx.lineNum;
            return VTA_ERR_SYNTAX;
        }

        if (token.type == TOKEN_INT) {
            VTAUop parsedUop;
            VTAErr status;

            status = parseUopLine(&ctx, token, &parsedUop);

            if (status != VTA_OK) {
                *errLine = ctx.lineNum;
                return status;
            }

            currentUopIdx++;

            if (currentUopIdx > (uint32_t)maxUop) {
                *errLine = ctx.lineNum;
                return VTA_ERR_UOP_BUFFER_FULL;
            }

            continue;
        }

        if (token.type != TOKEN_IDENTIFIER) {
            *errLine = ctx.lineNum;
            return VTA_ERR_SYNTAX;
        }

        /*
         * Label definition.
         */
        VTAToken nextToken;
        nextToken = peekNextToken(&ctx);

        if (
            nextToken.type == TOKEN_PUNCTUATION &&
            strcmp(nextToken.text, ":") == 0
        ) {
            // * un label cant appear after a pop and before an insn
            if (
                pendingPop.ex_ld ||
                pendingPop.ex_st ||
                pendingPop.ld_ex ||
                pendingPop.st_ex 
            ) {
                *errLine = ctx.lineNum;
                return VTA_ERR_SYNTAX;
            }

            getNextToken(&ctx);

            VTAErr status;
            status = LT_add(
                table,
                token.text,
                currentUopIdx,
                ctx.lineNum
            );

            if (status != VTA_OK) {
                *errLine = ctx.lineNum;
                return status;
            }

            continue;
        }

        if (strcmp(token.text, "POP") == 0) {
            VTAErr status;

            // * to avoid two POP for the same insn
            if (
                pendingPop.ex_ld ||
                pendingPop.ex_st ||
                pendingPop.ld_ex ||
                pendingPop.st_ex
            ) {
                *errLine = ctx.lineNum;
                return VTA_ERR_SYNTAX;
            }

            status = parseDependencyLine(&ctx, &token, &pendingPop);
            if (status != VTA_OK) {
                *errLine = ctx.lineNum;
                return status;
            }

            continue;
        }

        // * PUSH without insn
        if (strcmp(token.text, "PUSH") == 0) {
            *errLine = ctx.lineNum;
            return VTA_ERR_SYNTAX;
        }

        /*
         * Every supported instruction starts here.
         * FOR consumes both its own line and the following
         * GEMM/GEMM.RST/ALU line
         */
        if (
            strcmp(token.text, "FINISH")    == 0 ||
            strcmp(token.text, "NOOP")      == 0 ||
            strcmp(token.text, "LOAD")      == 0 ||
            strcmp(token.text, "STORE")     == 0 ||
            strcmp(token.text, "STOR")      == 0 ||
            strcmp(token.text, "FOR")       == 0
        ) {
            VTAParsedInsn parsedInsn;
            VTAErr status;

            memset(&parsedInsn, 0, sizeof(parsedInsn));

            status = parseInstruction(&ctx, &token, &parsedInsn);

            if (status != VTA_OK) {
                *errLine = ctx.lineNum;
                return status;
            }

            // * check for PUSH
            VTADependenciesTransition pushEdges;
            memset(&pushEdges, 0, sizeof(pushEdges));

            nextToken = peekNextToken(&ctx);

            if (nextToken.type == TOKEN_IDENTIFIER && strcmp(nextToken.text, "PUSH") == 0) {
                nextToken = getNextToken(&ctx);

                status = parseDependencyLine(&ctx, &nextToken, &pushEdges);
                if (status != VTA_OK) {
                    *errLine = ctx.lineNum;
                    return status;
                }
            }

            status = applyDependencies(&parsedInsn, &pendingPop, &pushEdges);
            if (status != VTA_OK) {
                *errLine = ctx.lineNum;
                return status;
            }


            IC++;

            if (IC > maxInsn) {
                *errLine = ctx.lineNum;
                return VTA_ERR_INSN_BUFFER_FULL;
            }

            memset(&pendingPop, 0, sizeof(pendingPop));

            continue;
        }

        *errLine = ctx.lineNum;
        return VTA_ERR_UNKNOWN_MNEMONIC;
    }

    // * POP without insn
    if (
        pendingPop.ex_ld ||
        pendingPop.ex_st ||
        pendingPop.ld_ex ||
        pendingPop.st_ex 
    ) {
        *errLine = ctx.lineNum;
        return VTA_ERR_SYNTAX;
    }

    *estimatedInsn = IC;
    *estimatedUop = (int)currentUopIdx;

    return VTA_OK;
}


static VTAErr encodeInstructions(
    const char      *asmCode,
    VTAGenericInsn  *insnBuffer,
    int              maxInsn,
    int              *numInsn,
    VTAUop          *uopBuffer,
    int              maxUop,
    const VTALT     *labelTable
) {
    VTAParserContext ctx;
    uint32_t currentUopIdx;

    ctx.cursor = asmCode;
    ctx.lineNum = 1;

    currentUopIdx = 0;
    *numInsn = 0;

    VTADependenciesTransition pendingPop;
    memset(&pendingPop, 0, sizeof(pendingPop));

    while (1) {
        VTAToken token;
        token = getNextToken(&ctx);

        if (token.type == TOKEN_EOF)
            break;

        if (token.type == TOKEN_EOL)
            continue;

        if (token.type == TOKEN_ERROR)
            return VTA_ERR_SYNTAX;

        if (token.type == TOKEN_INT) {
            if (currentUopIdx >= (uint32_t)maxUop)
                return VTA_ERR_UOP_BUFFER_FULL;

            VTAErr status;

            status = parseUopLine(
                &ctx,
                token,
                &uopBuffer[currentUopIdx]
            );

            if (status != VTA_OK)
                return status;

            currentUopIdx++;
            continue;
        }

        if (token.type != TOKEN_IDENTIFIER)
            return VTA_ERR_SYNTAX;

        /*
         * Label definitions were already stored during the
         * first pass. Consume only the label syntax here.
         */
        VTAToken nextToken;
        nextToken = peekNextToken(&ctx);

        if (
            nextToken.type == TOKEN_PUNCTUATION &&
            strcmp(nextToken.text, ":") == 0
        ) {
            // * no label between dep
            if (
                pendingPop.ex_ld ||
                pendingPop.ex_st ||
                pendingPop.ld_ex ||
                pendingPop.st_ex
            )
                return VTA_ERR_SYNTAX;

            getNextToken(&ctx);
            continue;
        }

        if (strcmp(token.text, "POP") == 0) {
            VTAErr status;

            if (
                pendingPop.ex_ld ||
                pendingPop.ex_st ||
                pendingPop.ld_ex ||
                pendingPop.st_ex
            )
                return VTA_ERR_SYNTAX;

            status = parseDependencyLine(
                &ctx,
                &token,
                &pendingPop
            );

            if (status != VTA_OK)
                return status;

            continue;
        }

        // * push w/o insn
        if (strcmp(token.text, "PUSH") == 0)
            return VTA_ERR_SYNTAX;

        /*
         * All currently implemented instructions go through
         * parseInstruction().
         */
        if (
            strcmp(token.text, "LOAD") == 0 ||
            strcmp(token.text, "STORE") == 0 ||
            strcmp(token.text, "STOR") == 0 ||
            strcmp(token.text, "FOR") == 0 ||
            strcmp(token.text, "NOOP") == 0 ||
            strcmp(token.text, "FINISH") == 0
        ) {
            VTAParsedInsn parsedInsn;
            VTAErr status;

            memset(&parsedInsn, 0, sizeof(parsedInsn));

            status = parseInstruction(
                &ctx,
                &token,
                &parsedInsn
            );

            if (status != VTA_OK)
                return status;

            if (
                parsedInsn.kind == ASM_GEMM ||
                parsedInsn.kind == ASM_GEMM_RST ||
                parsedInsn.kind == ASM_ALU
            ) {
                status = resolveUopLabels(
                    labelTable,
                    &parsedInsn
                );

                if (status != VTA_OK)
                    return status;
            }


            VTADependenciesTransition pushEdges;
            memset(&pushEdges, 0, sizeof(pushEdges));

            nextToken = peekNextToken(&ctx);

            if (
                nextToken.type == TOKEN_IDENTIFIER &&
                strcmp(nextToken.text, "PUSH") == 0
            ) {
                nextToken = getNextToken(&ctx);

                status = parseDependencyLine(&ctx, &nextToken, &pushEdges);

                if (status != VTA_OK)
                    return status;
            }

            status = applyDependencies(&parsedInsn, &pendingPop, &pushEdges);

            if (status != VTA_OK)
                return status;

            status = emitInstruction(&parsedInsn, insnBuffer, maxInsn, numInsn);

            if (status != VTA_OK)
                return status;

            memset(&pendingPop, 0, sizeof(pendingPop));

            continue;
        }

        return VTA_ERR_UNKNOWN_MNEMONIC;
    }

    if (
        pendingPop.ex_ld ||
        pendingPop.ex_st ||
        pendingPop.ld_ex ||
        pendingPop.st_ex
    )
        return VTA_ERR_SYNTAX;

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
    
    
    // *: Step 2: binary encoding
    VTAErr statusEncode;
    statusEncode = encodeInstructions(asmCode, insnBuffer, maxInsn, numInsn, uopBuffer, maxUop, &labelTable);
    
    if (statusEncode != VTA_OK)
        return statusEncode;

    *numUop = estimatedUop;

    return VTA_OK;
}

