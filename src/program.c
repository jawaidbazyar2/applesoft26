#include "as_program.h"

#include "as_common.h"
#include "as_token.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

AsProgram *as_program_new(void) {
    return (AsProgram *)as_xmalloc(sizeof(AsProgram));
}

static void free_line(AsLine *ln) {
    free(ln->tokens);
    free(ln);
}

void as_program_clear(AsProgram *p) {
    AsLine *ln = p->head;
    while (ln) {
        AsLine *n = ln->next;
        free_line(ln);
        ln = n;
    }
    p->head = NULL;
    p->count = 0;
    memset(p->targets, 0, sizeof p->targets);
}

void as_program_free(AsProgram *p) {
    if (!p) return;
    as_program_clear(p);
    free(p);
}

size_t as_program_count(const AsProgram *p) { return p->count; }

AsLine *as_program_at(AsProgram *p, size_t index) {
    AsLine *ln = p->head;
    while (ln && index) {
        ln = ln->next;
        index--;
    }
    return ln;
}

AsLine *as_program_find(AsProgram *p, uint16_t number) {
    AsLine *ln = p->head;
    while (ln) {
        if (ln->number == number) return ln;
        if (ln->number > number) return NULL;
        ln = ln->next;
    }
    return NULL;
}

static void target_set(AsProgram *p, uint16_t n) {
    p->targets[n >> 6] |= (uint64_t)1 << (n & 63);
}

int as_program_is_target(const AsProgram *p, uint16_t number) {
    return (p->targets[number >> 6] >> (number & 63)) & 1;
}

static int read_num(const uint8_t *t, size_t len, size_t pc, uint16_t *num, size_t *next) {
    unsigned v = 0;
    int any = 0;
    if (pc >= len || t[pc] < '0' || t[pc] > '9') return 0;
    while (pc < len && t[pc] >= '0' && t[pc] <= '9') {
        v = v * 10u + (unsigned)(t[pc] - '0');
        if (v > 63999u) return -1;
        pc++;
        any = 1;
    }
    if (!any) return 0;
    *num = (uint16_t)v;
    *next = pc;
    return 1;
}

static void copy_string(const uint8_t *t, size_t len, size_t *pc) {
    if (*pc < len && t[*pc] == '"') (*pc)++;
    while (*pc < len && t[*pc] != '"') (*pc)++;
    if (*pc < len && t[*pc] == '"') (*pc)++;
}

/* Record every explicit jump target in this token stream. */
static void collect_targets(AsProgram *p, const uint8_t *t, size_t len) {
    size_t pc = 0;
    while (pc < len) {
        uint8_t c = t[pc];
        if (c == TK_REM) return;
        if (c == '"') {
            copy_string(t, len, &pc);
            continue;
        }
        if (c == TK_DATA) {
            pc++;
            while (pc < len && t[pc] != ':') pc++;
            continue;
        }
        if (c == TK_ON) {
            int depth = 0;
            pc++;
            while (pc < len && t[pc] != ':') {
                if (t[pc] == '"') {
                    copy_string(t, len, &pc);
                    continue;
                }
                if (t[pc] == '(') depth++;
                else if (t[pc] == ')' && depth) depth--;
                if (depth == 0 && (t[pc] == TK_GOTO || t[pc] == TK_GOSUB)) break;
                pc++;
            }
            if (pc < len && (t[pc] == TK_GOTO || t[pc] == TK_GOSUB)) {
                pc++;
                for (;;) {
                    uint16_t num;
                    size_t next;
                    int r = read_num(t, len, pc, &num, &next);
                    if (r <= 0) break;
                    target_set(p, num);
                    pc = next;
                    if (pc < len && t[pc] == ',') {
                        pc++;
                        continue;
                    }
                    break;
                }
            }
            continue;
        }
        if (c == TK_GOTO || c == TK_GOSUB || c == TK_THEN || c == TK_RUN || c == TK_RESTORE) {
            uint16_t num;
            size_t next;
            pc++;
            if (read_num(t, len, pc, &num, &next) > 0) {
                target_set(p, num);
                pc = next;
            }
            continue;
        }
        pc++;
    }
}

