#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

#include "assembler.h"

/*!
 * \brief Text of size 63 + 1 ('\n')
*/
#define TEXT_SIZE 64


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
     * \brief End of file/string.
    */
    TOKEN_EOF,

    /*!
     * \brief Invalid character.
    */
    TOKEN_ERROR,
} VTATokenType;


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
 * \brief Go on with the text, skipping white spaces and comments.
 * 
 * Count line when encountering `\n` and move the cursor when encountering white spaces and comments.
 * \note
 * - Current supported comment formats: `#` (python-like), `//` (C-like).
 * 
 * \param ctx Input context.
*/
static void skipWhiteSpaceAndComments(VTAParserContext* ctx) {
    while (*ctx->cursor != '\0') {
        if (*ctx->cursor == '\n') {
            ctx->lineNum++;
            ctx->cursor++;
            continue;
        }

        if (isspace((unsigned char)*ctx->cursor)) {
            ctx->cursor++;
            continue;
        }

        if (
            *ctx->cursor == '#' || 
            (*ctx->cursor == '/' && *(ctx->cursor + 1) == '/')
        ) {
            while (*ctx->cursor != '\0' && *ctx->cursor != '\n')
                *ctx->cursor++;
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
*/
static VTAToken getNextToken(VTAParserContext* ctx) {
    VTAToken token;
    memset(&token, 0, sizeof(VTAToken));

    skipWhiteSpaceAndComments(ctx);

    if (*ctx->cursor == '\0') {
        token.type = TOKEN_EOF;
        return token;
    }

    if (isdigit((unsigned char)*ctx->cursor)) {
        token.type = TOKEN_INT;
        
        int i;
        i = 0;

        while (isdigit((unsigned char)*ctx->cursor) && i < TEXT_SIZE - 1)
            token.text[i++] = *ctx->cursor++;

        token.text[i] = '\0';
        // * instant conversion (no allocation)
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

        strcpy(token.text, '>');
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


VTAErr assemble(
    const char* asmCode,
    VTAGenericInsn* insnBuffer,
    int maxInsn,
    int* numInsn,
    VTAUop* uopBuffer,
    int maxUop,
    int* numUop
) {
    if (asmCode == NULL || insnBuffer == NULL || numInsn == NULL || uopBuffer == NULL || numUop == NULL)
        return VTA_ERR_NULLPTR;
    if (maxInsn <= 0 || maxUop <= 0)
        return VTA_ERR_INVALID_INSN_SIZE;

    *numInsn = 0;
    *numUop  = 0;

    VTAParserContext ctx;
    ctx.cursor  = asmCode;
    ctx.lineNum = 1;

    // TODO: Step 1: scan to make label table
    
    // TODO: Step 2: binary stuff

    return VTA_OK;
}


/*!
 * \brief Print on stderr the error string for assembler errors.
 *
 * The error is based on the status passed as parameter.
 * 
 * \param status VTAErr code.
 * \param lineNum line number of the input string in which the error happened.
*/
void assemble_errorPrint(VTAErr status, int lineNum) {
    if (status == VTA_OK) return;
    
    fprintf(stderr, "[ASM_ERR] Row %d: ", lineNum > 0 ? lineNum : 0);
    switch (status) {
        case VTA_ERR_NULLPTR:
            fprintf(stderr, "NULL pointer to the buffer.\n"); 
            break;
        case VTA_ERR_INVALID_INSN_SIZE:
            fprintf(stderr, "Buffer size not valid (<= 0).\n"); 
            break;
        case VTA_ERR_INSN_BUFFER_FULL:
            fprintf(stderr, "The instruction buffer is full.\n"); 
            break;
        case VTA_ERR_UOP_BUFFER_FULL:
            fprintf(stderr, "The UOP buffer is full.\n"); 
            break;
        case VTA_ERR_SYNTAX:
            fprintf(stderr, "Syntax error. Unexpected token or invalid format.\n"); 
            break;
        case VTA_ERR_UNKNOWN_MNEMONIC:
            fprintf(stderr, "Unknown instruction or register.\n"); 
            break;
        case VTA_ERR_LABEL_NOT_FOUND:
            fprintf(stderr, "Label reference not found.\n"); 
            break;
        case VTA_ERR_OUT_OF_RANGE:
            fprintf(stderr, "Numeric value outside of range.\n"); 
            break;
        default:
            fprintf(stderr, "Unknown error to VTAErr (Code: %d).\n", status); 
            break;
    }
}