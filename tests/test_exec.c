#include "as_machine.h"
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

static void add_line(AsProgram *p, unsigned number, const char *src) {
    AsTokenized t;
    as_tokenize(src, &t);
    as_program_upsert(p, (uint16_t)number, t.bytes, t.len, 0);
    as_tokenized_free(&t);
}

static void row_trim(const AsScreen *s, int row, char *dst) {
    int x, end = 0;
    for (x = 0; x < 40; x++) {
        char c = s->cell[row][x].ch ? (char)s->cell[row][x].ch : ' ';
        dst[x] = c;
        if (c != ' ') end = x + 1;
    }
    dst[end] = 0;
}

static void expect_row(const char **src, const unsigned *nums, int n, int row, const char *expect) {
    AsScreen *s = as_screen_new();
    AsProgram *p = as_program_new();
    AsMachine *m;
    char got[48];
    int i, guard = 0, rc = AS_OK;
    for (i = 0; i < n; i++) add_line(p, nums[i], src[i]);
    m = as_machine_new(p, s);
    as_machine_run(m);
    while (as_machine_running(m) && guard++ < 100000) {
        rc = as_machine_quantum(m, 0.05);
        if (rc != AS_OK) break;
    }
    row_trim(s, row, got);
    if (strcmp(got, expect) != 0) {
        fprintf(stderr, "FAIL row %d got \"%s\" want \"%s\" status \"%s\" rc %d\n", row, got, expect,
                as_machine_status(m), rc);
        g_fail++;
    }
    as_machine_free(m);
    as_program_free(p);
    as_screen_free(s);
}

static void test_math(void) {
    const char *s1[] = {"PRINT 1+2*3"};
    unsigned n1[] = {10};
    const char *s2[] = {"PRINT 2^3"};
    unsigned n2[] = {10};
    const char *s3[] = {"PRINT NOT 0"};
    unsigned n3[] = {10};
    const char *s4[] = {"PRINT 5 AND 3"};
    unsigned n4[] = {10};
    const char *s5[] = {"PRINT INT(-1.5)"};
    unsigned n5[] = {10};
    const char *s6[] = {"A$=\"AB\"", "PRINT A$+\"CD\""};
    unsigned n6[] = {10, 20};
    const char *s7[] = {"A%=5", "PRINT A%*2"};
    unsigned n7[] = {10, 20};
    expect_row(s1, n1, 1, 0, " 7");
    expect_row(s2, n2, 1, 0, " 8");
    expect_row(s3, n3, 1, 0, " 1");
    expect_row(s4, n4, 1, 0, " 1");
    expect_row(s5, n5, 1, 0, "-2");
    expect_row(s6, n6, 2, 0, "ABCD");
    expect_row(s7, n7, 2, 0, " 10");
}

static void test_flow(void) {
    const char *loop[] = {"FOR I=1 TO 3", "PRINT I", "NEXT I"};
    unsigned ln[] = {10, 20, 30};
    const char *gt[] = {"GOTO 30", "PRINT \"NO\"", "PRINT \"YES\""};
    unsigned gn[] = {10, 20, 30};
    const char *gs[] = {"GOSUB 100", "PRINT \"BACK\"", "END", "PRINT \"SUB\"", "RETURN"};
    unsigned gsn[] = {10, 20, 30, 100, 110};
    const char *iff[] = {"IF 1 THEN PRINT \"Y\"", "IF 0 THEN PRINT \"N\"", "PRINT \"Z\""};
    unsigned ifn[] = {10, 20, 30};
    AsScreen *s = as_screen_new();
    AsProgram *p = as_program_new();
    AsMachine *m;
    char a[48], b[48], c[48];
    int i, guard = 0;
    for (i = 0; i < 3; i++) add_line(p, ln[i], loop[i]);
    m = as_machine_new(p, s);
    as_machine_run(m);
    while (as_machine_running(m) && guard++ < 100000) {
        if (as_machine_quantum(m, 0.05) != AS_OK) break;
    }
    row_trim(s, 0, a);
    row_trim(s, 1, b);
    row_trim(s, 2, c);
    CHECK(strcmp(a, " 1") == 0);
    CHECK(strcmp(b, " 2") == 0);
    CHECK(strcmp(c, " 3") == 0);
    as_machine_free(m);
    as_program_free(p);
    as_screen_free(s);
    expect_row(gt, gn, 3, 0, "YES");
    {
        AsScreen *s2 = as_screen_new();
        AsProgram *p2 = as_program_new();
        AsMachine *m2;
        char r0[48], r1[48];
        int g = 0;
        for (i = 0; i < 5; i++) add_line(p2, gsn[i], gs[i]);
        m2 = as_machine_new(p2, s2);
        as_machine_run(m2);
        while (as_machine_running(m2) && g++ < 100000) {
            if (as_machine_quantum(m2, 0.05) != AS_OK) break;
        }
        row_trim(s2, 0, r0);
        row_trim(s2, 1, r1);
        CHECK(strcmp(r0, "SUB") == 0);
        CHECK(strcmp(r1, "BACK") == 0);
        as_machine_free(m2);
        as_program_free(p2);
        as_screen_free(s2);
    }
    {
        AsScreen *s2 = as_screen_new();
        AsProgram *p2 = as_program_new();
        AsMachine *m2;
        char r0[48], r1[48];
        int g = 0;
        for (i = 0; i < 3; i++) add_line(p2, ifn[i], iff[i]);
        m2 = as_machine_new(p2, s2);
        as_machine_run(m2);
        while (as_machine_running(m2) && g++ < 100000) {
            if (as_machine_quantum(m2, 0.05) != AS_OK) break;
        }
        row_trim(s2, 0, r0);
        row_trim(s2, 1, r1);
        CHECK(strcmp(r0, "Y") == 0);
        CHECK(strcmp(r1, "Z") == 0);
        as_machine_free(m2);
        as_program_free(p2);
        as_screen_free(s2);
    }
}

