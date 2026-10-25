#include "as_internal.h"
#include "as_tokenize.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

struct JumpMap {
    uint32_t *keys;
    AsLine **lines;
    size_t cap;
};

int as_expect(AsMachine *m, uint8_t tok) {
    if (as_peekc(m) != tok) return as_fail(m, AS_ERR_SYNTAX);
    as_getc(m);
    return AS_OK;
}

static int in_direct(const AsMachine *m) {
    return m->line == &m->direct_line;
}

static void show_error(AsMachine *m, int err) {
    AsLine *ln = m->stmt_line ? m->stmt_line : m->line;
    if (ln && ln != &m->direct_line)
        snprintf(m->status, sizeof m->status, "?%s IN %u", as_error_text(err), ln->number);
    else
        snprintf(m->status, sizeof m->status, "?%s", as_error_text(err));
    if (m->screen && !m->check) {
        as_screen_puts(m->screen, m->status);
        as_screen_putc(m->screen, '\r');
    }
}

static int raise_err(AsMachine *m, int err) {
    AsLine *dest;
    m->error = err;
    if (m->check) return err;
    if (m->onerr && !m->in_onerr && m->jumps) {
        dest = NULL;
        {
            size_t mask, i, slot;
            JumpMap *j = m->jumps;
            mask = j->cap - 1;
            slot = ((size_t)m->onerr_line * 2654435761u) & mask;
            for (i = 0; i < j->cap; i++) {
                size_t k = (slot + i) & mask;
                if (j->keys[k] == 0xFFFFFFFFu) break;
                if (j->keys[k] == m->onerr_line) {
                    dest = j->lines[k];
                    break;
                }
            }
        }
        if (dest) {
            m->in_onerr = 1;
            m->resume_line = m->stmt_line;
            m->resume_pc = m->stmt_pc;
            m->line = dest;
            m->pc = 0;
            m->at_line_start = 1;
            m->redirect = 1;
            return err;
        }
    }
    show_error(m, err);
    m->running = 0;
    m->paused = 0;
    if (m->screen) m->screen->cursor_on = 0;
    return err;
}

int as_fail(AsMachine *m, int err) {
    m->err_pc = m->pc;
    return raise_err(m, err);
}

static void jump_free(JumpMap *j) {
    if (!j) return;
    free(j->keys);
    free(j->lines);
    free(j);
}

static JumpMap *jump_build(AsProgram *p) {
    size_t cap = 16, i;
    JumpMap *j;
    AsLine *ln;
    if (!p) return NULL;
    while (cap < p->count * 2 + 16) cap *= 2;
    j = (JumpMap *)as_xmalloc(sizeof *j);
    j->cap = cap;
    j->keys = (uint32_t *)as_xmalloc(cap * sizeof(uint32_t));
    j->lines = (AsLine **)as_xmalloc(cap * sizeof(AsLine *));
    for (i = 0; i < cap; i++) j->keys[i] = 0xFFFFFFFFu;
    for (ln = p->head; ln; ln = ln->next) {
        size_t mask = cap - 1;
        size_t slot = ((size_t)ln->number * 2654435761u) & mask;
        for (i = 0; i < cap; i++) {
            size_t k = (slot + i) & mask;
            if (j->keys[k] == 0xFFFFFFFFu || j->keys[k] == ln->number) {
                j->keys[k] = ln->number;
                j->lines[k] = ln;
                break;
            }
        }
    }
    return j;
}

static AsLine *jump_get(JumpMap *j, uint16_t num) {
    size_t mask, i, slot;
    if (!j || !j->cap) return NULL;
    mask = j->cap - 1;
    slot = ((size_t)num * 2654435761u) & mask;
    for (i = 0; i < j->cap; i++) {
        size_t k = (slot + i) & mask;
        if (j->keys[k] == 0xFFFFFFFFu) return NULL;
        if (j->keys[k] == num) return j->lines[k];
    }
    return NULL;
}

static void clear_fns(AsMachine *m) {
    int i;
    for (i = 0; i < 26; i++) {
        free(m->fn[i].body);
        memset(&m->fn[i], 0, sizeof m->fn[i]);
    }
}

static void reset_runtime(AsMachine *m) {
    as_vars_clear(m->vars);
    clear_fns(m);
    m->sp = 0;
    m->data_line = NULL;
    m->data_pc = 0;
    m->data_ready = 0;
    m->onerr = 0;
    m->in_onerr = 0;
    m->can_cont = 0;
    m->wait = 0;
    m->input_ready = 0;
    m->input_shown = 0;
    m->key_ready = 0;
    jump_free(m->jumps);
    m->jumps = jump_build(m->prog);
    if (m->screen) m->screen->cursor_on = 0;
}

static int read_line_number(AsMachine *m, uint16_t *out) {
    unsigned v = 0;
    int any = 0;
    while (as_peekc(m) >= '0' && as_peekc(m) <= '9') {
        v = v * 10u + (unsigned)(as_getc(m) - '0');
        if (v > 63999u) return as_fail(m, AS_ERR_ILLEGAL_QUANTITY);
        any = 1;
    }
    if (!any) return as_fail(m, AS_ERR_SYNTAX);
    *out = (uint16_t)v;
    return AS_OK;
}

static int eval_coord(AsMachine *m, int *out) {
    double v;
    int rc = as_eval_num(m, &v);
    if (rc) return rc;
    if (!isfinite(v) || v < 0 || v > 32767) return as_fail(m, AS_ERR_ILLEGAL_QUANTITY);
    *out = (int)trunc(v);
    return AS_OK;
}

static int store_ref(AsMachine *m, const AsRef *ref, const AsValue *val) {
    int rc;
    if (ref->type == AS_T_STR) {
        if (val->type != AS_STR) return as_fail(m, AS_ERR_TYPE_MISMATCH);
    } else if (val->type != AS_NUM) {
        return as_fail(m, AS_ERR_TYPE_MISMATCH);
    }
    if (m->check) return AS_OK;
    rc = as_vars_store(m->vars, ref, val);
    if (rc) return as_fail(m, rc);
    return AS_OK;
}

static void format_num(double v, char *dst, size_t cap) {
    char body[64];
    char *e;
    if (v == 0) {
        snprintf(dst, cap, " 0");
        return;
    }
    snprintf(body, sizeof body, "%.9g", v < 0 ? -v : v);
    e = strchr(body, 'e');
    if (e) *e = 'E';
    if (v < 0) snprintf(dst, cap, "-%s", body);
    else snprintf(dst, cap, " %s", body);
}

static int print_value(AsMachine *m, const AsValue *v) {
    size_t i;
    if (!m->screen || m->check) return AS_OK;
    if (v->type == AS_STR) {
        for (i = 0; i < v->str.len; i++) as_screen_putc(m->screen, v->str.data[i]);
        return AS_OK;
    }
    if (!isfinite(v->num)) return as_fail(m, AS_ERR_OVERFLOW);
    {
        char buf[64];
        format_num(v->num, buf, sizeof buf);
        as_screen_puts(m->screen, buf);
        as_screen_putc(m->screen, ' ');
    }
    return AS_OK;
}

static int do_goto(AsMachine *m, uint16_t n) {
    AsLine *ln;
    if (m->check) return AS_OK;
    ln = jump_get(m->jumps, n);
    if (!ln) return as_fail(m, AS_ERR_UNDEF_STATEMENT);
    m->line = ln;
    m->pc = 0;
    m->at_line_start = 1;
    return AS_OK;
}