void as_program_rebuild_targets(AsProgram *p) {
    AsLine *ln;
    memset(p->targets, 0, sizeof p->targets);
    for (ln = p->head; ln; ln = ln->next) collect_targets(p, ln->tokens, ln->len);
}

static uint8_t *dup_tokens(const uint8_t *tokens, size_t len) {
    uint8_t *d = (uint8_t *)as_xmalloc(len + 1);
    if (len) memcpy(d, tokens, len);
    d[len] = 0;
    return d;
}

int as_program_upsert(AsProgram *p, uint16_t number, const uint8_t *tokens, size_t len, int breakpoint) {
    AsLine *prev = NULL;
    AsLine *ln = p->head;
    if (number > 63999) return AS_ERR_ILLEGAL_QUANTITY;
    while (ln && ln->number < number) {
        prev = ln;
        ln = ln->next;
    }
    if (len == 0) {
        if (ln && ln->number == number) {
            if (prev) prev->next = ln->next;
            else p->head = ln->next;
            free_line(ln);
            p->count--;
            p->dirty = 1;
            as_program_rebuild_targets(p);
        }
        return AS_OK;
    }
    if (ln && ln->number == number) {
        free(ln->tokens);
        ln->tokens = dup_tokens(tokens, len);
        ln->len = len;
        ln->breakpoint = breakpoint;
    } else {
        AsLine *node = (AsLine *)as_xmalloc(sizeof(AsLine));
        node->number = number;
        node->breakpoint = breakpoint;
        node->tokens = dup_tokens(tokens, len);
        node->len = len;
        node->next = ln;
        if (prev) prev->next = node;
        else p->head = node;
        p->count++;
    }
    p->dirty = 1;
    as_program_rebuild_targets(p);
    return AS_OK;
}

void as_program_set_break(AsProgram *p, size_t index, int on) {
    AsLine *ln = as_program_at(p, index);
    if (ln) ln->breakpoint = on ? 1 : 0;
}

void as_program_delete_range(AsProgram *p, uint16_t first, uint16_t last) {
    AsLine *prev = NULL;
    AsLine *ln = p->head;
    if (first > last) {
        uint16_t t = first;
        first = last;
        last = t;
    }
    while (ln) {
        if (ln->number >= first && ln->number <= last) {
            AsLine *dead = ln;
            ln = ln->next;
            if (prev) prev->next = ln;
            else p->head = ln;
            free_line(dead);
            if (p->count) p->count--;
        } else {
            prev = ln;
            ln = ln->next;
        }
    }
    p->dirty = 1;
    as_program_rebuild_targets(p);
}

typedef struct {
    uint8_t *b;
    size_t n, cap;
} OutBuf;

static void ob_add(OutBuf *o, uint8_t c) {
    if (o->n + 1 >= o->cap) {
        o->cap = o->cap ? o->cap * 2 : 32;
        o->b = (uint8_t *)as_xrealloc(o->b, o->cap);
    }
    o->b[o->n++] = c;
}

static void ob_digits(OutBuf *o, unsigned v) {
    char tmp[8];
    int k = snprintf(tmp, sizeof tmp, "%u", v);
    int i;
    for (i = 0; i < k; i++) ob_add(o, (uint8_t)tmp[i]);
}

