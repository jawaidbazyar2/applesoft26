#include "as_internal.h"

#include <ctype.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int parse_expr(AsMachine *m, int min_prec, AsValue *out);

static void set_num(AsValue *v, double n) {
    as_value_clear(v);
    v->type = AS_NUM;
    v->num = n;
}

static int fit_byte(AsMachine *m, double v, int *out) {
    if (!isfinite(v) || v < 0 || v >= 256) return as_fail(m, AS_ERR_ILLEGAL_QUANTITY);
    *out = (int)trunc(v);
    return AS_OK;
}

static int parse_number(AsMachine *m, AsValue *out) {
    char buf[80];
    size_t n = 0;
    char *end = NULL;
    double v;
    while (n + 1 < sizeof buf) {
        uint8_t c = as_peekc(m);
        if ((c >= '0' && c <= '9') || c == '.') {
            buf[n++] = (char)as_getc(m);
            continue;
        }
        break;
    }
    if (n && (as_peekc(m) == 'E')) {
        buf[n++] = 'E';
        as_getc(m);
        if (n + 1 < sizeof buf && (as_peekc(m) == TK_PLUS || as_peekc(m) == TK_MINUS)) {
            buf[n++] = as_peekc(m) == TK_MINUS ? '-' : '+';
            as_getc(m);
        }
        while (n + 1 < sizeof buf && as_peekc(m) >= '0' && as_peekc(m) <= '9') buf[n++] = (char)as_getc(m);
    }
    buf[n] = 0;
    v = strtod(buf, &end);
    if (end == buf) return as_fail(m, AS_ERR_SYNTAX);
    if (!isfinite(v)) return as_fail(m, AS_ERR_OVERFLOW);
    set_num(out, v);
    return AS_OK;
}

static int parse_string(AsMachine *m, AsValue *out) {
    uint8_t tmp[256];
    size_t n = 0;
    as_getc(m);
    while (as_peekc(m) && as_peekc(m) != '"') {
        if (n >= 255) return as_fail(m, AS_ERR_STRING_TOO_LONG);
        tmp[n++] = as_getc(m);
    }
    if (as_peekc(m) == '"') as_getc(m);
    out->type = AS_STR;
    return as_str_set(&out->str, tmp, n);
}

int as_parse_ref(AsMachine *m, AsRef *ref) {
    uint8_t c1, n;
    int c2 = 0;
    memset(ref, 0, sizeof *ref);
    c1 = as_peekc(m);
    if (c1 < 'A' || c1 > 'Z') return as_fail(m, AS_ERR_SYNTAX);
    as_getc(m);
    n = as_peekc(m);
    if ((n >= 'A' && n <= 'Z') || (n >= '0' && n <= '9')) {
        c2 = n;
        as_getc(m);
        for (;;) {
            uint8_t e = as_peekc(m);
            if ((e >= 'A' && e <= 'Z') || (e >= '0' && e <= '9')) as_getc(m);
            else break;
        }
    }
    ref->type = AS_T_FLOAT;
    if (as_peekc(m) == '$') {
        as_getc(m);
        ref->type = AS_T_STR;
    } else if (as_peekc(m) == '%') {
        as_getc(m);
        ref->type = AS_T_INT;
    }
    ref->name = as_name_index(c1, c2);
    if (ref->name < 0) return as_fail(m, AS_ERR_SYNTAX);
    if (as_peekc(m) == '(') {
        as_getc(m);
        ref->is_array = 1;
        for (;;) {
            double s;
            int rc = as_eval_num(m, &s);
            if (rc) return rc;
            if (ref->ndims >= 8) return as_fail(m, AS_ERR_BAD_SUBSCRIPT);
            if (!isfinite(s) || s < 0 || s > 32767) return as_fail(m, AS_ERR_ILLEGAL_QUANTITY);
            ref->subs[ref->ndims++] = (int)trunc(s);
            if (as_peekc(m) == ',') {
                as_getc(m);
                continue;
            }
            break;
        }
        if (as_getc(m) != ')') return as_fail(m, AS_ERR_SYNTAX);
    }
    return AS_OK;
}