static int do_gosub(AsMachine *m, uint16_t n) {
    AsLine *ln;
    if (m->check) return AS_OK;
    ln = jump_get(m->jumps, n);
    if (!ln) return as_fail(m, AS_ERR_UNDEF_STATEMENT);
    if (m->sp >= AS_STACK) return as_fail(m, AS_ERR_OUT_OF_MEMORY);
    m->stack[m->sp].kind = 1;
    m->stack[m->sp].line = m->line;
    m->stack[m->sp].pc = m->pc;
    m->sp++;
    m->line = ln;
    m->pc = 0;
    m->at_line_start = 1;
    return AS_OK;
}

static int stmt_end(AsMachine *m) {
    if (!as_at_end(m)) return as_fail(m, AS_ERR_SYNTAX);
    if (m->check) return AS_OK;
    m->running = 0;
    m->can_cont = 0;
    return AS_END;
}

static int stmt_stop(AsMachine *m) {
    if (!as_at_end(m)) return as_fail(m, AS_ERR_SYNTAX);
    if (m->check) return AS_OK;
    if (in_direct(m)) {
        snprintf(m->status, sizeof m->status, "BREAK");
        if (m->screen) {
            as_screen_puts(m->screen, m->status);
            as_screen_putc(m->screen, '\r');
        }
        m->running = 0;
        return AS_END;
    }
    m->can_cont = 1;
    m->cont_line = m->line;
    m->cont_pc = m->pc;
    if (m->line)
        snprintf(m->status, sizeof m->status, "BREAK IN %u", m->line->number);
    else
        snprintf(m->status, sizeof m->status, "BREAK");
    if (m->screen) {
        as_screen_puts(m->screen, m->status);
        as_screen_putc(m->screen, '\r');
    }
    m->running = 0;
    m->paused = 1;
    return AS_PAUSED;
}

static int stmt_rem(AsMachine *m) {
    if (m->line) m->pc = m->line->len;
    return AS_OK;
}

static int stmt_data(AsMachine *m) {
    while (as_peekc(m) && as_peekc(m) != ':') as_getc(m);
    return AS_OK;
}

static int stmt_let(AsMachine *m) {
    AsRef ref;
    AsValue v;
    int rc = as_parse_ref(m, &ref);
    if (rc) return rc;
    if (as_peekc(m) != TK_EQ) return as_fail(m, AS_ERR_SYNTAX);
    as_getc(m);
    rc = as_eval(m, &v);
    if (rc) return rc;
    rc = store_ref(m, &ref, &v);
    as_value_clear(&v);
    if (rc) return rc;
    if (!as_at_end(m)) return as_fail(m, AS_ERR_SYNTAX);
    return AS_OK;
}

static int stmt_for(AsMachine *m) {
    AsRef ref;
    double startv, limit, step = 1;
    AsValue start;
    int rc, i;
    rc = as_parse_ref(m, &ref);
    if (rc) return rc;
    if (ref.is_array || ref.type == AS_T_STR) return as_fail(m, AS_ERR_TYPE_MISMATCH);
    if (as_getc(m) != TK_EQ) return as_fail(m, AS_ERR_SYNTAX);
    rc = as_eval_num(m, &startv);
    if (rc) return rc;
    memset(&start, 0, sizeof start);
    start.type = AS_NUM;
    start.num = startv;
    rc = store_ref(m, &ref, &start);
    if (rc) return rc;
    if (as_expect(m, TK_TO)) return AS_ERR_SYNTAX;
    rc = as_eval_num(m, &limit);
    if (rc) return rc;
    if (as_peekc(m) == TK_STEP) {
        as_getc(m);
        rc = as_eval_num(m, &step);
        if (rc) return rc;
    }
    if (!as_at_end(m)) return as_fail(m, AS_ERR_SYNTAX);
    if (m->check) return AS_OK;
    for (i = m->sp - 1; i >= 0; i--) {
        if (m->stack[i].kind != 0) break;
        if (m->stack[i].name == ref.name && m->stack[i].vtype == ref.type) {
            m->sp = i;
            break;
        }
    }
    if (m->sp >= AS_STACK) return as_fail(m, AS_ERR_OUT_OF_MEMORY);
    m->stack[m->sp].kind = 0;
    m->stack[m->sp].name = ref.name;
    m->stack[m->sp].vtype = ref.type;
    m->stack[m->sp].limit = limit;
    m->stack[m->sp].step = step;
    m->stack[m->sp].line = m->line;
    m->stack[m->sp].pc = m->pc;
    m->sp++;
    return AS_OK;
}

static int stmt_next(AsMachine *m) {
    int name = -1, type = AS_T_FLOAT;
    int rc;
    double cur;
    AsFrame *fr;
    if (as_peekc(m) >= 'A' && as_peekc(m) <= 'Z') {
        AsRef ref;
        rc = as_parse_ref(m, &ref);
        if (rc) return rc;
        if (ref.is_array || ref.type == AS_T_STR) return as_fail(m, AS_ERR_TYPE_MISMATCH);
        name = ref.name;
        type = ref.type;
    }
    if (!as_at_end(m)) return as_fail(m, AS_ERR_SYNTAX);
    if (m->check) return AS_OK;
    for (;;) {
        if (m->sp <= 0 || m->stack[m->sp - 1].kind != 0) return as_fail(m, AS_ERR_NEXT_WITHOUT_FOR);
        if (name < 0 || (m->stack[m->sp - 1].name == name && m->stack[m->sp - 1].vtype == type)) break;
        m->sp--;
    }
    fr = &m->stack[m->sp - 1];
    rc = as_vars_get_scalar(m->vars, fr->vtype, fr->name, &cur);
    if (rc) return as_fail(m, rc);
    cur += fr->step;
    rc = as_vars_set_scalar(m->vars, fr->vtype, fr->name, cur);
    if (rc) return as_fail(m, rc);
    as_vars_get_scalar(m->vars, fr->vtype, fr->name, &cur);
    if ((fr->step >= 0 && cur > fr->limit) || (fr->step < 0 && cur < fr->limit)) {
        m->sp--;
        return AS_OK;
    }
    m->line = fr->line;
    m->pc = fr->pc;
    m->at_line_start = 0;
    return AS_OK;
}

static int stmt_goto(AsMachine *m) {
    uint16_t n;
    int rc = read_line_number(m, &n);
    if (rc) return rc;
    if (!as_at_end(m)) return as_fail(m, AS_ERR_SYNTAX);
    return do_goto(m, n);
}

static int stmt_gosub(AsMachine *m) {
    uint16_t n;
    int rc = read_line_number(m, &n);
    if (rc) return rc;
    if (!as_at_end(m)) return as_fail(m, AS_ERR_SYNTAX);
    return do_gosub(m, n);
}

static int stmt_return(AsMachine *m) {
    if (!as_at_end(m)) return as_fail(m, AS_ERR_SYNTAX);
    if (m->check) return AS_OK;
    while (m->sp > 0 && m->stack[m->sp - 1].kind == 0) m->sp--;
    if (m->sp <= 0 || m->stack[m->sp - 1].kind != 1) return as_fail(m, AS_ERR_RETURN_WITHOUT_GOSUB);
    m->sp--;
    m->line = m->stack[m->sp].line;
    m->pc = m->stack[m->sp].pc;
    m->at_line_start = 0;
    return AS_OK;
}

static int stmt_pop(AsMachine *m) {
    if (!as_at_end(m)) return as_fail(m, AS_ERR_SYNTAX);
    if (m->check) return AS_OK;
    while (m->sp > 0 && m->stack[m->sp - 1].kind == 0) m->sp--;
    if (m->sp <= 0 || m->stack[m->sp - 1].kind != 1) return as_fail(m, AS_ERR_RETURN_WITHOUT_GOSUB);
    m->sp--;
    return AS_OK;
}