static void test_data_array(void) {
    const char *src[] = {"DATA 7,\"X\"", "READ A,B$", "PRINT A;B$"};
    unsigned nums[] = {10, 20, 30};
    const char *dim[] = {"DIM A(2)", "A(1)=42", "PRINT A(1)"};
    unsigned dn[] = {10, 20, 30};
    expect_row(src, nums, 3, 0, " 7 X");
    expect_row(dim, dn, 3, 0, " 42");
}

static void test_graphics_and_call(void) {
    const char *src[] = {"GR", "COLOR=15", "PLOT 1,2"};
    unsigned nums[] = {10, 20, 30};
    AsScreen *s = as_screen_new();
    AsProgram *p = as_program_new();
    AsMachine *m;
    int i, g = 0;
    const char *call[] = {"CALL 768"};
    unsigned cn[] = {10};
    AsScreen *s2;
    AsProgram *p2;
    AsMachine *m2;
    for (i = 0; i < 3; i++) add_line(p, nums[i], src[i]);
    m = as_machine_new(p, s);
    as_machine_run(m);
    while (as_machine_running(m) && g++ < 10000) {
        if (as_machine_quantum(m, 0.05) != AS_OK) break;
    }
    CHECK(s->disp == AS_DISP_GR);
    CHECK(s->pix && s->gw == 40 && s->pix[2 * s->gw + 1] == 15);
    as_machine_free(m);
    as_program_free(p);
    as_screen_free(s);

    s = as_screen_new();
    p = as_program_new();
    add_line(p, 10, "GR (80,80)");
    add_line(p, 20, "COLOR=4");
    add_line(p, 30, "PLOT 3,4");
    m = as_machine_new(p, s);
    as_machine_run(m);
    g = 0;
    while (as_machine_running(m) && g++ < 10000) {
        if (as_machine_quantum(m, 0.05) != AS_OK) break;
    }
    CHECK(s->disp == AS_DISP_GR80);
    CHECK(s->pix && s->gw == 80 && s->pix[4 * s->gw + 3] == 4);
    as_machine_free(m);
    as_program_free(p);
    as_screen_free(s);

    s = as_screen_new();
    p = as_program_new();
    add_line(p, 10, "HGR");
    add_line(p, 20, "HCOLOR=3");
    add_line(p, 30, "HPLOT 0,0 TO 10,0");
    m = as_machine_new(p, s);
    as_machine_run(m);
    g = 0;
    while (as_machine_running(m) && g++ < 10000) {
        if (as_machine_quantum(m, 0.05) != AS_OK) break;
    }
    CHECK(s->disp == AS_DISP_HGR);
    CHECK(s->pix && s->pix[0] == 3 && s->pix[10] == 3);
    as_machine_free(m);
    as_program_free(p);
    as_screen_free(s);

    s = as_screen_new();
    p = as_program_new();
    add_line(p, 10, "HGR (640,400)");
    add_line(p, 20, "HCOLOR=15");
    add_line(p, 30, "HPLOT 0,0");
    m = as_machine_new(p, s);
    as_machine_run(m);
    g = 0;
    while (as_machine_running(m) && g++ < 10000) {
        if (as_machine_quantum(m, 0.05) != AS_OK) break;
    }
    CHECK(s->disp == AS_DISP_H640);
    CHECK(s->gw == 640 && s->gh == 400);
    CHECK(s->pix && s->pix[0] == 15);
    as_machine_free(m);
    as_program_free(p);
    as_screen_free(s);

    s2 = as_screen_new();
    p2 = as_program_new();
    add_line(p2, cn[0], call[0]);
    m2 = as_machine_new(p2, s2);
    as_machine_run(m2);
    g = 0;
    while (as_machine_running(m2) && g++ < 10000) {
        if (as_machine_quantum(m2, 0.05) != AS_OK) break;
    }
    CHECK(strstr(as_machine_status(m2), "CALL NOT SUPPORTED") != NULL);
    as_machine_free(m2);
    as_program_free(p2);
    as_screen_free(s2);
}