static int fn_call(AsMachine *m, AsValue *out) {
    int letter, rc;
    double arg, saved = 0;
    AsFn *fn;
    AsLine tmp, *save_line;
    size_t save_pc;
    letter = as_peekc(m);
    if (letter < 'A' || letter > 'Z') return as_fail(m, AS_ERR_SYNTAX);
    as_getc(m);
    while ((as_peekc(m) >= 'A' && as_peekc(m) <= 'Z') || (as_peekc(m) >= '0' && as_peekc(m) <= '9')) as_getc(m);
    if (as_getc(m) != '(') return as_fail(m, AS_ERR_SYNTAX);
    rc = as_eval_num(m, &arg);
    if (rc) return rc;
    if (as_getc(m) != ')') return as_fail(m, AS_ERR_SYNTAX);
    if (m->check) {
        set_num(out, 0);
        return AS_OK;
    }
    fn = &m->fn[letter - 'A'];
    if (!fn->used) return as_fail(m, AS_ERR_UNDEF_FUNCTION);
    as_vars_get_scalar(m->vars, fn->ptype, fn->pname, &saved);
    rc = as_vars_set_scalar(m->vars, fn->ptype, fn->pname, arg);
    if (rc) return as_fail(m, rc);
    memset(&tmp, 0, sizeof tmp);
    tmp.tokens = fn->body;
    tmp.len = fn->len;
    tmp.number = m->line ? m->line->number : 0;
    save_line = m->line;
    save_pc = m->pc;
    m->line = &tmp;
    m->pc = 0;
    rc = as_eval(m, out);
    m->line = save_line;
    m->pc = save_pc;
    as_vars_set_scalar(m->vars, fn->ptype, fn->pname, saved);
    return rc;
}

static int string_fn(AsMachine *m, int token, AsValue *out) {
    AsValue src;
    double a = 0, b = 0;
    int rc, start, count, has_len = 0;
    size_t from, ncopy;
    memset(&src, 0, sizeof src);
    if (as_getc(m) != '(') return as_fail(m, AS_ERR_SYNTAX);
    rc = as_eval(m, &src);
    if (rc) return rc;
    if (src.type != AS_STR) {
        as_value_clear(&src);
        return as_fail(m, AS_ERR_TYPE_MISMATCH);
    }
    if (as_getc(m) != ',') {
        as_value_clear(&src);
        return as_fail(m, AS_ERR_SYNTAX);
    }
    rc = as_eval_num(m, &a);
    if (rc) {
        as_value_clear(&src);
        return rc;
    }
    if (token == TK_MID && as_peekc(m) == ',') {
        as_getc(m);
        rc = as_eval_num(m, &b);
        if (rc) {
            as_value_clear(&src);
            return rc;
        }
        has_len = 1;
    }
    if (as_getc(m) != ')') {
        as_value_clear(&src);
        return as_fail(m, AS_ERR_SYNTAX);
    }
    if (token == TK_MID) {
        if (!isfinite(a) || a < 1 || a >= 256) {
            as_value_clear(&src);
            return as_fail(m, AS_ERR_ILLEGAL_QUANTITY);
        }
        start = (int)trunc(a);
        if (!has_len) count = 255;
        else {
            if (!isfinite(b) || b < 0 || b >= 256) {
                as_value_clear(&src);
                return as_fail(m, AS_ERR_ILLEGAL_QUANTITY);
            }
            count = (int)trunc(b);
        }
        if (start > (int)src.str.len) {
            as_value_clear(&src);
            out->type = AS_STR;
            return as_str_set(&out->str, NULL, 0);
        }
        from = (size_t)(start - 1);
        ncopy = src.str.len - from;
        if ((int)ncopy > count) ncopy = (size_t)count;
    } else {
        if (!isfinite(a) || a < 0 || a >= 256) {
            as_value_clear(&src);
            return as_fail(m, AS_ERR_ILLEGAL_QUANTITY);
        }
        count = (int)trunc(a);
        if (count > (int)src.str.len) count = (int)src.str.len;
        ncopy = (size_t)count;
        from = (token == TK_RIGHT) ? src.str.len - ncopy : 0;
    }
    out->type = AS_STR;
    rc = as_str_set(&out->str, src.str.data ? src.str.data + from : NULL, ncopy);
    as_value_clear(&src);
    return rc;
}

