#include "as_internal.h"

int as_do_peek(AsMachine *m, int addr, double *out) {
    if (!m->screen) {
        *out = 0;
        return AS_OK;
    }
    *out = as_screen_peek(m->screen, addr);
    return AS_OK;
}

int as_do_poke(AsMachine *m, int addr, int value) {
    if (!m->screen) return AS_OK;
    return as_screen_poke(m->screen, addr, value);
}

int as_do_pdl(AsMachine *m, int n, double *out) {
    if (n < 0 || n > 255) {
        m->error = AS_ERR_ILLEGAL_QUANTITY;
        return AS_ERR_ILLEGAL_QUANTITY;
    }
    if (m->screen && n >= 0 && n <= 1) *out = m->screen->pdl[n];
    else *out = 0;
    return AS_OK;
}
