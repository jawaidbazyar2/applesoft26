#include "as_tokenize.h"

#include "as_token.h"

#include <ctype.h>
#include <stdlib.h>
#include <string.h>

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
                if (token == TK_DATA) data = 1;
                if (token == TK_REM) {
                    while (i < n) {
                        buf_add(&buf, (uint8_t)src[i], (uint32_t)i, 1);
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

int as_detokenize(const uint8_t *bytes, size_t len, char **text) {
    size_t cap = 64, n = 0, i;
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
        } else {
            if (n + 2 > cap) {
                cap *= 2;
                d = (char *)as_xrealloc(d, cap);
            }
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
