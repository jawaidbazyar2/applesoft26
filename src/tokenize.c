#include "as_tokenize.h"

#include "as_token.h"

#include <ctype.h>
#include <stdlib.h>
#include <string.h>

/* Longest extension name at s[i], or 0. More letters may follow (the argument). */
static size_t ext_command(const char *s, size_t n, size_t i) {
    static const char *const words[] = {"DELAY", "TONE", "DHGR", "DHCOLOR", "DHPLOT"};
    size_t w, best = 0;
    for (w = 0; w < sizeof words / sizeof words[0]; w++) {
        size_t k = 0;
        while (words[w][k] && i + k < n) {
            char c = (char)toupper((unsigned char)s[i + k]);
            if (c != words[w][k]) break;
            k++;
        }
        if (!words[w][k] && k > best) best = k;
    }
    return best;
}

static int kw_match(const char *s, size_t n, size_t i, int *token, size_t *end) {
    int t;
    for (t = 0; t < TK_COUNT; t++) {
        const char *w = as_keyword(TK_FIRST + t);
        size_t j = i;
        size_t k = 0;
        int ok = 1;
        while (w[k]) {
            while (j < n && s[j] == ' ') j++;
            if (j >= n || (char)toupper((unsigned char)s[j]) != w[k]) {
                ok = 0;
                break;
            }
            j++;
            k++;
        }
        if (!ok) continue;
        if ((TK_FIRST + t) == TK_AT) {
            size_t k2 = j;
            /* "A THEN" / "A TO" must not become AT by skipping a space. */
            if (j - i != 2) continue;
            while (k2 < n && s[k2] == ' ') k2++;
            if (k2 < n) {
                char c = (char)toupper((unsigned char)s[k2]);
                if (c == 'N' || c == 'O') continue;
            }
        }
        *token = TK_FIRST + t;
        *end = j;
        return 1;
    }
    return 0;
}

typedef struct Buf {
    uint8_t *b;
    uint32_t *src;
    uint16_t *src_n;
    size_t n, cap;
} Buf;

static void buf_add(Buf *b, uint8_t c, uint32_t src, uint16_t src_n) {
    if (b->n + 1 >= b->cap) {
        size_t ncap = b->cap ? b->cap * 2 : 32;
        b->b = (uint8_t *)as_xrealloc(b->b, ncap);
        b->src = (uint32_t *)as_xrealloc(b->src, ncap * sizeof(uint32_t));
        b->src_n = (uint16_t *)as_xrealloc(b->src_n, ncap * sizeof(uint16_t));
        b->cap = ncap;
    }
    b->b[b->n] = c;
    b->src[b->n] = src;
    b->src_n[b->n] = src_n;
    b->n++;
}

static void mark_kw(uint8_t *klass, const char *s, size_t begin, size_t end) {
    size_t i;
    for (i = begin; i < end; i++) {
        if (s[i] != ' ') klass[i] = AS_CLS_KEYWORD;
    }
}

void as_tokenized_free(AsTokenized *t) {
    if (!t) return;
    free(t->bytes);
    free(t->src);
    free(t->src_n);
    free(t->klass);
    memset(t, 0, sizeof *t);
}

