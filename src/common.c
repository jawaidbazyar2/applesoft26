#include "as_common.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

const char *as_error_text(int err) {
    switch (err) {
    case AS_ERR_NEXT_WITHOUT_FOR: return "NEXT WITHOUT FOR";
    case AS_ERR_SYNTAX: return "SYNTAX ERROR";
    case AS_ERR_RETURN_WITHOUT_GOSUB: return "RETURN WITHOUT GOSUB";
    case AS_ERR_OUT_OF_DATA: return "OUT OF DATA";
    case AS_ERR_ILLEGAL_QUANTITY: return "ILLEGAL QUANTITY";
    case AS_ERR_OVERFLOW: return "OVERFLOW";
    case AS_ERR_OUT_OF_MEMORY: return "OUT OF MEMORY";
    case AS_ERR_UNDEF_STATEMENT: return "UNDEF'D STATEMENT";
    case AS_ERR_BAD_SUBSCRIPT: return "BAD SUBSCRIPT";
    case AS_ERR_REDIMD_ARRAY: return "REDIM'D ARRAY";
    case AS_ERR_DIVISION_BY_ZERO: return "DIVISION BY ZERO";
    case AS_ERR_ILLEGAL_DIRECT: return "ILLEGAL DIRECT";
    case AS_ERR_TYPE_MISMATCH: return "TYPE MISMATCH";
    case AS_ERR_STRING_TOO_LONG: return "STRING TOO LONG";
    case AS_ERR_FORMULA_TOO_COMPLEX: return "FORMULA TOO COMPLEX";
    case AS_ERR_CANT_CONTINUE: return "CAN'T CONTINUE";
    case AS_ERR_UNDEF_FUNCTION: return "UNDEF'D FUNCTION";
    case AS_ERR_CALL: return "CALL NOT SUPPORTED";
    case AS_ERR_NOT_SUPPORTED: return "NOT SUPPORTED";
    default: return "ERROR";
    }
}

void *as_xmalloc(size_t n) {
    void *p = calloc(1, n ? n : 1);
    if (!p) {
        fprintf(stderr, "out of memory\n");
        exit(1);
    }
    return p;
}

void *as_xrealloc(void *p, size_t n) {
    void *q = realloc(p, n ? n : 1);
    if (!q) {
        fprintf(stderr, "out of memory\n");
        exit(1);
    }
    return q;
}

void as_str_clear(AsStr *s) {
    free(s->data);
    s->data = NULL;
    s->len = 0;
}

int as_str_set(AsStr *s, const uint8_t *data, size_t len) {
    uint8_t *d;
    if (len > 255) return AS_ERR_STRING_TOO_LONG;
    d = (uint8_t *)malloc(len + 1);
    if (!d) return AS_ERR_OUT_OF_MEMORY;
    if (len) memcpy(d, data, len);
    d[len] = 0;
    free(s->data);
    s->data = d;
    s->len = len;
    return AS_OK;
}

int as_str_set_cstr(AsStr *s, const char *z) {
    return as_str_set(s, (const uint8_t *)z, strlen(z));
}

void as_value_clear(AsValue *v) {
    if (v->type == AS_STR) as_str_clear(&v->str);
    v->type = AS_NUM;
    v->num = 0;
}