static int one_fn(AsMachine *m, int token, AsValue *out) {
    double v = 0;
    int rc, byte = 0, addr = 0, col = 0;
    AsValue sv;
    if (token == TK_USR) {
        if (as_getc(m) != '(') return as_fail(m, AS_ERR_SYNTAX);
        rc = as_eval_num(m, &v);
        if (rc) return rc;
        if (as_getc(m) != ')') return as_fail(m, AS_ERR_SYNTAX);
        if (m->check) {
            set_num(out, 0);
            return AS_OK;
        }
        return as_fail(m, AS_ERR_NOT_SUPPORTED);
    }
    if (token == TK_SCRN) {
        double yv;
        int x, y, color = 0;
        if (as_getc(m) != '(') return as_fail(m, AS_ERR_SYNTAX);
        rc = as_eval_num(m, &v);
        if (rc) return rc;
        if (as_getc(m) != ',') return as_fail(m, AS_ERR_SYNTAX);
        rc = as_eval_num(m, &yv);
        if (rc) return rc;
        if (as_getc(m) != ')') return as_fail(m, AS_ERR_SYNTAX);
        if (!isfinite(v) || !isfinite(yv)) return as_fail(m, AS_ERR_OVERFLOW);
        x = (int)trunc(v);
        y = (int)trunc(yv);
        if (m->screen) {
            rc = as_screen_scrn(m->screen, x, y, &color);
            if (rc) return as_fail(m, rc);
        }
        set_num(out, color);
        return AS_OK;
    }
    if (as_getc(m) != '(') return as_fail(m, AS_ERR_SYNTAX);
    if (token == TK_LEN || token == TK_VAL || token == TK_ASC) {
        memset(&sv, 0, sizeof sv);
        rc = as_eval(m, &sv);
        if (rc) return rc;
        if (as_getc(m) != ')') {
            as_value_clear(&sv);
            return as_fail(m, AS_ERR_SYNTAX);
        }
        if (sv.type != AS_STR) {
            as_value_clear(&sv);
            return as_fail(m, AS_ERR_TYPE_MISMATCH);
        }
        if (token == TK_LEN) set_num(out, (double)sv.str.len);
        else if (token == TK_ASC) {
            if (sv.str.len < 1) {
                as_value_clear(&sv);
                return as_fail(m, AS_ERR_ILLEGAL_QUANTITY);
            }
            set_num(out, sv.str.data[0]);
        } else {
            char buf[256];
            char *end = NULL;
            size_t i = 0;
            double n;
            while (i < sv.str.len && i + 1 < sizeof buf) {
                buf[i] = (char)sv.str.data[i];
                i++;
            }
            buf[i] = 0;
            n = strtod(buf, &end);
            set_num(out, end == buf ? 0 : n);
        }
        as_value_clear(&sv);
        return AS_OK;
    }
    rc = as_eval_num(m, &v);
    if (rc) return rc;
    if (as_getc(m) != ')') return as_fail(m, AS_ERR_SYNTAX);
    if (!isfinite(v) && token != TK_FRE && token != TK_POS) return as_fail(m, AS_ERR_OVERFLOW);
    switch (token) {
    case TK_SGN:
        set_num(out, v < 0 ? -1 : v > 0 ? 1 : 0);
        return AS_OK;
    case TK_INT:
        set_num(out, floor(v));
        return AS_OK;
    case TK_ABS:
        set_num(out, fabs(v));
        return AS_OK;
    case TK_FRE:
        set_num(out, 32000);
        return AS_OK;
    case TK_PDL:
        rc = fit_byte(m, v, &byte);
        if (rc) return rc;
        if (m->check) {
            set_num(out, 0);
            return AS_OK;
        }
        return as_do_pdl(m, byte, &out->num) ? as_fail(m, m->error) : (out->type = AS_NUM, AS_OK);
    case TK_POS:
        set_num(out, m->screen ? m->screen->cur_x : 0);
        return AS_OK;
    case TK_SQR:
        if (v < 0) return as_fail(m, AS_ERR_ILLEGAL_QUANTITY);
        set_num(out, sqrt(v));
        return AS_OK;
    case TK_RND: {
        if (v < 0) {
            double a = fabs(v);
            m->rnd_state = (uint32_t)a;
            if (m->rnd_state == 0) m->rnd_state = 1;
        }
        if (v != 0 || !m->rnd_have) {
            m->rnd_state = m->rnd_state * 1664525u + 1013904223u;
            m->rnd_last = ((m->rnd_state >> 8) & 0xffffff) / 16777216.0;
            if (m->rnd_last <= 0) m->rnd_last = 1.0 / 16777216.0;
            m->rnd_have = 1;
        }
        set_num(out, m->rnd_last);
        return AS_OK;
    }
    case TK_LOG:
        if (v <= 0) return as_fail(m, AS_ERR_ILLEGAL_QUANTITY);
        set_num(out, log(v));
        return AS_OK;
    case TK_EXP: {
        double r = exp(v);
        if (!isfinite(r)) return as_fail(m, AS_ERR_OVERFLOW);
        set_num(out, r);
        return AS_OK;
    }
    case TK_COS:
        set_num(out, cos(v));
        return AS_OK;
    case TK_SIN:
        set_num(out, sin(v));
        return AS_OK;
    case TK_TAN: {
        double r = tan(v);
        if (!isfinite(r)) return as_fail(m, AS_ERR_OVERFLOW);
        set_num(out, r);
        return AS_OK;
    }
    case TK_ATN:
        set_num(out, atan(v));
        return AS_OK;
    case TK_PEEK:
        if (v < 0 || v > 65535) return as_fail(m, AS_ERR_ILLEGAL_QUANTITY);
        addr = (int)trunc(v);
        rc = as_do_peek(m, addr, &out->num);
        if (rc) return rc;
        out->type = AS_NUM;
        return AS_OK;
    case TK_STR: {
        char tmp[64];
        char body[64];
        if (v == 0) snprintf(body, sizeof body, "0");
        else snprintf(body, sizeof body, "%.9g", fabs(v));
        {
            char *e = strchr(body, 'e');
            if (e) *e = 'E';
        }
        if (v < 0) snprintf(tmp, sizeof tmp, "-%s", body);
        else snprintf(tmp, sizeof tmp, " %s", body);
        out->type = AS_STR;
        return as_str_set_cstr(&out->str, tmp);
    }
    case TK_CHR:
        rc = fit_byte(m, v, &byte);
        if (rc) return rc;
        {
            uint8_t ch = (uint8_t)byte;
            out->type = AS_STR;
            return as_str_set(&out->str, &ch, 1);
        }
    default:
        (void)col;
        return as_fail(m, AS_ERR_SYNTAX);
    }
}

