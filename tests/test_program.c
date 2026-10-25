#include "as_program.h"
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

static void add(AsProgram *p, const char *src, unsigned number) {
    AsTokenized t;
    as_tokenize(src, &t);
    CHECK(as_program_upsert(p, (uint16_t)number, t.bytes, t.len, 0) == 0);
    as_tokenized_free(&t);
}

static void test_order_and_targets(void) {
    AsProgram *p = as_program_new();
    add(p, "GOTO 30", 10);
    add(p, "PRINT \"A\"", 30);
    add(p, "GOSUB 10", 20);
    CHECK(as_program_count(p) == 3);
    CHECK(as_program_at(p, 0)->number == 10);
    CHECK(as_program_at(p, 1)->number == 20);
    CHECK(as_program_at(p, 2)->number == 30);
    CHECK(as_program_is_target(p, 30));
    CHECK(as_program_is_target(p, 10));
    CHECK(!as_program_is_target(p, 20));
    as_program_free(p);
}

static void test_renumber(void) {
    AsProgram *p = as_program_new();
    char *text = NULL;
    add(p, "GOTO 15", 5);
    add(p, "PRINT \"A\"", 15);
    CHECK(as_program_renumber(p, 10, 10) == 0);
    CHECK(as_program_at(p, 0)->number == 10);
    CHECK(as_program_at(p, 1)->number == 20);
    as_detokenize(as_program_at(p, 0)->tokens, as_program_at(p, 0)->len, &text);
    CHECK(text && strcmp(text, "GOTO 20") == 0);
    free(text);
    CHECK(as_program_is_target(p, 20));
    as_program_free(p);
}

static void test_image(void) {
    AsProgram *p = as_program_new();
    AsProgram *q;
    uint8_t *img = NULL;
    size_t n = 0;
    char err[128];
    /* 10 PRINT "HI" / 20 END */
    const uint8_t expect[] = {
        0x0B, 0x08, 0x0A, 0x00, 0xBA, '"', 'H', 'I', '"', 0x00,
        0x11, 0x08, 0x14, 0x00, 0x80, 0x00,
        0x00, 0x00
    };
    add(p, "PRINT \"HI\"", 10);
    add(p, "END", 20);
    CHECK(as_program_save(p, &img, &n, err, sizeof err) == 0);
    if (n != sizeof expect || memcmp(img, expect, n) != 0) {
        size_t i;
        fprintf(stderr, "FAIL image bytes (%zu)\n", n);
        for (i = 0; i < n; i++) fprintf(stderr, " %02X", img[i]);
        fprintf(stderr, "\n");
        g_fail++;
    }
    q = as_program_new();
    CHECK(as_program_load(q, img, n, err, sizeof err) == 0);
    CHECK(as_program_count(q) == 2);
    CHECK(q->count == 2);
    CHECK(as_program_at(q, 0)->len == as_program_at(p, 0)->len);
    CHECK(memcmp(as_program_at(q, 0)->tokens, as_program_at(p, 0)->tokens, as_program_at(p, 0)->len) == 0);
    CHECK(memcmp(as_program_at(q, 1)->tokens, as_program_at(p, 1)->tokens, as_program_at(p, 1)->len) == 0);
    CHECK(as_program_load(q, (const uint8_t *)"10 PRINT", 8, err, sizeof err) != 0);
    free(img);
    as_program_free(p);
    as_program_free(q);
}

int main(void) {
    test_order_and_targets();
    test_renumber();
    test_image();
    if (g_fail) {
        fprintf(stderr, "%d program failure(s)\n", g_fail);
        return 1;
    }
    printf("program ok\n");
    return 0;
}
