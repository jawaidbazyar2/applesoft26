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
    const uint8_t then[] = {TK_IF, 'A', TK_THEN, '5', '0'};
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
    expect_tok("IF A THEN 50", then, sizeof then);
    expect_tok("REM Hello", rem, sizeof rem);
    expect_tok("DATA Hello", data, sizeof data);
    expect_tok("print \"Hi\"", low, sizeof low);
    const uint8_t delay[] = {TK_AMP, 'D', 'E', 'L', 'A', 'Y', ' ', '0', '.', '2', '5'};
    const uint8_t tone[] = {TK_AMP, 'T', 'O', 'N', 'E', ' ', '4', '4', '0', ',', '0', '.', '2'};
    const uint8_t bare_delay[] = {TK_DEL, 'A', 'Y'};
    const uint8_t bare_tone[] = {TK_TO, 'N', 'E'};
    expect_tok("HGR2", hgr, sizeof hgr);
    expect_tok("HGR", hgr1, sizeof hgr1);
    expect_tok("& DELAY 0.25", delay, sizeof delay);
    expect_tok("&tone 440, 0.2", tone, sizeof tone);
    expect_tok("DELAY", bare_delay, sizeof bare_delay);
    expect_tok("TONE", bare_tone, sizeof bare_tone);
}

static void test_roundtrip_text(void) {
    AsTokenized t;
    char *text = NULL;
    as_tokenize("FOR I=1 TO 10", &t);
    as_detokenize(t.bytes, t.len, &text);
    CHECK(text && strcmp(text, "FOR I = 1 TO 10") == 0);
    free(text);
    as_tokenized_free(&t);

    as_tokenize("&DELAY 0.25", &t);
    as_detokenize(t.bytes, t.len, &text);
    CHECK(text && strcmp(text, "& DELAY 0.25") == 0);
    free(text);
    as_tokenized_free(&t);

    as_tokenize("&dhplot 0,0 to 559,191", &t);
    CHECK(t.len >= 7 && t.bytes[0] == TK_AMP && memcmp(t.bytes + 1, "DHPLOT", 6) == 0);
    as_detokenize(t.bytes, t.len, &text);
    CHECK(text && strcmp(text, "& DHPLOT 0,0 TO 559,191") == 0);
    free(text);
    as_tokenized_free(&t);
    as_tokenize("& DHGR: & DHCOLOR=5", &t);
    as_detokenize(t.bytes, t.len, &text);
    CHECK(text && strcmp(text, "& DHGR : & DHCOLOR = 5") == 0);
    free(text);
    as_tokenized_free(&t);
    as_tokenize("& TONE 440,0.2", &t);
    as_detokenize(t.bytes, t.len, &text);
    CHECK(text && strcmp(text, "& TONE 440,0.2") == 0);
    free(text);
    as_tokenized_free(&t);
}

static void test_klass(void) {
    AsTokenized t;
    size_t i;
    as_tokenize("PRINT A", &t);
    CHECK(t.source_len == 7);
    CHECK(t.klass[0] == AS_CLS_KEYWORD);
    CHECK(t.klass[5] == AS_CLS_PLAIN);
    CHECK(t.klass[6] == AS_CLS_VARIABLE);
    as_tokenized_free(&t);

    as_tokenize("& DELAY 1", &t);
    CHECK(t.klass[0] == AS_CLS_KEYWORD);
    CHECK(t.klass[2] == AS_CLS_KEYWORD);
    CHECK(t.klass[6] == AS_CLS_KEYWORD);
    CHECK(t.klass[8] == AS_CLS_PLAIN);
    as_tokenized_free(&t);

    as_tokenize("PRINT 1: REM Hello", &t);
    CHECK(t.klass[0] == AS_CLS_KEYWORD);
    CHECK(t.klass[6] == AS_CLS_PLAIN);
    CHECK(t.klass[9] == AS_CLS_KEYWORD);
    CHECK(t.klass[11] == AS_CLS_KEYWORD);
    for (i = 12; i < t.source_len; i++) CHECK(t.klass[i] == AS_CLS_REMARK);
    as_tokenized_free(&t);
}

