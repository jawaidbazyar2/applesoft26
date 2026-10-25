#ifndef AS_INTERNAL_H
#define AS_INTERNAL_H

#include "as_machine.h"
#include "as_token.h"
#include "as_vars.h"

#include <stddef.h>
#include <stdint.h>

#define AS_STACK 128

typedef struct AsFrame {
    int kind; /* 0 = FOR, 1 = GOSUB */
    int name;
    int vtype;
    double limit;
    double step;
    AsLine *line;
    size_t pc;
} AsFrame;

typedef struct AsFn {
    int used;
    int pname;
    int ptype;
    uint8_t *body;
    size_t len;
} AsFn;

typedef struct JumpMap JumpMap;

struct AsMachine {
    AsProgram *prog;
    AsScreen *screen;
    AsVars *vars;
    AsLine *line;
    size_t pc;
    int at_line_start;
    int break_skip;
    int running;
    int paused;
    int step;
    int check;
    int redirect;
    int trace;
    int error;
    size_t err_pc;
    char status[96];
    AsFrame stack[AS_STACK];
    int sp;
    AsLine *data_line;
    size_t data_pc;
    int data_ready;
    AsFn fn[26];
    int onerr;
    uint16_t onerr_line;
    int in_onerr;
    AsLine *resume_line;
    size_t resume_pc;
    int can_cont;
    AsLine *cont_line;
    size_t cont_pc;
    int wait;
    double wait_until; /* monotonic deadline, or remaining seconds while frozen */
    double tone_hz;    /* square wave requested by & TONE; 0 is silent */
    int wait_frozen;
    int input_shown;
    int input_ready;
    char input_line[256];
    size_t input_len;
    int key_ready;
    int got_key;
    int depth;
    uint32_t rnd_state;
    double rnd_last;
    int rnd_have;
    JumpMap *jumps;
    AsLine *stmt_line;
    size_t stmt_pc;
    AsLine direct_line;
    uint8_t *direct_tokens;
};

static inline uint8_t as_peekc(const AsMachine *m) {
    if (!m->line || m->pc >= m->line->len) return 0;
    return m->line->tokens[m->pc];
}

static inline uint8_t as_getc(AsMachine *m) {
    uint8_t c = as_peekc(m);
    if (m->line && m->pc < m->line->len) m->pc++;
    return c;
}

static inline int as_at_end(const AsMachine *m) {
    uint8_t c = as_peekc(m);
    return c == 0 || c == ':';
}

int as_exec_one(AsMachine *m);
int as_eval(AsMachine *m, AsValue *out);
int as_eval_num(AsMachine *m, double *out);
int as_parse_ref(AsMachine *m, AsRef *ref);
int as_fail(AsMachine *m, int err);
int as_expect(AsMachine *m, uint8_t tok);

int as_do_peek(AsMachine *m, int addr, double *out);
int as_do_poke(AsMachine *m, int addr, int value);
int as_do_pdl(AsMachine *m, int n, double *out);

#endif