static int parse_primary(AsMachine *m, AsValue *out) {
    uint8_t c = as_peekc(m);
    if (c == TK_PLUS) {
        as_getc(m);
        return parse_primary(m, out);
    }
    if (c == TK_MINUS || c == TK_NOT) {
        int neg = c == TK_MINUS;
        AsValue v;
        int rc;
        as_getc(m);
        memset(&v, 0, sizeof v);
        rc = parse_expr(m, 0x7F, &v);
        if (rc) return rc;
        if (v.type != AS_NUM) {
            as_value_clear(&v);
            return as_fail(m, AS_ERR_TYPE_MISMATCH);
        }
        if (neg) {
            double r = -v.num;
            if (!isfinite(r)) {
                as_value_clear(&v);
                return as_fail(m, AS_ERR_OVERFLOW);
            }
            set_num(out, r);
        } else {
            set_num(out, v.num == 0 ? 1 : 0);
        }
        as_value_clear(&v);
        return AS_OK;
    }
    if (c == '"') return parse_string(m, out);
    if (c == '(') {
        int rc;
        as_getc(m);
        rc = parse_expr(m, 0, out);
        if (rc) return rc;
        if (as_getc(m) != ')') return as_fail(m, AS_ERR_SYNTAX);
        return AS_OK;
    }
    if (c == '.' || (c >= '0' && c <= '9')) return parse_number(m, out);
    if (c == TK_FN) {
        as_getc(m);
        return fn_call(m, out);
    }
    if (c >= TK_SGN && c <= TK_MID) {
        int token = as_getc(m);
        if (token >= TK_LEFT && token <= TK_MID) return string_fn(m, token, out);
        return one_fn(m, token, out);
    }
    if (c >= 'A' && c <= 'Z') {
        AsRef ref;
        int rc = as_parse_ref(m, &ref);
        if (rc) return rc;
        if (m->check) {
            if (ref.type == AS_T_STR) {
                out->type = AS_STR;
                return as_str_set(&out->str, NULL, 0);
            }
            set_num(out, 0);
            return AS_OK;
        }
        return as_vars_load(m->vars, &ref, out);
    }
    return as_fail(m, AS_ERR_SYNTAX);
}