static int stmt_if(AsMachine *m) {
    AsValue v;
    int rc, cond;
    rc = as_eval(m, &v);
    if (rc) return rc;
    if (v.type != AS_NUM) {
        as_value_clear(&v);
        return as_fail(m, AS_ERR_TYPE_MISMATCH);
    }
    cond = v.num != 0;
    as_value_clear(&v);
    if (as_peekc(m) == TK_GOTO) {
        uint16_t n;
        as_getc(m);
        rc = read_line_number(m, &n);
        if (rc) return rc;
        if (!as_at_end(m)) return as_fail(m, AS_ERR_SYNTAX);
        if (m->check || !cond) return AS_OK;
        return do_goto(m, n);
    }
    if (as_peekc(m) != TK_THEN) return as_fail(m, AS_ERR_SYNTAX);
    as_getc(m);
    if (as_peekc(m) >= '0' && as_peekc(m) <= '9') {
        uint16_t n;
        rc = read_line_number(m, &n);
        if (rc) return rc;
        if (!as_at_end(m)) return as_fail(m, AS_ERR_SYNTAX);
        if (m->check || !cond) return AS_OK;
        return do_goto(m, n);
    }
    if (!cond && !m->check && m->line) m->pc = m->line->len;
    return AS_OK;
}

static int stmt_on(AsMachine *m) {
    double v;
    int rc, n, count = 0, gosub, chosen_ok = 0;
    uint16_t chosen = 0;
    rc = as_eval_num(m, &v);
    if (rc) return rc;
    if (!isfinite(v) || v < 0 || v > 255) return as_fail(m, AS_ERR_ILLEGAL_QUANTITY);
    n = (int)trunc(v);
    if (as_peekc(m) == TK_GOTO) gosub = 0;
    else if (as_peekc(m) == TK_GOSUB) gosub = 1;
    else return as_fail(m, AS_ERR_SYNTAX);
    as_getc(m);
    for (;;) {
        uint16_t ln;
        rc = read_line_number(m, &ln);
        if (rc) return rc;
        count++;
        if (count == n) {
            chosen = ln;
            chosen_ok = 1;
        }
        if (as_peekc(m) == ',') {
            as_getc(m);
            continue;
        }
        break;
    }
    if (!as_at_end(m)) return as_fail(m, AS_ERR_SYNTAX);
    if (m->check || !chosen_ok) return AS_OK;
    return gosub ? do_gosub(m, chosen) : do_goto(m, chosen);
}

static int stmt_print(AsMachine *m) {
    int newline = 1;
    if (as_at_end(m)) {
        if (!m->check && m->screen) as_screen_putc(m->screen, '\r');
        return AS_OK;
    }
    while (!as_at_end(m)) {
        uint8_t c = as_peekc(m);
        if (c == ';') {
            as_getc(m);
            newline = 0;
            continue;
        }
        if (c == ',') {
            as_getc(m);
            newline = 0;
            if (!m->check && m->screen) as_screen_comma_tab(m->screen);
            continue;
        }
        if (c == TK_TAB || c == TK_SPC) {
            int tab = c == TK_TAB;
            double v;
            int n, rc;
            as_getc(m);
            rc = as_eval_num(m, &v);
            if (rc) return rc;
            if (as_getc(m) != ')') return as_fail(m, AS_ERR_SYNTAX);
            if (!isfinite(v) || v < 0 || v >= 256) return as_fail(m, AS_ERR_ILLEGAL_QUANTITY);
            n = (int)trunc(v);
            if (!m->check && m->screen) {
                if (tab) {
                    int guard = 0;
                    int dest = n == 0 ? 0 : n - 1;
                    while (m->screen->cur_x != dest && guard++ < 255) as_screen_putc(m->screen, ' ');
                } else {
                    int i;
                    for (i = 0; i < n; i++) as_screen_putc(m->screen, ' ');
                }
            }
            newline = 1;
            continue;
        }
        {
            AsValue v;
            int rc = as_eval(m, &v);
            if (rc) return rc;
            rc = print_value(m, &v);
            as_value_clear(&v);
            if (rc) return rc;
            newline = 1;
        }
    }
    if (newline && !m->check && m->screen) as_screen_putc(m->screen, '\r');
    return AS_OK;
}

static int parse_prompt(AsMachine *m, int *question, char *prompt, size_t cap) {
    *question = 1;
    prompt[0] = 0;
    if (as_peekc(m) == '"') {
        AsValue v;
        size_t n, i;
        int rc = as_eval(m, &v);
        if (rc) return rc;
        if (v.type != AS_STR) {
            as_value_clear(&v);
            return as_fail(m, AS_ERR_TYPE_MISMATCH);
        }
        if (as_peekc(m) == ';') {
            *question = 1;
            as_getc(m);
        } else if (as_peekc(m) == ',') {
            *question = 0;
            as_getc(m);
        } else {
            as_value_clear(&v);
            return as_fail(m, AS_ERR_SYNTAX);
        }
        n = v.str.len;
        if (n >= cap) n = cap - 1;
        for (i = 0; i < n; i++) prompt[i] = v.str.data ? (char)v.str.data[i] : 0;
        prompt[n] = 0;
        as_value_clear(&v);
    }
    return AS_OK;
}

static int stmt_input(AsMachine *m) {
    int question = 1, rc, nrefs = 0, i;
    char prompt[240];
    AsRef refs[8];
    const char *p;
    if (m->check) {
        rc = parse_prompt(m, &question, prompt, sizeof prompt);
        if (rc) return rc;
        do {
            rc = as_parse_ref(m, &refs[0]);
            if (rc) return rc;
            if (as_peekc(m) == ',') {
                as_getc(m);
                continue;
            }
            break;
        } while (1);
        if (!as_at_end(m)) return as_fail(m, AS_ERR_SYNTAX);
        return AS_OK;
    }
    if (in_direct(m)) return as_fail(m, AS_ERR_ILLEGAL_DIRECT);
    if (!m->input_ready) {
        if (!m->input_shown) {
            rc = parse_prompt(m, &question, prompt, sizeof prompt);
            if (rc) return rc;
            if (m->screen) {
                if (prompt[0]) as_screen_puts(m->screen, prompt);
                if (question) as_screen_putc(m->screen, '?');
                m->screen->cursor_on = 1;
            }
            m->input_len = 0;
            m->input_line[0] = 0;
            m->input_shown = 1;
        }
        m->pc = m->stmt_pc;
        m->wait = AS_INPUT;
        return AS_INPUT;
    }
    m->input_ready = 0;
    m->input_shown = 0;
    m->wait = 0;
    if (m->screen) m->screen->cursor_on = 0;
    rc = parse_prompt(m, &question, prompt, sizeof prompt);
    if (rc) return rc;
    do {
        if (nrefs >= 8) return as_fail(m, AS_ERR_SYNTAX);
        rc = as_parse_ref(m, &refs[nrefs]);
        if (rc) return rc;
        nrefs++;
        if (as_peekc(m) == ',') {
            as_getc(m);
            continue;
        }
        break;
    } while (1);
    if (!as_at_end(m)) return as_fail(m, AS_ERR_SYNTAX);
    p = m->input_line;
    for (i = 0; i < nrefs; i++) {
        char buf[256];
        size_t n = 0;
        AsValue val;
        while (*p == ' ') p++;
        if (*p == '"') {
            p++;
            while (*p && *p != '"' && n < 255) buf[n++] = *p++;
            if (*p == '"') p++;
        } else {
            while (*p && *p != ',' && n < 255) buf[n++] = *p++;
            while (n && buf[n - 1] == ' ') n--;
        }
        memset(&val, 0, sizeof val);
        if (refs[i].type == AS_T_STR) {
            val.type = AS_STR;
            as_str_set(&val.str, (const uint8_t *)buf, n);
        } else {
            char tmp[256];
            char *end = NULL;
            memcpy(tmp, buf, n);
            tmp[n] = 0;
            val.type = AS_NUM;
            val.num = strtod(tmp, &end);
            while (*end == ' ') end++;
            if (n && end == tmp) {
                as_screen_puts(m->screen, "?REENTER");
                as_screen_putc(m->screen, '\r');
                m->input_len = 0;
                m->input_line[0] = 0;
                m->input_shown = 0;
                m->pc = m->stmt_pc;
                m->wait = AS_INPUT;
                return AS_INPUT;
            }
        }
        rc = store_ref(m, &refs[i], &val);
        as_value_clear(&val);
        if (rc) return rc;
        while (*p == ' ') p++;
        if (i + 1 < nrefs) {
            if (*p != ',') {
                if (m->screen) {
                    as_screen_puts(m->screen, "??");
                    as_screen_putc(m->screen, '\r');
                    m->screen->cursor_on = 1;
                }
                m->input_len = 0;
                m->input_line[0] = 0;
                m->input_shown = 1;
                m->pc = m->stmt_pc;
                m->wait = AS_INPUT;
                return AS_INPUT;
            }
            p++;
        }
    }
    while (*p == ' ') p++;
    if (*p && m->screen) {
        as_screen_puts(m->screen, "?EXTRA IGNORED");
        as_screen_putc(m->screen, '\r');
    }
    return AS_OK;
}