static uint8_t *rewrite_tokens(const uint8_t *t, size_t len, const int32_t *map, size_t *out_len) {
    size_t pc = 0;
    OutBuf o;
    memset(&o, 0, sizeof o);
    while (pc < len) {
        uint8_t c = t[pc];
        if (c == TK_REM) {
            while (pc < len) ob_add(&o, t[pc++]);
            break;
        }
        if (c == '"') {
            size_t start = pc;
            copy_string(t, len, &pc);
            while (start < pc) ob_add(&o, t[start++]);
            continue;
        }
        if (c == TK_DATA) {
            ob_add(&o, c);
            pc++;
            while (pc < len && t[pc] != ':') ob_add(&o, t[pc++]);
            continue;
        }
        if (c == TK_ON) {
            int depth = 0;
            ob_add(&o, c);
            pc++;
            while (pc < len && t[pc] != ':') {
                if (t[pc] == '"') {
                    size_t start = pc;
                    copy_string(t, len, &pc);
                    while (start < pc) ob_add(&o, t[start++]);
                    continue;
                }
                if (t[pc] == '(') depth++;
                else if (t[pc] == ')' && depth) depth--;
                if (depth == 0 && (t[pc] == TK_GOTO || t[pc] == TK_GOSUB)) break;
                ob_add(&o, t[pc++]);
            }
            if (pc < len && (t[pc] == TK_GOTO || t[pc] == TK_GOSUB)) {
                ob_add(&o, t[pc++]);
                for (;;) {
                    uint16_t num;
                    size_t next;
                    int r = read_num(t, len, pc, &num, &next);
                    if (r <= 0) break;
                    if (map[num] >= 0) ob_digits(&o, (unsigned)map[num]);
                    else ob_digits(&o, num);
                    pc = next;
                    if (pc < len && t[pc] == ',') {
                        ob_add(&o, ',');
                        pc++;
                        continue;
                    }
                    break;
                }
            }
            continue;
        }
        if (c == TK_GOTO || c == TK_GOSUB || c == TK_THEN || c == TK_RUN || c == TK_RESTORE) {
            uint16_t num;
            size_t next;
            ob_add(&o, c);
            pc++;
            if (read_num(t, len, pc, &num, &next) > 0) {
                if (map[num] >= 0) ob_digits(&o, (unsigned)map[num]);
                else ob_digits(&o, num);
                pc = next;
            }
            continue;
        }
        ob_add(&o, c);
        pc++;
    }
    ob_add(&o, 0);
    *out_len = o.n - 1;
    return o.b;
}

int as_program_renumber(AsProgram *p, uint16_t start, uint16_t step) {
    int32_t *map;
    uint16_t *neu;
    AsLine *ln;
    size_t i, n;
    unsigned num;
    if (step == 0) return AS_ERR_ILLEGAL_QUANTITY;
    n = p->count;
    map = (int32_t *)malloc(64000u * sizeof(int32_t));
    neu = (uint16_t *)malloc((n ? n : 1) * sizeof(uint16_t));
    if (!map || !neu) {
        free(map);
        free(neu);
        return AS_ERR_OUT_OF_MEMORY;
    }
    for (i = 0; i < 64000u; i++) map[i] = -1;
    num = start;
    i = 0;
    for (ln = p->head; ln; ln = ln->next, i++) {
        if (num > 63999u) {
            free(map);
            free(neu);
            return AS_ERR_ILLEGAL_QUANTITY;
        }
        map[ln->number] = (int32_t)num;
        neu[i] = (uint16_t)num;
        num += step;
    }
    i = 0;
    for (ln = p->head; ln; ln = ln->next, i++) {
        size_t nlen = 0;
        uint8_t *nb = rewrite_tokens(ln->tokens, ln->len, map, &nlen);
        free(ln->tokens);
        ln->tokens = nb;
        ln->len = nlen;
        ln->number = neu[i];
    }
    free(map);
    free(neu);
    p->dirty = 1;
    as_program_rebuild_targets(p);
    return AS_OK;
}

int as_program_dirty(const AsProgram *p) { return p->dirty; }
void as_program_mark_clean(AsProgram *p) { p->dirty = 0; }

static void set_err(char *err, size_t cap, const char *msg) {
    if (err && cap) snprintf(err, cap, "%s", msg);
}