static int bin_prec(uint8_t c) {
    switch (c) {
    case TK_OR: return 0x46;
    case TK_AND: return 0x50;
    case TK_PLUS:
    case TK_MINUS: return 0x79;
    case TK_STAR:
    case TK_SLASH: return 0x7B;
    case TK_CARET: return 0x7D;
    default: return -1;
    }
}

static int take_rel(AsMachine *m, int *mask) {
    int bits = 0;
    for (;;) {
        uint8_t c = as_peekc(m);
        int bit = 0;
        if (c == TK_GT) bit = 1;
        else if (c == TK_EQ) bit = 2;
        else if (c == TK_LT) bit = 4;
        else break;
        if (bits & bit) return as_fail(m, AS_ERR_SYNTAX);
        bits |= bit;
        as_getc(m);
    }
    if (!bits) return as_fail(m, AS_ERR_SYNTAX);
    *mask = bits;
    return AS_OK;
}

static int cmp_mem(const AsStr *a, const AsStr *b) {
    size_t n = a->len < b->len ? a->len : b->len;
    int c = 0;
    if (n && a->data && b->data) c = memcmp(a->data, b->data, n);
    if (c < 0) return -1;
    if (c > 0) return 1;
    if (a->len < b->len) return -1;
    if (a->len > b->len) return 1;
    return 0;
}