static int stmt_get(AsMachine *m) {
    AsRef ref;
    AsValue val;
    int rc;
    uint8_t ch;
    if (m->check) {
        rc = as_parse_ref(m, &ref);
        if (rc) return rc;
        if (!as_at_end(m)) return as_fail(m, AS_ERR_SYNTAX);
        return AS_OK;
    }
    if (in_direct(m)) return as_fail(m, AS_ERR_ILLEGAL_DIRECT);
    if (!m->key_ready) {
        m->pc = m->stmt_pc;
        m->wait = AS_GET;
        return AS_GET;
    }
    ch = (uint8_t)m->got_key;
    m->key_ready = 0;
    m->wait = 0;
    rc = as_parse_ref(m, &ref);
    if (rc) return rc;
    if (!as_at_end(m)) return as_fail(m, AS_ERR_SYNTAX);
    memset(&val, 0, sizeof val);
    if (ref.type == AS_T_STR) {
        val.type = AS_STR;
        as_str_set(&val.str, &ch, 1);
    } else {
        char tmp[2] = {(char)ch, 0};
        char *end = NULL;
        val.type = AS_NUM;
        val.num = strtod(tmp, &end);
        if (end == tmp) val.num = 0;
    }
    rc = store_ref(m, &ref, &val);
    as_value_clear(&val);
    return rc;
}

static int pull_data(AsMachine *m, char *buf, size_t cap, size_t *out_n) {
    for (;;) {
        const uint8_t *t;
        size_t n;
        if (!m->data_line) m->data_line = m->prog ? m->prog->head : NULL;
        if (!m->data_line) return as_fail(m, AS_ERR_OUT_OF_DATA);
        t = m->data_line->tokens;
        n = m->data_line->len;
        if (!m->data_ready) {
            if (m->data_pc >= n) {
                m->data_line = m->data_line->next;
                m->data_pc = 0;
                continue;
            }
            if (t[m->data_pc] == TK_DATA) {
                m->data_pc++;
                m->data_ready = 1;
            } else if (t[m->data_pc] == TK_REM) {
                m->data_line = m->data_line->next;
                m->data_pc = 0;
                continue;
            } else if (t[m->data_pc] == '"') {
                m->data_pc++;
                while (m->data_pc < n && t[m->data_pc] != '"') m->data_pc++;
                if (m->data_pc < n) m->data_pc++;
                continue;
            } else {
                m->data_pc++;
                continue;
            }
        }
        while (m->data_pc < n && t[m->data_pc] == ' ') m->data_pc++;
        if (m->data_pc >= n || t[m->data_pc] == ':') {
            m->data_ready = 0;
            if (m->data_pc < n && t[m->data_pc] == ':') m->data_pc++;
            continue;
        }
        {
            size_t s, len;
            if (t[m->data_pc] == '"') {
                m->data_pc++;
                s = m->data_pc;
                while (m->data_pc < n && t[m->data_pc] != '"') m->data_pc++;
                len = m->data_pc - s;
                if (m->data_pc < n && t[m->data_pc] == '"') m->data_pc++;
            } else {
                s = m->data_pc;
                while (m->data_pc < n && t[m->data_pc] != ',' && t[m->data_pc] != ':') m->data_pc++;
                len = m->data_pc - s;
                while (len && t[s + len - 1] == ' ') len--;
            }
            if (len >= cap) len = cap - 1;
            memcpy(buf, t + s, len);
            buf[len] = 0;
            *out_n = len;
            if (m->data_pc < n && t[m->data_pc] == ',') m->data_pc++;
            else m->data_ready = 0;
            return AS_OK;
        }
    }
}

static int stmt_read(AsMachine *m) {
    for (;;) {
        AsRef ref;
        char buf[256];
        size_t n = 0;
        AsValue val;
        int rc = as_parse_ref(m, &ref);
        if (rc) return rc;
        if (m->check) {
            if (as_peekc(m) == ',') {
                as_getc(m);
                continue;
            }
            break;
        }
        rc = pull_data(m, buf, sizeof buf, &n);
        if (rc) return rc;
        memset(&val, 0, sizeof val);
        if (ref.type == AS_T_STR) {
            val.type = AS_STR;
            as_str_set(&val.str, (const uint8_t *)buf, n);
        } else {
            char *end = NULL;
            val.type = AS_NUM;
            val.num = strtod(buf, &end);
        }
        rc = store_ref(m, &ref, &val);
        as_value_clear(&val);
        if (rc) return rc;
        if (as_peekc(m) == ',') {
            as_getc(m);
            continue;
        }
        break;
    }
    if (!as_at_end(m)) return as_fail(m, AS_ERR_SYNTAX);
    return AS_OK;
}

static int stmt_restore(AsMachine *m) {
    if (as_peekc(m) >= '0' && as_peekc(m) <= '9') {
        uint16_t n;
        int rc = read_line_number(m, &n);
        if (rc) return rc;
        if (!as_at_end(m)) return as_fail(m, AS_ERR_SYNTAX);
        if (m->check) return AS_OK;
        m->data_line = jump_get(m->jumps, n);
        if (!m->data_line) return as_fail(m, AS_ERR_UNDEF_STATEMENT);
    } else {
        if (!as_at_end(m)) return as_fail(m, AS_ERR_SYNTAX);
        if (m->check) return AS_OK;
        m->data_line = m->prog ? m->prog->head : NULL;
    }
    m->data_pc = 0;
    m->data_ready = 0;
    return AS_OK;
}

static int stmt_dim(AsMachine *m) {
    for (;;) {
        AsRef ref;
        int rc = as_parse_ref(m, &ref);
        if (rc) return rc;
        if (!ref.is_array) return as_fail(m, AS_ERR_SYNTAX);
        if (!m->check) {
            rc = as_vars_dim(m->vars, ref.name, ref.type, ref.subs, ref.ndims);
            if (rc) return as_fail(m, rc);
        }
        if (as_peekc(m) == ',') {
            as_getc(m);
            continue;
        }
        break;
    }
    if (!as_at_end(m)) return as_fail(m, AS_ERR_SYNTAX);
    return AS_OK;
}