int as_program_save(const AsProgram *p, uint8_t **out, size_t *out_len, char *err, size_t errcap) {
    AsLine *ln;
    size_t total = 2;
    uint8_t *buf;
    size_t n = 0;
    uint32_t addr = 0x0801;
    for (ln = p->head; ln; ln = ln->next) total += 4 + ln->len + 1;
    if (total > 0x8DFFu) {
        set_err(err, errcap, "program does not fit below HIMEM ($95FF)");
        return AS_ERR_OUT_OF_MEMORY;
    }
    buf = (uint8_t *)as_xmalloc(total);
    for (ln = p->head; ln; ln = ln->next) {
        uint32_t next = addr + 4u + (uint32_t)ln->len + 1u;
        buf[n++] = (uint8_t)(next & 0xff);
        buf[n++] = (uint8_t)((next >> 8) & 0xff);
        buf[n++] = (uint8_t)(ln->number & 0xff);
        buf[n++] = (uint8_t)((ln->number >> 8) & 0xff);
        if (ln->len) {
            memcpy(buf + n, ln->tokens, ln->len);
            n += ln->len;
        }
        buf[n++] = 0;
        addr = next;
    }
    buf[n++] = 0;
    buf[n++] = 0;
    *out = buf;
    *out_len = n;
    return AS_OK;
}

int as_program_load(AsProgram *p, const uint8_t *data, size_t len, char *err, size_t errcap) {
    AsProgram *tmp;
    size_t off = 0;
    uint16_t prev = 0;
    int have_prev = 0;
    if (!data || len < 2) {
        set_err(err, errcap, "not an AppleSoft program image");
        return AS_ERR_SYNTAX;
    }
    tmp = as_program_new();
    while (off + 1 < len) {
        uint16_t next = (uint16_t)(data[off] | (data[off + 1] << 8));
        uint32_t next_off;
        uint16_t lno;
        size_t tok_end, tok_len, i;
        if (next == 0) {
            if (off + 2 != len) {
                as_program_free(tmp);
                set_err(err, errcap, "trailing bytes after program");
                return AS_ERR_SYNTAX;
            }
            break;
        }
        if (next < 0x0801) {
            as_program_free(tmp);
            set_err(err, errcap, "bad next-line pointer");
            return AS_ERR_SYNTAX;
        }
        next_off = (uint32_t)next - 0x0801u;
        if (next_off > len || next_off <= off + 4) {
            as_program_free(tmp);
            set_err(err, errcap, "bad next-line pointer");
            return AS_ERR_SYNTAX;
        }
        lno = (uint16_t)(data[off + 2] | (data[off + 3] << 8));
        if (lno > 63999 || (have_prev && lno <= prev)) {
            as_program_free(tmp);
            set_err(err, errcap, "bad line number");
            return AS_ERR_SYNTAX;
        }
        tok_end = next_off;
        if (data[tok_end - 1] != 0) {
            as_program_free(tmp);
            set_err(err, errcap, "line is not terminated");
            return AS_ERR_SYNTAX;
        }
        for (i = off + 4; i + 1 < tok_end; i++) {
            if (data[i] == 0) {
                as_program_free(tmp);
                set_err(err, errcap, "embedded NUL in line");
                return AS_ERR_SYNTAX;
            }
        }
        tok_len = tok_end - (off + 4) - 1;
        if (as_program_upsert(tmp, lno, data + off + 4, tok_len, 0) != AS_OK) {
            as_program_free(tmp);
            set_err(err, errcap, "could not load line");
            return AS_ERR_OUT_OF_MEMORY;
        }
        prev = lno;
        have_prev = 1;
        off = next_off;
    }
    if (off + 2 != len) {
        as_program_free(tmp);
        set_err(err, errcap, "truncated program image");
        return AS_ERR_SYNTAX;
    }
    as_program_clear(p);
    p->head = tmp->head;
    p->count = tmp->count;
    tmp->head = NULL;
    tmp->count = 0;
    as_program_free(tmp);
    as_program_rebuild_targets(p);
    p->dirty = 0;
    return AS_OK;
}