static int apply_op(AsMachine *m, int rel, int op, AsValue *lhs, AsValue *rhs) {
    if (rel) {
        int cmp, ok = 0;
        if (lhs->type != rhs->type) return as_fail(m, AS_ERR_TYPE_MISMATCH);
        if (lhs->type == AS_STR) cmp = cmp_mem(&lhs->str, &rhs->str);
        else if (lhs->num < rhs->num) cmp = -1;
        else if (lhs->num > rhs->num) cmp = 1;
        else cmp = 0;
        if ((op & 4) && cmp < 0) ok = 1;
        if ((op & 2) && cmp == 0) ok = 1;
        if ((op & 1) && cmp > 0) ok = 1;
        set_num(lhs, ok);
        return AS_OK;
    }
    if (op == TK_PLUS && (lhs->type == AS_STR || rhs->type == AS_STR)) {
        uint8_t *d;
        size_t n;
        if (lhs->type != AS_STR || rhs->type != AS_STR) return as_fail(m, AS_ERR_TYPE_MISMATCH);
        if (lhs->str.len + rhs->str.len > 255) return as_fail(m, AS_ERR_STRING_TOO_LONG);
        n = lhs->str.len + rhs->str.len;
        d = (uint8_t *)as_xmalloc(n + 1);
        if (lhs->str.len && lhs->str.data) memcpy(d, lhs->str.data, lhs->str.len);
        if (rhs->str.len && rhs->str.data) memcpy(d + lhs->str.len, rhs->str.data, rhs->str.len);
        d[n] = 0;
        as_str_clear(&lhs->str);
        lhs->str.data = d;
        lhs->str.len = n;
        lhs->type = AS_STR;
        return AS_OK;
    }
    if (lhs->type != AS_NUM || rhs->type != AS_NUM) return as_fail(m, AS_ERR_TYPE_MISMATCH);
    {
        double a = lhs->num, b = rhs->num, r = 0;
        switch (op) {
        case TK_PLUS: r = a + b; break;
        case TK_MINUS: r = a - b; break;
        case TK_STAR: r = a * b; break;
        case TK_SLASH:
            if (b == 0) return as_fail(m, AS_ERR_DIVISION_BY_ZERO);
            r = a / b;
            break;
        case TK_CARET:
            r = pow(a, b);
            break;
        case TK_AND:
            r = (a != 0.0 && b != 0.0) ? 1 : 0;
            break;
        case TK_OR:
            r = (a != 0.0 || b != 0.0) ? 1 : 0;
            break;
        default: return as_fail(m, AS_ERR_SYNTAX);
        }
        if (!isfinite(r)) return as_fail(m, AS_ERR_OVERFLOW);
        set_num(lhs, r);
        return AS_OK;
    }
}

static int parse_expr_body(AsMachine *m, int min_prec, AsValue *out) {
    int rc = parse_primary(m, out);
    if (rc) return rc;
    for (;;) {
        uint8_t c = as_peekc(m);
        int rel = (c == TK_GT || c == TK_EQ || c == TK_LT);
        int prec = rel ? 0x64 : bin_prec(c);
        int op = c;
        int mask = 0;
        AsValue rhs;
        if (prec < 0 || prec < min_prec) break;
        if (rel) {
            rc = take_rel(m, &mask);
            if (rc) return rc;
        } else {
            as_getc(m);
        }
        memset(&rhs, 0, sizeof rhs);
        rc = parse_expr(m, prec + 1, &rhs);
        if (rc) {
            as_value_clear(&rhs);
            return rc;
        }
        rc = apply_op(m, rel, rel ? mask : op, out, &rhs);
        as_value_clear(&rhs);
        if (rc) return rc;
    }
    return AS_OK;
}

static int parse_expr(AsMachine *m, int min_prec, AsValue *out) {
    int rc;
    if (++m->depth > 64) {
        --m->depth;
        return as_fail(m, AS_ERR_FORMULA_TOO_COMPLEX);
    }
    rc = parse_expr_body(m, min_prec, out);
    --m->depth;
    if (rc) as_value_clear(out);
    return rc;
}

int as_eval(AsMachine *m, AsValue *out) {
    memset(out, 0, sizeof *out);
    return parse_expr(m, 0, out);
}

int as_eval_num(AsMachine *m, double *out) {
    AsValue v;
    int rc = as_eval(m, &v);
    if (rc) return rc;
    if (v.type != AS_NUM) {
        as_value_clear(&v);
        return as_fail(m, AS_ERR_TYPE_MISMATCH);
    }
    *out = v.num;
    as_value_clear(&v);
    return AS_OK;
}