static int stmt_def(AsMachine *m) {
    int letter, rc;
    AsRef param;
    size_t start, n;
    uint8_t *body;
    if (as_expect(m, TK_FN)) return AS_ERR_SYNTAX;
    letter = as_peekc(m);
    if (letter < 'A' || letter > 'Z') return as_fail(m, AS_ERR_SYNTAX);
    as_getc(m);
    if (as_getc(m) != '(') return as_fail(m, AS_ERR_SYNTAX);
    rc = as_parse_ref(m, &param);
    if (rc) return rc;
    if (param.is_array || param.type == AS_T_STR) return as_fail(m, AS_ERR_TYPE_MISMATCH);
    if (as_getc(m) != ')') return as_fail(m, AS_ERR_SYNTAX);
    if (as_peekc(m) != TK_EQ) return as_fail(m, AS_ERR_SYNTAX);
    as_getc(m);
    start = m->pc;
    while (as_peekc(m) && as_peekc(m) != ':') as_getc(m);
    n = m->pc - start;
    if (m->check) return AS_OK;
    if (in_direct(m)) return as_fail(m, AS_ERR_ILLEGAL_DIRECT);
    body = (uint8_t *)as_xmalloc(n + 1);
    if (n && m->line) memcpy(body, m->line->tokens + start, n);
    body[n] = 0;
    free(m->fn[letter - 'A'].body);
    m->fn[letter - 'A'].body = body;
    m->fn[letter - 'A'].len = n;
    m->fn[letter - 'A'].used = 1;
    m->fn[letter - 'A'].pname = param.name;
    m->fn[letter - 'A'].ptype = param.type;
    return AS_OK;
}

static int stmt_run(AsMachine *m) {
    uint16_t n = 0;
    int has = 0;
    AsLine *ln;
    if (as_peekc(m) >= '0' && as_peekc(m) <= '9') {
        int rc = read_line_number(m, &n);
        if (rc) return rc;
        has = 1;
    }
    if (!as_at_end(m)) return as_fail(m, AS_ERR_SYNTAX);
    if (m->check) return AS_OK;
    ln = has ? as_program_find(m->prog, n) : (m->prog ? m->prog->head : NULL);
    if (has && !ln) return as_fail(m, AS_ERR_UNDEF_STATEMENT);
    reset_runtime(m);
    m->line = ln;
    m->pc = 0;
    m->at_line_start = 1;
    m->running = ln ? 1 : 0;
    return ln ? AS_OK : AS_END;
}

static int stmt_del(AsMachine *m) {
    uint16_t a, b, lo, hi;
    int rc = read_line_number(m, &a);
    if (rc) return rc;
    if (as_expect(m, ',')) return AS_ERR_SYNTAX;
    rc = read_line_number(m, &b);
    if (rc) return rc;
    if (!as_at_end(m)) return as_fail(m, AS_ERR_SYNTAX);
    if (m->check) return AS_OK;
    lo = a < b ? a : b;
    hi = a < b ? b : a;
    if (m->line && m->line->number >= lo && m->line->number <= hi) {
        AsLine *n = m->line->next;
        while (n && n->number <= hi) n = n->next;
        m->line = n;
        m->pc = 0;
        m->at_line_start = 1;
    }
    as_program_delete_range(m->prog, lo, hi);
    jump_free(m->jumps);
    m->jumps = jump_build(m->prog);
    m->can_cont = 0;
    return AS_OK;
}

static int stmt_list(AsMachine *m) {
    uint16_t a = 0, b = 63999;
    int have = 0;
    AsLine *ln;
    if (as_peekc(m) >= '0' && as_peekc(m) <= '9') {
        int rc = read_line_number(m, &a);
        if (rc) return rc;
        have = 1;
        b = a;
    }
    if (as_peekc(m) == ',') {
        int rc;
        as_getc(m);
        b = 63999;
        if (as_peekc(m) >= '0' && as_peekc(m) <= '9') {
            rc = read_line_number(m, &b);
            if (rc) return rc;
        }
        if (!have) a = 0;
    }
    if (!as_at_end(m)) return as_fail(m, AS_ERR_SYNTAX);
    if (m->check || !m->screen || !m->prog) return AS_OK;
    for (ln = m->prog->head; ln; ln = ln->next) {
        char *text = NULL;
        char num[16];
        if (ln->number < a || ln->number > b) continue;
        snprintf(num, sizeof num, "%u ", ln->number);
        as_screen_puts(m->screen, num);
        as_detokenize(ln->tokens, ln->len, &text);
        if (text) as_screen_puts(m->screen, text);
        free(text);
        as_screen_putc(m->screen, '\r');
    }
    return AS_OK;
}

static int stmt_new(AsMachine *m) {
    if (!as_at_end(m)) return as_fail(m, AS_ERR_SYNTAX);
    if (m->check) return AS_OK;
    m->line = NULL;
    if (m->prog) {
        as_program_clear(m->prog);
        m->prog->dirty = 1;
    }
    reset_runtime(m);
    m->running = 0;
    return AS_END;
}

static int stmt_clear(AsMachine *m) {
    if (!as_at_end(m)) return as_fail(m, AS_ERR_SYNTAX);
    if (m->check) return AS_OK;
    as_vars_clear(m->vars);
    clear_fns(m);
    m->sp = 0;
    m->data_line = NULL;
    m->data_pc = 0;
    m->data_ready = 0;
    m->onerr = 0;
    m->in_onerr = 0;
    return AS_OK;
}

static int stmt_cont(AsMachine *m) {
    if (!as_at_end(m)) return as_fail(m, AS_ERR_SYNTAX);
    if (m->check) return AS_OK;
    if (!m->can_cont || !m->cont_line) return as_fail(m, AS_ERR_CANT_CONTINUE);
    m->line = m->cont_line;
    m->pc = m->cont_pc;
    m->at_line_start = 0;
    m->running = 1;
    m->paused = 0;
    m->can_cont = 0;
    return AS_OK;
}

static int stmt_poke(AsMachine *m) {
    double addr, val;
    int rc = as_eval_num(m, &addr);
    if (rc) return rc;
    if (as_expect(m, ',')) return AS_ERR_SYNTAX;
    rc = as_eval_num(m, &val);
    if (rc) return rc;
    if (!as_at_end(m)) return as_fail(m, AS_ERR_SYNTAX);
    if (!isfinite(addr) || addr < 0 || addr > 65535) return as_fail(m, AS_ERR_ILLEGAL_QUANTITY);
    if (!isfinite(val) || val < 0 || val > 255) return as_fail(m, AS_ERR_ILLEGAL_QUANTITY);
    if (m->check) return AS_OK;
    rc = as_do_poke(m, (int)trunc(addr), (int)trunc(val));
    if (rc) return as_fail(m, rc);
    return AS_OK;
}

static int stmt_home(AsMachine *m) {
    if (!as_at_end(m)) return as_fail(m, AS_ERR_SYNTAX);
    if (!m->check && m->screen) as_screen_home(m->screen);
    return AS_OK;
}

static int stmt_htab(AsMachine *m) {
    int n, rc = eval_coord(m, &n);
    if (rc) return rc;
    if (!as_at_end(m)) return as_fail(m, AS_ERR_SYNTAX);
    if (n < 0 || n > 255) return as_fail(m, AS_ERR_ILLEGAL_QUANTITY);
    if (!m->check && m->screen) m->screen->cur_x = n == 0 ? 0 : n - 1;
    return AS_OK;
}

