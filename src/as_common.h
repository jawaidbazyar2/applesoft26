#ifndef AS_COMMON_H
#define AS_COMMON_H

#include <stddef.h>
#include <stdint.h>

enum {
    AS_OK = 0,
    AS_ERR_NEXT_WITHOUT_FOR,
    AS_ERR_SYNTAX,
    AS_ERR_RETURN_WITHOUT_GOSUB,
    AS_ERR_OUT_OF_DATA,
    AS_ERR_ILLEGAL_QUANTITY,
    AS_ERR_OVERFLOW,
    AS_ERR_OUT_OF_MEMORY,
    AS_ERR_UNDEF_STATEMENT,
    AS_ERR_BAD_SUBSCRIPT,
    AS_ERR_REDIMD_ARRAY,
    AS_ERR_DIVISION_BY_ZERO,
    AS_ERR_ILLEGAL_DIRECT,
    AS_ERR_TYPE_MISMATCH,
    AS_ERR_STRING_TOO_LONG,
    AS_ERR_FORMULA_TOO_COMPLEX,
    AS_ERR_CANT_CONTINUE,
    AS_ERR_UNDEF_FUNCTION,
    AS_ERR_CALL,
    AS_ERR_NOT_SUPPORTED,
    AS_STOPPED,
    AS_PAUSED,
    AS_END,
    AS_INPUT,
    AS_GET,
    AS_DELAY
};

enum {
    AS_NUM = 0,
    AS_STR = 1
};

typedef struct AsStr {
    uint8_t *data;
    size_t len;
} AsStr;

typedef struct AsValue {
    int type;
    double num;
    AsStr str;
} AsValue;

enum {
    AS_T_FLOAT = 0,
    AS_T_INT = 1,
    AS_T_STR = 2
};

enum {
    AS_CLS_PLAIN = 0,
    AS_CLS_KEYWORD = 1,
    AS_CLS_VARIABLE = 2,
    AS_CLS_REMARK = 3
};

const char *as_error_text(int err);

void *as_xmalloc(size_t n);
void *as_xrealloc(void *p, size_t n);

void as_str_clear(AsStr *s);
int as_str_set(AsStr *s, const uint8_t *data, size_t len);
int as_str_set_cstr(AsStr *s, const char *z);
void as_value_clear(AsValue *v);

#endif