static size_t nth_number(const char *s, int n, size_t *len) {
    size_t i = 0;
    int seen = 0;
    *len = 0;
    while (s[i]) {
        if (s[i] >= '0' && s[i] <= '9') {
            size_t b = i;
            while (s[i] >= '0' && s[i] <= '9') i++;
            if (seen == n) {
                *len = i - b;
                return b;
            }
            seen++;
        } else {
            i++;
        }
    }
    return 0;
}

static void expect_jump(const char *src, size_t index, int want, unsigned number, size_t begin, size_t end) {
    uint16_t n = 999;
    size_t b = 99, e = 99;
    int got = as_jump_number_at(src, index, &n, &b, &e);
    if (got != want || (want && (n != number || b != begin || e != end))) {
        fprintf(stderr, "FAIL jump \"%s\" at %zu got %d num %u span %zu..%zu\n", src, index, got, n, b, e);
        g_fail++;
    }
}

static void expect_nth(const char *src, int n, int want, unsigned number) {
    size_t len = 0;
    size_t b = nth_number(src, n, &len);
    expect_jump(src, b, want, number, b, b + len);
    if (len > 1) expect_jump(src, b + len - 1, want, number, b, b + len);
}

static void test_jumps(void) {
    expect_nth("GOTO 100", 0, 1, 100);
    expect_nth("goto 100", 0, 1, 100);
    expect_nth("GOSUB 20", 0, 1, 20);
    expect_nth("GO TO 40", 0, 1, 40);
    expect_nth("ON X GOTO 10, 20", 0, 1, 10);
    expect_nth("ON X GOTO 10, 20", 1, 1, 20);
    expect_nth("ON A GOSUB 1,2,3", 0, 1, 1);
    expect_nth("ON A GOSUB 1,2,3", 2, 1, 3);
    expect_nth("ON A+1 GOTO 10", 0, 0, 0);
    expect_nth("ON A+1 GOTO 10", 1, 1, 10);
    expect_nth("IF A THEN 50", 0, 1, 50);
    expect_nth("IF A=1 THEN 10", 0, 0, 0);
    expect_nth("IF A=1 THEN 10", 1, 1, 10);
    expect_nth("IF A THEN PRINT 50", 0, 0, 0);
    expect_nth("IF A THEN GOTO 50", 0, 1, 50);
    expect_nth("PRINT 100", 0, 0, 0);
    expect_nth("RUN 100", 0, 0, 0);
    expect_nth("RESTORE 100", 0, 0, 0);
    expect_nth("REM GOTO 10", 0, 0, 0);
    expect_nth("PRINT \"GOTO 10\"", 0, 0, 0);
    expect_nth("DATA 10", 0, 0, 0);
    expect_nth("GOTO 10: GOSUB 20", 0, 1, 10);
    expect_nth("GOTO 10: GOSUB 20", 1, 1, 20);
    expect_nth("A=10:GOTO 20", 0, 0, 0);
    expect_nth("A=10:GOTO 20", 1, 1, 20);
    expect_nth("GOTO 63999", 0, 1, 63999);
    expect_nth("GOTO 64000", 0, 0, 0);
    expect_jump("GOTO 100", 0, 0, 0, 0, 0);
    expect_jump("GOTO 100", 4, 0, 0, 0, 0);
    expect_jump("ON X GOTO 10, 20", 12, 0, 0, 0, 0);
    {
        AsJumpRef refs[4];
        size_t n = as_jump_numbers("GOTO 10: GOSUB 20", refs, 4);
        CHECK(n == 2);
        CHECK(refs[0].number == 10);
        CHECK(refs[1].number == 20);
    }
}

int main(void) {
    test_tokens();
    test_roundtrip_text();
    test_klass();
    test_jumps();
    if (g_fail) {
        fprintf(stderr, "%d tokenize failure(s)\n", g_fail);
        return 1;
    }
    printf("tokenize ok\n");
    return 0;
}