static int stmt_vtab(AsMachine *m) {
    int n, rc = eval_coord(m, &n);
    if (rc) return rc;
    if (!as_at_end(m)) return as_fail(m, AS_ERR_SYNTAX);
    if (n < 1 || n > 24) return as_fail(m, AS_ERR_ILLEGAL_QUANTITY);
    if (!m->check && m->screen) m->screen->cur_y = n - 1;
    return AS_OK;
}

static int stmt_text(AsMachine *m) {
    if (!as_at_end(m)) return as_fail(m, AS_ERR_SYNTAX);
    if (!m->check && m->screen) as_screen_text_mode(m->screen);
    return AS_OK;
}

static int stmt_gr(AsMachine *m) {
    int cols = 40, rows = 40, saw = 0, rc;
    if (as_peekc(m) == '(') {
        as_getc(m);
        rc = eval_coord(m, &cols);
        if (rc) return rc;
        if (as_expect(m, ',')) return AS_ERR_SYNTAX;
        rc = eval_coord(m, &rows);
        if (rc) return rc;
        if (as_getc(m) != ')') return as_fail(m, AS_ERR_SYNTAX);
        saw = 1;
    }
    if (!as_at_end(m)) return as_fail(m, AS_ERR_SYNTAX);
    if (m->check) return AS_OK;
    if (!m->screen) return AS_OK;
    if (!saw || (cols == 40 && rows == 40)) as_screen_gr(m->screen, 40, 40);
    else if (cols == 80 && rows == 80) as_screen_gr(m->screen, 80, 80);
    else return as_fail(m, AS_ERR_ILLEGAL_QUANTITY);
    return AS_OK;
}

static int stmt_hgr(AsMachine *m) {
    int w = 0, h = 0, saw = 0, rc;
    if (as_peekc(m) == '(') {
        as_getc(m);
        rc = eval_coord(m, &w);
        if (rc) return rc;
        if (as_expect(m, ',')) return AS_ERR_SYNTAX;
        rc = eval_coord(m, &h);
        if (rc) return rc;
        if (as_getc(m) != ')') return as_fail(m, AS_ERR_SYNTAX);
        saw = 1;
    }
    if (!as_at_end(m)) return as_fail(m, AS_ERR_SYNTAX);
    if (m->check) return AS_OK;
    if (!m->screen) return AS_OK;
    if (!saw) as_screen_hgr(m->screen, 0);
    else if (w == 640 && h == 400) as_screen_hgr(m->screen, 2);
    else return as_fail(m, AS_ERR_ILLEGAL_QUANTITY);
    return AS_OK;
}

static int stmt_hgr2(AsMachine *m) {
    if (!as_at_end(m)) return as_fail(m, AS_ERR_SYNTAX);
    if (!m->check && m->screen) as_screen_hgr(m->screen, 1);
    return AS_OK;
}

static int stmt_color(AsMachine *m) {
    int n, rc = eval_coord(m, &n);
    if (rc) return rc;
    if (!as_at_end(m)) return as_fail(m, AS_ERR_SYNTAX);
    if (n < 0 || n > 15) return as_fail(m, AS_ERR_ILLEGAL_QUANTITY);
    if (!m->check && m->screen) m->screen->color = n;
    return AS_OK;
}

static int stmt_hcolor(AsMachine *m) {
    int n, rc = eval_coord(m, &n);
    if (rc) return rc;
    if (!as_at_end(m)) return as_fail(m, AS_ERR_SYNTAX);
    if (n < 0 || n > 15) return as_fail(m, AS_ERR_ILLEGAL_QUANTITY);
    if (!m->check && m->screen) m->screen->hcolor = n;
    return AS_OK;
}

static int stmt_plot(AsMachine *m) {
    int x, y, rc;
    rc = eval_coord(m, &x);
    if (rc) return rc;
    if (as_expect(m, ',')) return AS_ERR_SYNTAX;
    rc = eval_coord(m, &y);
    if (rc) return rc;
    if (!as_at_end(m)) return as_fail(m, AS_ERR_SYNTAX);
    if (m->check || !m->screen) return AS_OK;
    rc = as_screen_plot(m->screen, x, y);
    if (rc) return as_fail(m, rc);
    return AS_OK;
}

static int stmt_hlin(AsMachine *m) {
    int x1, x2, y, rc;
    rc = eval_coord(m, &x1);
    if (rc) return rc;
    if (as_expect(m, ',')) return AS_ERR_SYNTAX;
    rc = eval_coord(m, &x2);
    if (rc) return rc;
    if (as_expect(m, TK_AT)) return AS_ERR_SYNTAX;
    rc = eval_coord(m, &y);
    if (rc) return rc;
    if (!as_at_end(m)) return as_fail(m, AS_ERR_SYNTAX);
    if (m->check || !m->screen) return AS_OK;
    rc = as_screen_hlin(m->screen, x1, x2, y);
    if (rc) return as_fail(m, rc);
    return AS_OK;
}

static int stmt_vlin(AsMachine *m) {
    int y1, y2, x, rc;
    rc = eval_coord(m, &y1);
    if (rc) return rc;
    if (as_expect(m, ',')) return AS_ERR_SYNTAX;
    rc = eval_coord(m, &y2);
    if (rc) return rc;
    if (as_expect(m, TK_AT)) return AS_ERR_SYNTAX;
    rc = eval_coord(m, &x);
    if (rc) return rc;
    if (!as_at_end(m)) return as_fail(m, AS_ERR_SYNTAX);
    if (m->check || !m->screen) return AS_OK;
    rc = as_screen_vlin(m->screen, y1, y2, x);
    if (rc) return as_fail(m, rc);
    return AS_OK;
}

static int stmt_hplot(AsMachine *m) {
    int x, y, rc, abs_first = 0;
    if (as_peekc(m) != TK_TO) {
        rc = eval_coord(m, &x);
        if (rc) return rc;
        if (as_expect(m, ',')) return AS_ERR_SYNTAX;
        rc = eval_coord(m, &y);
        if (rc) return rc;
        abs_first = 1;
    }
    if (abs_first && !m->check && m->screen) {
        rc = as_screen_hplot(m->screen, x, y);
        if (rc) return as_fail(m, rc);
    }
    while (as_peekc(m) == TK_TO) {
        as_getc(m);
        rc = eval_coord(m, &x);
        if (rc) return rc;
        if (as_expect(m, ',')) return AS_ERR_SYNTAX;
        rc = eval_coord(m, &y);
        if (rc) return rc;
        if (!m->check && m->screen) {
            rc = as_screen_hplot_to(m->screen, x, y);
            if (rc) return as_fail(m, rc);
        }
    }
    if (!as_at_end(m)) return as_fail(m, AS_ERR_SYNTAX);
    return AS_OK;
}

static int stmt_style(AsMachine *m, int style) {
    if (!as_at_end(m)) return as_fail(m, AS_ERR_SYNTAX);
    if (!m->check && m->screen) m->screen->style = style;
    return AS_OK;
}

static int stmt_onerr(AsMachine *m) {
    uint16_t n;
    int rc;
    if (as_expect(m, TK_GOTO)) return AS_ERR_SYNTAX;
    rc = read_line_number(m, &n);
    if (rc) return rc;
    if (!as_at_end(m)) return as_fail(m, AS_ERR_SYNTAX);
    if (m->check) return AS_OK;
    m->onerr = 1;
    m->onerr_line = n;
    return AS_OK;
}

