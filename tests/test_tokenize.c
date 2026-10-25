#include "as_program.h"
#include "as_token.h"
#include "as_tokenize.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int g_fail;

#define CHECK(cond)                                                                 \
    do {                                                                            \
        if (!(cond)) {                                                              \
            fprintf(stderr, "FAIL %s:%d %s\n", __FILE__, __LINE__, #cond);         \
            g_fail++;                                                               \
        }                                                                           \
    } while (0)

static void expect_tok(const char *src, const uint8_t *want, size_t n) {
    AsTokenized t;
    as_tokenize(src, &t);
    if (t.len != n || memcmp(t.bytes, want, n) != 0) {
        size_t i;
        fprintf(stderr, "FAIL tokenize \"%s\" got", src);
        for (i = 0; i < t.len; i++) fprintf(stderr, " %02X", t.bytes[i]);
        fprintf(stderr, " want");
        for (i = 0; i < n; i++) fprintf(stderr, " %02X", want[i]);
        fprintf(stderr, "\n");
        g_fail++;
    }
    as_tokenized_free(&t);
}

static void test_tokens(void) {
    const uint8_t hi[] = {TK_PRINT, '"', 'H', 'I', '"'};
    const uint8_t q[] = {TK_PRINT};
    const uint8_t qhi[] = {TK_PRINT, '"', 'H', 'I', '"'};
    const uint8_t fr[] = {TK_FOR, 'I', TK_EQ, '1', TK_TO, '1', '0', TK_STEP, '2'};
    const uint8_t atn[] = {TK_ATN, '(', '1', ')'};
    const uint8_t at[] = {TK_AT, '1', '0'};
    const uint8_t ato[] = {'A', TK_TO, '1', '0'};
    const uint8_t rem[] = {TK_REM, ' ', 'H', 'e', 'l', 'l', 'o'};
    const uint8_t data[] = {TK_DATA, ' ', 'H', 'e', 'l', 'l', 'o'};
    const uint8_t low[] = {TK_PRINT, '"', 'H', 'i', '"'};
    const uint8_t hgr[] = {TK_HGR2};
    const uint8_t hgr1[] = {TK_HGR};
    expect_tok("PRINT \"HI\"", hi, sizeof hi);
    expect_tok("?", q, sizeof q);
    expect_tok("?\"HI\"", qhi, sizeof qhi);
    expect_tok("FOR I = 1 TO 10 STEP 2", fr, sizeof fr);
    expect_tok("ATN(1)", atn, sizeof atn);
    expect_tok("AT 10", at, sizeof at);
    expect_tok("A TO 10", ato, sizeof ato);
    expect_tok("REM Hello", rem, sizeof rem);
    expect_tok("DATA Hello", data, sizeof data);
    expect_tok("print \"Hi\"", low, sizeof low);
    expect_tok("HGR2", hgr, sizeof hgr);
    expect_tok("HGR", hgr1, sizeof hgr1);
}

static void test_roundtrip_text(void) {
    AsTokenized t;
    char *text = NULL;
    as_tokenize("FOR I=1 TO 10", &t);
    as_detokenize(t.bytes, t.len, &text);
    CHECK(text && strcmp(text, "FOR I = 1 TO 10") == 0);
    free(text);
    as_tokenized_free(&t);
}

static void test_klass(void) {
    AsTokenized t;
    as_tokenize("PRINT A", &t);
    CHECK(t.source_len == 7);
    CHECK(t.klass[0] == AS_CLS_KEYWORD);
    CHECK(t.klass[5] == AS_CLS_PLAIN);
    CHECK(t.klass[6] == AS_CLS_VARIABLE);
    as_tokenized_free(&t);
}

int main(void) {
    test_tokens();
    test_roundtrip_text();
    test_klass();
    if (g_fail) {
        fprintf(stderr, "%d tokenize failure(s)\n", g_fail);
        return 1;
    }
    printf("tokenize ok\n");
    return 0;
}