static void test_check(void) {
    AsTokenized t;
    size_t at = 0;
    int rc;
    as_tokenize("PRINT (", &t);
    rc = as_check_line(t.bytes, t.len, &at);
    CHECK(rc == AS_ERR_SYNTAX);
    as_tokenized_free(&t);
    as_tokenize("PRINT 1+2", &t);
    rc = as_check_line(t.bytes, t.len, &at);
    CHECK(rc == AS_OK);
    as_tokenized_free(&t);
}

static void test_input(void) {
    AsScreen *s = as_screen_new();
    AsProgram *p = as_program_new();
    AsMachine *m;
    char got[48];
    int g = 0;
    const char *keys = "42";
    add_line(p, 10, "INPUT A$");
    add_line(p, 20, "PRINT A$");
    m = as_machine_new(p, s);
    as_machine_run(m);
    while (as_machine_running(m) && g++ < 10000) {
        int rc = as_machine_quantum(m, 0.05);
        if (rc == AS_INPUT || rc == AS_GET) break;
        if (rc != AS_OK) break;
    }
    CHECK(as_machine_waiting(m) == AS_INPUT);
    while (*keys) as_machine_keydown(m, *keys++);
    as_machine_keydown(m, '\r');
    g = 0;
    while (as_machine_running(m) && g++ < 10000) {
        if (as_machine_quantum(m, 0.05) != AS_OK) break;
    }
    row_trim(s, 1, got);
    CHECK(strcmp(got, "42") == 0);
    as_machine_free(m);
    as_program_free(p);
    as_screen_free(s);

    s = as_screen_new();
    p = as_program_new();
    add_line(p, 10, "INPUT A$");
    add_line(p, 20, "INPUT B$");
    add_line(p, 30, "PRINT A$;B$");
    m = as_machine_new(p, s);
    as_machine_run(m);
    g = 0;
    while (as_machine_running(m) && g++ < 10000) {
        int rc = as_machine_quantum(m, 0.05);
        if (rc == AS_INPUT) break;
        if (rc != AS_OK) break;
    }
    as_machine_keydown(m, 'A');
    as_machine_keydown(m, '\r');
    g = 0;
    while (as_machine_running(m) && g++ < 10000) {
        int rc = as_machine_quantum(m, 0.05);
        if (rc == AS_INPUT) break;
        if (rc != AS_OK) break;
    }
    CHECK(as_machine_waiting(m) == AS_INPUT);
    as_machine_keydown(m, 'B');
    as_machine_keydown(m, '\r');
    g = 0;
    while (as_machine_running(m) && g++ < 10000) {
        if (as_machine_quantum(m, 0.05) != AS_OK) break;
    }
    row_trim(s, 2, got);
    CHECK(strcmp(got, "AB") == 0);
    as_machine_free(m);
    as_program_free(p);
    as_screen_free(s);
}

static void test_peek(void) {
    const char *src[] = {"POKE 36,5", "A=PEEK(36)", "HTAB 1", "PRINT A"};
    unsigned nums[] = {10, 20, 30, 40};
    const char *other[] = {"POKE 100,9", "PRINT PEEK(100)"};
    unsigned on[] = {10, 20};
    expect_row(src, nums, 4, 0, " 5");
    expect_row(other, on, 2, 0, " 0");
}

static void test_pdl_key_wait(void) {
    AsScreen *s = as_screen_new();
    AsProgram *p = as_program_new();
    AsMachine *m;
    char a[48], b[48], c[48];
    int g = 0;
    s->pdl[0] = 200;
    s->btn0 = 1;
    add_line(p, 10, "PRINT PDL(0)");
    add_line(p, 20, "PRINT PEEK(49249)");
    add_line(p, 30, "PRINT PEEK(49152)");
    m = as_machine_new(p, s);
    as_machine_keydown(m, 'A');
    as_machine_run(m);
    while (as_machine_running(m) && g++ < 10000) {
        if (as_machine_quantum(m, 0.05) != AS_OK) break;
    }
    row_trim(s, 0, a);
    row_trim(s, 1, b);
    row_trim(s, 2, c);
    CHECK(strcmp(a, " 200") == 0);
    CHECK(strcmp(b, " 128") == 0);
    CHECK(strcmp(c, " 193") == 0);
    as_machine_free(m);
    as_program_free(p);
    as_screen_free(s);

    s = as_screen_new();
    p = as_program_new();
    add_line(p, 10, "WAIT 1,1");
    m = as_machine_new(p, s);
    as_machine_run(m);
    g = 0;
    while (as_machine_running(m) && g++ < 10000) {
        if (as_machine_quantum(m, 0.05) != AS_OK) break;
    }
    CHECK(strstr(as_machine_status(m), "NOT SUPPORTED") != NULL);
    as_machine_free(m);
    as_program_free(p);
    as_screen_free(s);
}

int main(void) {
    test_math();
    test_flow();
    test_data_array();
    test_graphics_and_call();
    test_input();
    test_check();
    test_peek();
    test_pdl_key_wait();
    if (g_fail) {
        fprintf(stderr, "%d exec failure(s)\n", g_fail);
        return 1;
    }
    printf("exec ok\n");
    return 0;
}