static int stmt_resume(AsMachine *m) {
    if (!as_at_end(m)) return as_fail(m, AS_ERR_SYNTAX);
    if (m->check) return AS_OK;
    if (!m->in_onerr || !m->resume_line) return as_fail(m, AS_ERR_CANT_CONTINUE);
    m->in_onerr = 0;
    m->line = m->resume_line;
    m->pc = m->resume_pc;
    m->at_line_start = 0;
    return AS_OK;
}

static int stmt_trace(AsMachine *m, int on) {
    if (!as_at_end(m)) return as_fail(m, AS_ERR_SYNTAX);
    if (!m->check) m->trace = on;
    return AS_OK;
}

static int stmt_ignore_num(AsMachine *m) {
    double v;
    int rc = as_eval_num(m, &v);
    if (rc) return rc;
    if (!as_at_end(m)) return as_fail(m, AS_ERR_SYNTAX);
    (void)v;
    return AS_OK;
}

static int stmt_call(AsMachine *m) {
    double v;
    int rc = as_eval_num(m, &v);
    if (rc) return rc;
    if (!as_at_end(m)) return as_fail(m, AS_ERR_SYNTAX);
    if (m->check) return AS_OK;
    return as_fail(m, AS_ERR_CALL);
}

static int stmt_unsupported(AsMachine *m) {
    while (!as_at_end(m)) {
        if (as_peekc(m) == '"') {
            as_getc(m);
            while (as_peekc(m) && as_peekc(m) != '"') as_getc(m);
            if (as_peekc(m) == '"') as_getc(m);
            continue;
        }
        as_getc(m);
    }
    if (m->check) return AS_OK;
    return as_fail(m, AS_ERR_NOT_SUPPORTED);
}

static int dispatch(AsMachine *m, uint8_t tok) {
    switch (tok) {
    case TK_END: return stmt_end(m);
    case TK_FOR: return stmt_for(m);
    case TK_NEXT: return stmt_next(m);
    case TK_DATA: return stmt_data(m);
    case TK_INPUT: return stmt_input(m);
    case TK_DEL: return stmt_del(m);
    case TK_DIM: return stmt_dim(m);
    case TK_READ: return stmt_read(m);
    case TK_GR: return stmt_gr(m);
    case TK_TEXT: return stmt_text(m);
    case TK_PR:
    case TK_IN: return stmt_unsupported(m);
    case TK_CALL: return stmt_call(m);
    case TK_PLOT: return stmt_plot(m);
    case TK_HLIN: return stmt_hlin(m);
    case TK_VLIN: return stmt_vlin(m);
    case TK_HGR2: return stmt_hgr2(m);
    case TK_HGR: return stmt_hgr(m);
    case TK_HCOLOR: return stmt_hcolor(m);
    case TK_HPLOT: return stmt_hplot(m);
    case TK_DRAW:
    case TK_XDRAW:
    case TK_ROT:
    case TK_SCALE:
    case TK_SHLOAD: return stmt_unsupported(m);
    case TK_HTAB: return stmt_htab(m);
    case TK_HOME: return stmt_home(m);
    case TK_TRACE: return stmt_trace(m, 1);
    case TK_NOTRACE: return stmt_trace(m, 0);
    case TK_NORMAL: return stmt_style(m, 0);
    case TK_INVERSE: return stmt_style(m, 1);
    case TK_FLASH: return stmt_style(m, 2);
    case TK_COLOR: return stmt_color(m);
    case TK_POP: return stmt_pop(m);
    case TK_VTAB: return stmt_vtab(m);
    case TK_HIMEM:
    case TK_LOMEM:
    case TK_SPEED: return stmt_ignore_num(m);
    case TK_ONERR: return stmt_onerr(m);
    case TK_RESUME: return stmt_resume(m);
    case TK_RECALL:
    case TK_STORE:
    case TK_WAIT:
    case TK_LOAD:
    case TK_SAVE:
    case TK_AMP: return stmt_unsupported(m);
    case TK_LET: return stmt_let(m);
    case TK_GOTO: return stmt_goto(m);
    case TK_RUN: return stmt_run(m);
    case TK_IF: return stmt_if(m);
    case TK_RESTORE: return stmt_restore(m);
    case TK_GOSUB: return stmt_gosub(m);
    case TK_RETURN: return stmt_return(m);
    case TK_REM: return stmt_rem(m);
    case TK_STOP: return stmt_stop(m);
    case TK_ON: return stmt_on(m);
    case TK_DEF: return stmt_def(m);
    case TK_POKE: return stmt_poke(m);
    case TK_PRINT: return stmt_print(m);
    case TK_CONT: return stmt_cont(m);
    case TK_LIST: return stmt_list(m);
    case TK_CLEAR: return stmt_clear(m);
    case TK_GET: return stmt_get(m);
    case TK_NEW: return stmt_new(m);
    default: return as_fail(m, AS_ERR_SYNTAX);
    }
}

static int finish_stmt(AsMachine *m, int rc) {
    if (m->redirect) {
        m->redirect = 0;
        return AS_OK;
    }
    return rc;
}

int as_exec_one(AsMachine *m) {
    uint8_t c;
    if (!m->line) return AS_END;
    if (m->at_line_start) {
        m->at_line_start = 0;
        if (!m->check && m->line->breakpoint && !m->break_skip) {
            m->break_skip = 1;
            m->at_line_start = 1;
            m->running = 0;
            m->paused = 1;
            if (m->line) snprintf(m->status, sizeof m->status, "BREAK IN %u", m->line->number);
            return AS_PAUSED;
        }
        m->break_skip = 0;
        if (!m->check && m->trace && m->screen && m->line) {
            char b[24];
            snprintf(b, sizeof b, "#%u ", m->line->number);
            as_screen_puts(m->screen, b);
        }
    }
    m->stmt_line = m->line;
    m->stmt_pc = m->pc;
    c = as_peekc(m);
    if (c == 0) {
        m->line = m->line->next;
        m->pc = 0;
        m->at_line_start = 1;
        if (!m->line) {
            m->running = 0;
            return AS_END;
        }
        return AS_OK;
    }
    if (c == ':') {
        as_getc(m);
        return AS_OK;
    }
    if (c >= TK_END && c <= TK_NEW) {
        as_getc(m);
        return finish_stmt(m, dispatch(m, c));
    }
    if (c >= 'A' && c <= 'Z') return finish_stmt(m, stmt_let(m));
    return finish_stmt(m, as_fail(m, AS_ERR_SYNTAX));
}

AsMachine *as_machine_new(AsProgram *program, AsScreen *screen) {
    AsMachine *m = (AsMachine *)as_xmalloc(sizeof(AsMachine));
    m->prog = program;
    m->screen = screen;
    m->vars = as_vars_new();
    m->rnd_state = 1;
    m->rnd_last = 0.5;
    return m;
}

void as_machine_free(AsMachine *m) {
    if (!m) return;
    clear_fns(m);
    jump_free(m->jumps);
    as_vars_free(m->vars);
    free(m->direct_tokens);
    free(m);
}

static void start_immediate(AsMachine *m, const uint8_t *tokens, size_t len) {
    uint8_t *buf = (uint8_t *)as_xmalloc(len + 1);
    memcpy(buf, tokens, len);
    buf[len] = 0;
    free(m->direct_tokens);
    m->direct_tokens = buf;
    m->direct_line.next = NULL;
    m->direct_line.number = 0;
    m->direct_line.breakpoint = 0;
    m->direct_line.tokens = buf;
    m->direct_line.len = len;
    jump_free(m->jumps);
    m->jumps = jump_build(m->prog);
    m->line = &m->direct_line;
    m->pc = 0;
    m->at_line_start = 1;
    m->break_skip = 1;
    m->running = 1;
    m->paused = 0;
    m->step = 0;
    m->wait = 0;
    m->error = 0;
    m->redirect = 0;
    m->input_shown = 0;
    m->input_ready = 0;
    m->status[0] = 0;
}