void as_tokenize(const char *src, AsTokenized *out) {
    size_t n, i;
    int data = 0;
    int amp_name = 0;
    Buf buf;
    uint8_t *klass;
    memset(out, 0, sizeof *out);
    if (!src) src = "";
    n = strlen(src);
    klass = (uint8_t *)as_xmalloc(n ? n : 1);
    memset(&buf, 0, sizeof buf);

    i = 0;
    while (i < n) {
        unsigned char c = (unsigned char)src[i];
        if (!data && c == ' ') {
            i++;
            continue;
        }
        if (!data && c == '"') {
            size_t start = i++;
            while (i < n && src[i] != '"') i++;
            if (i < n && src[i] == '"') i++;
            while (start < i) {
                buf_add(&buf, (uint8_t)src[start], (uint32_t)start, 1);
                start++;
            }
            continue;
        }
        if (data) {
            if (c == ':') {
                data = 0;
                buf_add(&buf, ':', (uint32_t)i, 1);
                i++;
                continue;
            }
            if (c == '"') {
                size_t start = i++;
                while (i < n && src[i] != '"') i++;
                if (i < n && src[i] == '"') i++;
                while (start < i) {
                    buf_add(&buf, (uint8_t)src[start], (uint32_t)start, 1);
                    start++;
                }
                continue;
            }
            buf_add(&buf, (uint8_t)c, (uint32_t)i, 1);
            i++;
            continue;
        }
        if (c == '?') {
            buf_add(&buf, TK_PRINT, (uint32_t)i, 1);
            klass[i] = AS_CLS_KEYWORD;
            i++;
            continue;
        }
        /* The word after & is an extension name, stored as letters. DEL and TO
           would otherwise swallow DELAY and TONE, and HGR/PLOT/COLOR would
           split DHGR, DHPLOT and DHCOLOR. */
        if (amp_name) {
            if ((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z')) {
                size_t cmd = ext_command(src, n, i);
                size_t end = i;
                if (cmd) end = i + cmd;
                else {
                    while (end < n) {
                        unsigned char ch = (unsigned char)src[end];
                        if (ch >= 'a' && ch <= 'z') ch = (unsigned char)(ch - 32);
                        if (ch < 'A' || ch > 'Z') break;
                        end++;
                    }
                }
                while (i < end) {
                    unsigned char ch = (unsigned char)src[i];
                    if (ch >= 'a' && ch <= 'z') ch = (unsigned char)(ch - 32);
                    buf_add(&buf, (uint8_t)ch, (uint32_t)i, 1);
                    klass[i] = AS_CLS_KEYWORD;
                    i++;
                }
                amp_name = 0;
                /* Keep one space so "& TONE A" does not come back as "& TONEA". */
                if (i < n && src[i] == ' ') {
                    buf_add(&buf, ' ', (uint32_t)i, 1);
                    while (i < n && src[i] == ' ') i++;
                }
                continue;
            }
            amp_name = 0;
        }
        if (c >= '0' && c <= ';') {
            int varish = 0;
            if (i > 0 && klass[i - 1] == AS_CLS_VARIABLE && c >= '0' && c <= '9') varish = 1;
            buf_add(&buf, (uint8_t)c, (uint32_t)i, 1);
            if (varish) klass[i] = AS_CLS_VARIABLE;
            i++;
            continue;
        }
        {
            int token = 0;
            size_t end = i;
            if (kw_match(src, n, i, &token, &end)) {
                uint16_t span = (uint16_t)(end - i);
                if (span == 0) span = 1;
                buf_add(&buf, (uint8_t)token, (uint32_t)i, span);
                mark_kw(klass, src, i, end);
                i = end;
                amp_name = (token == TK_AMP);
                if (token == TK_DATA) data = 1;
                if (token == TK_REM) {
                    while (i < n) {
                        buf_add(&buf, (uint8_t)src[i], (uint32_t)i, 1);
                        klass[i] = AS_CLS_REMARK;
                        i++;
                    }
                    break;
                }
                continue;
            }
        }
        {
            uint8_t outc = (uint8_t)c;
            int cls = AS_CLS_PLAIN;
            if (c >= 'a' && c <= 'z') outc = (uint8_t)(c - 32);
            if ((outc >= 'A' && outc <= 'Z')) cls = AS_CLS_VARIABLE;
            else if ((c == '$' || c == '%') && i > 0 && klass[i - 1] == AS_CLS_VARIABLE)
                cls = AS_CLS_VARIABLE;
            buf_add(&buf, outc, (uint32_t)i, 1);
            klass[i] = (uint8_t)cls;
            i++;
        }
    }
    buf_add(&buf, 0, (uint32_t)n, 0);
    out->bytes = buf.b;
    out->len = buf.n - 1;
    out->src = buf.src;
    out->src_n = buf.src_n;
    out->klass = klass;
    out->source_len = n;
}

static void skip_string_tokens(const AsTokenized *t, size_t *pc) {
    if (*pc < t->len && t->bytes[*pc] == '"') (*pc)++;
    while (*pc < t->len && t->bytes[*pc] != '"') (*pc)++;
    if (*pc < t->len && t->bytes[*pc] == '"') (*pc)++;
}

/* 1 = number, 0 = not a number, -1 = digits that are not a legal line number. */
static int take_num(const AsTokenized *t, size_t *pc, uint16_t *num, size_t *begin, size_t *end) {
    size_t i = *pc;
    unsigned v = 0;
    int overflow = 0;
    if (i >= t->len || t->bytes[i] < '0' || t->bytes[i] > '9') return 0;
    *begin = t->src[i];
    while (i < t->len && t->bytes[i] >= '0' && t->bytes[i] <= '9') {
        if (v > 6399u) overflow = 1;
        else v = v * 10u + (unsigned)(t->bytes[i] - '0');
        if (v > 63999u) overflow = 1;
        i++;
    }
    *end = (size_t)t->src[i - 1] + t->src_n[i - 1];
    *pc = i;
    if (overflow) return -1;
    *num = (uint16_t)v;
    return 1;
}

static int each_jump(const AsTokenized *t, int (*fn)(uint16_t, size_t, size_t, void *), void *ctx) {
    size_t pc = 0;
    while (pc < t->len) {
        uint8_t c = t->bytes[pc];
        if (c == TK_REM) return 0;
        if (c == '"') {
            skip_string_tokens(t, &pc);
            continue;
        }
        if (c == TK_DATA) {
            pc++;
            while (pc < t->len && t->bytes[pc] != ':') pc++;
            continue;
        }
        if (c == TK_ON) {
            int depth = 0;
            pc++;
            while (pc < t->len && t->bytes[pc] != ':') {
                if (t->bytes[pc] == '"') {
                    skip_string_tokens(t, &pc);
                    continue;
                }
                if (t->bytes[pc] == '(') depth++;
                else if (t->bytes[pc] == ')' && depth) depth--;
                if (depth == 0 && (t->bytes[pc] == TK_GOTO || t->bytes[pc] == TK_GOSUB)) break;
                pc++;
            }
            if (pc < t->len && (t->bytes[pc] == TK_GOTO || t->bytes[pc] == TK_GOSUB)) {
                pc++;
                for (;;) {
                    uint16_t num;
                    size_t b, e;
                    int r = take_num(t, &pc, &num, &b, &e);
                    if (r <= 0) break;
                    if (fn(num, b, e, ctx)) return 1;
                    if (pc < t->len && t->bytes[pc] == ',') {
                        pc++;
                        continue;
                    }
                    break;
                }
            }
            continue;
        }
        if (c == TK_GOTO || c == TK_GOSUB || c == TK_THEN) {
            uint16_t num;
            size_t b, e;
            int r;
            pc++;
            r = take_num(t, &pc, &num, &b, &e);
            if (r > 0 && fn(num, b, e, ctx)) return 1;
            continue;
        }
        pc++;
    }
    return 0;
}

struct JumpAt {
    size_t index;
    uint16_t *number;
    size_t *begin;
    size_t *end;
};

static int jump_at_fn(uint16_t num, size_t b, size_t e, void *ctx) {
    struct JumpAt *at = ctx;
    if (at->index < b || at->index >= e) return 0;
    *at->number = num;
    *at->begin = b;
    *at->end = e;
    return 1;
}

struct JumpList {
    AsJumpRef *out;
    size_t cap;
    size_t n;
};

static int jump_list_fn(uint16_t num, size_t b, size_t e, void *ctx) {
    struct JumpList *list = ctx;
    if (list->n < list->cap && b <= e && e <= UINT32_MAX) {
        list->out[list->n].number = num;
        list->out[list->n].begin = (uint32_t)b;
        list->out[list->n].end = (uint32_t)e;
        list->n++;
    }
    return 0;
}

size_t as_jump_numbers(const char *src, AsJumpRef *out, size_t cap) {
    AsTokenized t;
    struct JumpList list;
    if (!src || !out || cap == 0) return 0;
    list.out = out;
    list.cap = cap;
    list.n = 0;
    as_tokenize(src, &t);
    each_jump(&t, jump_list_fn, &list);
    as_tokenized_free(&t);
    return list.n;
}

int as_jump_number_at(const char *src, size_t index, uint16_t *number, size_t *begin, size_t *end) {
    AsTokenized t;
    struct JumpAt at;
    int hit;
    if (!src || !number || !begin || !end) return 0;
    at.index = index;
    at.number = number;
    at.begin = begin;
    at.end = end;
    as_tokenize(src, &t);
    hit = each_jump(&t, jump_at_fn, &at);
    as_tokenized_free(&t);
    return hit;
}

int as_detokenize(const uint8_t *bytes, size_t len, char **text) {
    size_t cap = 64, n = 0, i;
    int amp_name = 0;
    char *d = (char *)as_xmalloc(cap);
    for (i = 0; i < len; i++) {
        uint8_t c = bytes[i];
        const char *kw;
        size_t klen, need;
        if (c >= TK_FIRST && (kw = as_keyword(c)) != NULL) {
            klen = strlen(kw);
            need = n + klen + 2 + 1;
            if (need > cap) {
                while (cap < need) cap *= 2;
                d = (char *)as_xrealloc(d, cap);
            }
            d[n++] = ' ';
            memcpy(d + n, kw, klen);
            n += klen;
            d[n++] = ' ';
            amp_name = (c == TK_AMP);
        } else {
            int gap = amp_name && (c < 'A' || c > 'Z');
            if (gap) amp_name = 0;
            if (n + 3 > cap) {
                cap *= 2;
                d = (char *)as_xrealloc(d, cap);
            }
            if (gap && c != ' ' && n && d[n - 1] != ' ') d[n++] = ' ';
            d[n++] = (char)c;
        }
    }
    d[n] = 0;
    while (n && d[n - 1] == ' ') d[--n] = 0;
    {
        size_t lead = 0;
        while (d[lead] == ' ') lead++;
        if (lead) memmove(d, d + lead, n - lead + 1);
    }
    *text = d;
    return AS_OK;
}
