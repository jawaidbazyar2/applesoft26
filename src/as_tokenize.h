#ifndef AS_TOKENIZE_H
#define AS_TOKENIZE_H

#include "as_common.h"

#include <stddef.h>
#include <stdint.h>

typedef struct AsTokenized {
    uint8_t *bytes;     /* token bytes plus a trailing 0 */
    size_t len;         /* length excluding the trailing 0 */
    uint32_t *src;      /* source offset of each token byte */
    uint16_t *src_n;    /* source length covered by each token byte */
    uint8_t *klass;     /* AS_CLS_* parallel to the source text */
    size_t source_len;
} AsTokenized;

void as_tokenized_free(AsTokenized *t);

/* Tokenize one program line, without its line number. Never fails on text;
   the result may still be a syntax error when checked. */
void as_tokenize(const char *src, AsTokenized *out);

/* LIST-style text: a space before and after each keyword. Caller frees *text. */
int as_detokenize(const uint8_t *bytes, size_t len, char **text);

typedef struct AsJumpRef {
    uint16_t number;
    uint32_t begin; /* inclusive source offset */
    uint32_t end;   /* exclusive source offset */
} AsJumpRef;

/* GOTO, GOSUB, and THEN line numbers, including each number of an ON list.
   Writes at most `cap` refs and returns how many were written. */
size_t as_jump_numbers(const char *src, AsJumpRef *out, size_t cap);

/* If `index` falls in one of those numbers, return 1 and its source span. */
int as_jump_number_at(const char *src, size_t index, uint16_t *number, size_t *begin, size_t *end);

#endif