static int prompt_number(const char *s, uint16_t *num, const char **rest, int *has) {
    unsigned n = 0;
    int digits = 0;
    *has = 0;
    while (*s == ' ' || *s == '\t') s++;
    if (*s < '0' || *s > '9') {
        *rest = s;
        return AS_OK;
    }
    while (*s >= '0' && *s <= '9') {
        if (++digits > 5) return AS_ERR_SYNTAX;
        n = n * 10u + (unsigned)(*s - '0');
        if (n > 63999u) return AS_ERR_SYNTAX;
        s++;
    }
    *num = (uint16_t)n;
    *has = 1;
    while (*s == ' ' || *s == '\t') s++;
    *rest = s;
    return AS_OK;
}

int as_machine_prompt(AsMachine *m, const char *text, int *kind) {
    uint16_t num = 0;
    const char *rest;
    int has = 0;
    int rc;
    AsTokenized t;
    if (kind) *kind = 0;
    if (!text) text = "";
    rc = prompt_number(text, &num, &rest, &has);
    if (rc) return rc;
    if (has) {
        AsLine *old;
        int br = 0;
        if (!m->prog) return AS_ERR_SYNTAX;
        as_tokenize(rest, &t);
        old = as_program_find(m->prog, num);
        if (old) br = old->breakpoint;
        as_machine_edited(m);
        if (t.len == 0) as_program_upsert(m->prog, num, NULL, 0, 0);
        else as_program_upsert(m->prog, num, t.bytes, t.len, br);
        as_tokenized_free(&t);
        if (kind) *kind = 1;
        return AS_OK;
    }
    as_tokenize(rest, &t);
    if (t.len == 0) {
        as_tokenized_free(&t);
        return AS_OK;
    }
    start_immediate(m, t.bytes, t.len);
    as_tokenized_free(&t);
    if (kind) *kind = 2;
    return AS_OK;
}

int as_machine_on_line(const AsMachine *m, uint16_t number) {
    return m->line && m->line != &m->direct_line && m->line->number == number;
}

void as_machine_run(AsMachine *m) {
    if (m->paused && m->line) {
        m->paused = 0;
        m->running = 1;
        m->break_skip = 1;
        return;
    }
    reset_runtime(m);
    m->line = m->prog ? m->prog->head : NULL;
    m->pc = 0;
    m->at_line_start = 1;
    m->running = m->line ? 1 : 0;
    m->paused = 0;
    m->status[0] = 0;
    m->step = 0;
}

void as_machine_stop(AsMachine *m) {
    m->running = 0;
    m->paused = 0;
    m->step = 0;
    m->wait = 0;
    m->sp = 0;
    m->can_cont = 0;
    if (m->screen) m->screen->cursor_on = 0;
    snprintf(m->status, sizeof m->status, "STOPPED");
}

void as_machine_pause(AsMachine *m) {
    if (!m->running) return;
    m->running = 0;
    m->paused = 1;
}

void as_machine_cont(AsMachine *m) {
    if (!m->paused) return;
    m->paused = 0;
    m->running = 1;
    m->break_skip = 1;
}

void as_machine_step(AsMachine *m) {
    if (!m->running && !m->paused) as_machine_run(m);
    m->step = 1;
    m->running = 1;
    m->paused = 0;
    m->break_skip = 1;
}

int as_machine_quantum(AsMachine *m, double seconds) {
    struct timespec t0, now;
    int steps = 0;
    if (m->wait) return m->wait;
    if (!m->running) return m->paused ? AS_PAUSED : (m->error ? m->error : AS_OK);
    clock_gettime(CLOCK_MONOTONIC, &t0);
    for (;;) {
        int rc = as_exec_one(m);
        if (rc == AS_INPUT || rc == AS_GET) return rc;
        if (rc == AS_PAUSED) return AS_PAUSED;
        if (rc == AS_END) {
            m->running = 0;
            return AS_END;
        }
        if (rc != AS_OK) {
            m->running = 0;
            return rc;
        }
        if (m->step) {
            m->step = 0;
            m->running = 0;
            m->paused = 1;
            return AS_PAUSED;
        }
        if (!m->running) return AS_END;
        if ((++steps & 31) == 0) {
            double elapsed;
            clock_gettime(CLOCK_MONOTONIC, &now);
            elapsed = (double)(now.tv_sec - t0.tv_sec) + (double)(now.tv_nsec - t0.tv_nsec) / 1e9;
            if (elapsed >= seconds) return AS_OK;
        }
    }
}

int as_machine_running(const AsMachine *m) { return m->running; }
int as_machine_paused(const AsMachine *m) { return m->paused; }
int as_machine_waiting(const AsMachine *m) { return m->wait; }
const char *as_machine_status(const AsMachine *m) { return m->status; }

uint16_t as_machine_line(const AsMachine *m) {
    if (!m->line || m->line == &m->direct_line) return 0;
    return m->line->number;
}

void as_machine_edited(AsMachine *m) {
    int drop = m->paused || m->can_cont;
    m->can_cont = 0;
    m->paused = 0;
    m->running = 0;
    m->wait = 0;
    m->step = 0;
    m->line = NULL;
    m->sp = 0;
    if (m->screen) m->screen->cursor_on = 0;
    if (drop) m->status[0] = 0;
}

void as_machine_keydown(AsMachine *m, int ch) {
    if (m->wait == AS_INPUT) {
        if (ch == '\r' || ch == '\n') {
            if (m->screen) as_screen_putc(m->screen, '\r');
            m->input_line[m->input_len] = 0;
            m->input_ready = 1;
            m->wait = 0;
            if (m->screen) m->screen->cursor_on = 0;
            return;
        }
        if (ch == 8 || ch == 127) {
            if (m->input_len > 0) {
                m->input_len--;
                if (m->screen) {
                    as_screen_backspace(m->screen);
                    as_screen_putc(m->screen, ' ');
                    as_screen_backspace(m->screen);
                }
            }
            return;
        }
        if (ch >= 32 && ch < 127 && m->input_len + 1 < sizeof m->input_line) {
            m->input_line[m->input_len++] = (char)ch;
            if (m->screen) as_screen_putc(m->screen, ch);
        }
        return;
    }
    if (m->wait == AS_GET) {
        m->got_key = ch == '\r' ? 13 : (ch & 0x7f);
        m->key_ready = 1;
        m->wait = 0;
        return;
    }
    if (m->screen) m->screen->key_strobe = (ch & 0x7f) | 0x80;
}

int as_check_line(const uint8_t *tokens, size_t len, size_t *err_at) {
    AsProgram *prog = as_program_new();
    AsScreen *screen = as_screen_new();
    AsMachine *m = as_machine_new(prog, screen);
    AsLine line;
    int rc = AS_OK;
    memset(&line, 0, sizeof line);
    line.tokens = (uint8_t *)tokens;
    line.len = len;
    m->check = 1;
    m->line = &line;
    m->pc = 0;
    m->at_line_start = 1;
    m->break_skip = 1;
    while (m->line && m->pc <= m->line->len) {
        size_t before = m->pc;
        if (m->pc >= m->line->len) break;
        rc = as_exec_one(m);
        if (rc != AS_OK) break;
        if (m->pc == before) break;
    }
    if (rc != AS_OK && err_at) *err_at = m->err_pc;
    as_machine_free(m);
    as_screen_free(screen);
    as_program_free(prog);
    return rc == AS_OK ? AS_OK : rc;
}
