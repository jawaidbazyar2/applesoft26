#include "as_token.h"

_Static_assert(TK_PRINT == 0xBA, "PRINT token");
_Static_assert(TK_FOR == 0x81, "FOR token");
_Static_assert(TK_GOTO == 0xAB, "GOTO token");
_Static_assert(TK_GOSUB == 0xB0, "GOSUB token");
_Static_assert(TK_AT == 0xC5, "AT token");
_Static_assert(TK_SGN == 0xD2, "SGN token");
_Static_assert(TK_MID == 0xEA, "MID token");
_Static_assert(TK_COUNT == 107, "keyword count");

static const char *const k_kw[TK_COUNT] = {
    "END", "FOR", "NEXT", "DATA", "INPUT", "DEL", "DIM", "READ",
    "GR", "TEXT", "PR#", "IN#", "CALL", "PLOT", "HLIN", "VLIN",
    "HGR2", "HGR", "HCOLOR=", "HPLOT", "DRAW", "XDRAW", "HTAB", "HOME",
    "ROT=", "SCALE=", "SHLOAD", "TRACE", "NOTRACE", "NORMAL", "INVERSE", "FLASH",
    "COLOR=", "POP", "VTAB", "HIMEM:", "LOMEM:", "ONERR", "RESUME", "RECALL",
    "STORE", "SPEED=", "LET", "GOTO", "RUN", "IF", "RESTORE", "&",
    "GOSUB", "RETURN", "REM", "STOP", "ON", "WAIT", "LOAD", "SAVE",
    "DEF", "POKE", "PRINT", "CONT", "LIST", "CLEAR", "GET", "NEW",
    "TAB(", "TO", "FN", "SPC(", "THEN", "AT", "NOT", "STEP",
    "+", "-", "*", "/", "^", "AND", "OR", ">",
    "=", "<", "SGN", "INT", "ABS", "USR", "FRE", "SCRN(",
    "PDL", "POS", "SQR", "RND", "LOG", "EXP", "COS", "SIN",
    "TAN", "ATN", "PEEK", "LEN", "STR$", "VAL", "ASC", "CHR$",
    "LEFT$", "RIGHT$", "MID$"
};

const char *as_keyword(int token) {
    if (token < TK_FIRST || token > TK_LAST) return NULL;
    return k_kw[token - TK_FIRST];
}
