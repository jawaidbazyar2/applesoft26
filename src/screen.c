#include "as_screen.h"

#include "as_common.h"

#include <stdlib.h>
#include <string.h>

static const uint32_t k_lores[16] = {
    0x000000, 0x9A2155, 0x3C22A0, 0xC847E0, 0x0B7A30, 0x777777, 0x1B7FDB, 0x6EC4FF,
    0x8B4E00, 0xFF6A00, 0xBBBBBB, 0xFF7AD5, 0x1CC000, 0xFFE000, 0x4BE3C5, 0xFFFFFF
};

uint32_t as_lores_rgb(int index) {
    if (index < 0 || index > 15) index = 0;
    return k_lores[index];
}

int as_screen_is_lores(const AsScreen *s) {
    return s->disp == AS_DISP_GR || s->disp == AS_DISP_GR80;
}

int as_screen_is_hires(const AsScreen *s) {
    return s->disp == AS_DISP_HGR || s->disp == AS_DISP_HGR2 || s->disp == AS_DISP_H640;
}

static void clear_cells(AsScreen *s) {
    int y, x;
    for (y = 0; y < 24; y++) {
        for (x = 0; x < 40; x++) {
            s->cell[y][x].ch = ' ';
            s->cell[y][x].style = 0;
        }
    }
}

static int alloc_pix(AsScreen *s, int w, int h) {
    uint8_t *p = (uint8_t *)calloc((size_t)w * (size_t)h, 1);
    if (!p) return AS_ERR_OUT_OF_MEMORY;
    free(s->pix);
    s->pix = p;
    s->gw = w;
    s->gh = h;
    s->hvalid = 0;
    return AS_OK;
}

static void window_full(AsScreen *s) {
    s->wnd_left = 0;
    s->wnd_width = 40;
    s->wnd_top = 0;
    s->wnd_btm = 24;
}

static void window_mixed(AsScreen *s) {
    s->wnd_left = 0;
    s->wnd_width = 40;
    s->wnd_top = 20;
    s->wnd_btm = 24;
    s->cur_x = 0;
    s->cur_y = 20;
}

AsScreen *as_screen_new(void) {
    AsScreen *s = (AsScreen *)as_xmalloc(sizeof(AsScreen));
    s->key_strobe = -1;
    s->disp = AS_DISP_TEXT;
    window_full(s);
    clear_cells(s);
    return s;
}

void as_screen_free(AsScreen *s) {
    if (!s) return;
    free(s->pix);
    free(s);
}

static void scroll_window(AsScreen *s) {
    int y, x;
    int top = s->wnd_top;
    int bot = s->wnd_btm;
    int left = s->wnd_left;
    int right = s->wnd_left + s->wnd_width;
    if (bot > 24) bot = 24;
    if (right > 40) right = 40;
    if (top < 0) top = 0;
    for (y = top; y < bot - 1; y++) {
        for (x = left; x < right; x++) s->cell[y][x] = s->cell[y + 1][x];
    }
    if (bot - 1 >= 0 && bot - 1 < 24) {
        for (x = left; x < right; x++) {
            s->cell[bot - 1][x].ch = ' ';
            s->cell[bot - 1][x].style = 0;
        }
    }
}

static void cr(AsScreen *s) {
    s->cur_x = s->wnd_left;
    s->cur_y++;
    if (s->cur_y >= s->wnd_btm) {
        scroll_window(s);
        s->cur_y = s->wnd_btm - 1;
        if (s->cur_y < 0) s->cur_y = 0;
    }
}

/* Ctrl-K: blank from the cursor through the end of the text window. */
static void clear_to_window_end(AsScreen *s) {
    int y, x;
    int left = s->wnd_left;
    int right = s->wnd_left + s->wnd_width;
    int top = s->wnd_top;
    int bot = s->wnd_btm;
    if (left < 0) left = 0;
    if (right > 40) right = 40;
    if (top < 0) top = 0;
    if (bot > 24) bot = 24;
    for (y = top; y < bot; y++) {
        int x0;
        if (y < s->cur_y) continue;
        x0 = (y == s->cur_y) ? s->cur_x : left;
        if (x0 < left) x0 = left;
        for (x = x0; x < right; x++) {
            if (x < 0 || x > 39) continue;
            s->cell[y][x].ch = ' ';
            s->cell[y][x].style = 0;
        }
    }
}

void as_screen_putc(AsScreen *s, int ch) {
    if (ch == '\r' || ch == '\n') {
        cr(s);
        return;
    }
    if (ch == 8) {
        as_screen_backspace(s);
        return;
    }
    if (ch == 7) {
        s->bell++;
        return;
    }
    if (ch == 11) {
        clear_to_window_end(s);
        return;
    }
    if (ch < 32 || ch > 126) return;
    if (s->cur_y < 0) s->cur_y = 0;
    if (s->cur_y > 23) s->cur_y = 23;
    if (s->cur_x < 0) s->cur_x = 0;
    if (s->cur_x > 39) {
        cr(s);
    }
    if (s->cur_x >= 0 && s->cur_x < 40 && s->cur_y >= 0 && s->cur_y < 24) {
        s->cell[s->cur_y][s->cur_x].ch = (uint8_t)ch;
        s->cell[s->cur_y][s->cur_x].style = (uint8_t)s->style;
    }
    s->cur_x++;
    if (s->cur_x >= s->wnd_left + s->wnd_width || s->cur_x >= 40) cr(s);
}

void as_screen_puts(AsScreen *s, const char *z) {
    if (!z) return;
    while (*z) as_screen_putc(s, (unsigned char)*z++);
}

void as_screen_backspace(AsScreen *s) {
    if (s->cur_x > s->wnd_left) s->cur_x--;
}

void as_screen_comma_tab(AsScreen *s) {
    int ch = s->cur_x;
    if (ch >= 24) {
        cr(s);
        return;
    }
    s->cur_x = (ch + 16) & ~15;
    if (s->cur_x >= s->wnd_left + s->wnd_width) cr(s);
}

void as_screen_home(AsScreen *s) {
    int y, x;
    int right = s->wnd_left + s->wnd_width;
    if (right > 40) right = 40;
    for (y = s->wnd_top; y < s->wnd_btm && y < 24; y++) {
        for (x = s->wnd_left; x < right; x++) {
            s->cell[y][x].ch = ' ';
            s->cell[y][x].style = 0;
        }
    }
    s->cur_x = s->wnd_left;
    s->cur_y = s->wnd_top;
}

void as_screen_text_mode(AsScreen *s) {
    s->graphics_on = 0;
    s->mixed = 0;
    s->hires = 0;
    s->page2 = 0;
    s->disp = AS_DISP_TEXT;
    window_full(s);
    s->cur_x = 0;
    s->cur_y = 0;
}

void as_screen_gr(AsScreen *s, int cols, int rows) {
    s->graphics_on = 1;
    s->hires = 0;
    s->page2 = 0;
    s->mixed = 1;
    if (cols == 80 && rows == 80) {
        s->disp = AS_DISP_GR80;
        alloc_pix(s, 80, 80);
    } else {
        s->disp = AS_DISP_GR;
        alloc_pix(s, 40, 48);
    }
    window_mixed(s);
}

void as_screen_hgr(AsScreen *s, int which) {
    s->graphics_on = 1;
    s->hires = 1;
    if (which == 2) {
        s->disp = AS_DISP_H640;
        s->mixed = 1;
        s->page2 = 0;
        alloc_pix(s, 640, 400);
        window_mixed(s);
        return;
    }
    if (which == 1) {
        s->disp = AS_DISP_HGR2;
        s->mixed = 0;
        s->page2 = 1;
        alloc_pix(s, 280, 192);
        window_full(s);
        return;
    }
    s->disp = AS_DISP_HGR;
    s->mixed = 1;
    s->page2 = 0;
    alloc_pix(s, 280, 192);
    window_mixed(s);
}

static int in_pix(const AsScreen *s, int x, int y) {
    return s->pix && x >= 0 && y >= 0 && x < s->gw && y < s->gh;
}

int as_screen_plot(AsScreen *s, int x, int y) {
    if (!as_screen_is_lores(s) || !in_pix(s, x, y)) return AS_ERR_ILLEGAL_QUANTITY;
    if (s->color < 0 || s->color > 15) return AS_ERR_ILLEGAL_QUANTITY;
    s->pix[y * s->gw + x] = (uint8_t)s->color;
    return AS_OK;
}

int as_screen_hlin(AsScreen *s, int x1, int x2, int y) {
    int x;
    if (x1 > x2) {
        int t = x1;
        x1 = x2;
        x2 = t;
    }
    if (!as_screen_is_lores(s) || y < 0 || y >= s->gh || x1 < 0 || x2 >= s->gw)
        return AS_ERR_ILLEGAL_QUANTITY;
    for (x = x1; x <= x2; x++) s->pix[y * s->gw + x] = (uint8_t)s->color;
    return AS_OK;
}

int as_screen_vlin(AsScreen *s, int y1, int y2, int x) {
    int y;
    if (y1 > y2) {
        int t = y1;
        y1 = y2;
        y2 = t;
    }
    if (!as_screen_is_lores(s) || x < 0 || x >= s->gw || y1 < 0 || y2 >= s->gh)
        return AS_ERR_ILLEGAL_QUANTITY;
    for (y = y1; y <= y2; y++) s->pix[y * s->gw + x] = (uint8_t)s->color;
    return AS_OK;
}

static void stamp(AsScreen *s, int x, int y, int color) {
    if (in_pix(s, x, y)) s->pix[y * s->gw + x] = (uint8_t)color;
}

static void bresenham(AsScreen *s, int x0, int y0, int x1, int y1, int color) {
    int dx = x1 > x0 ? x1 - x0 : x0 - x1;
    int sx = x0 < x1 ? 1 : -1;
    int dy = y1 > y0 ? y0 - y1 : y1 - y0;
    int sy = y0 < y1 ? 1 : -1;
    int err = dx + dy;
    for (;;) {
        stamp(s, x0, y0, color);
        if (x0 == x1 && y0 == y1) break;
        {
            int e2 = 2 * err;
            if (e2 >= dy) {
                err += dy;
                x0 += sx;
            }
            if (e2 <= dx) {
                err += dx;
                y0 += sy;
            }
        }
    }
}

static int hcolor_ok(const AsScreen *s) {
    if (s->disp == AS_DISP_H640) return s->hcolor >= 0 && s->hcolor <= 15;
    return s->hcolor >= 0 && s->hcolor <= 7;
}

int as_screen_hplot(AsScreen *s, int x, int y) {
    if (!as_screen_is_hires(s) || !hcolor_ok(s) || !in_pix(s, x, y)) return AS_ERR_ILLEGAL_QUANTITY;
    stamp(s, x, y, s->hcolor);
    s->hx = x;
    s->hy = y;
    s->hvalid = 1;
    return AS_OK;
}

int as_screen_hplot_to(AsScreen *s, int x, int y) {
    if (!as_screen_is_hires(s) || !hcolor_ok(s) || !in_pix(s, x, y)) return AS_ERR_ILLEGAL_QUANTITY;
    if (!s->hvalid) {
        s->hx = 0;
        s->hy = 0;
    }
    bresenham(s, s->hx, s->hy, x, y, s->hcolor);
    s->hx = x;
    s->hy = y;
    s->hvalid = 1;
    return AS_OK;
}

int as_screen_scrn(AsScreen *s, int x, int y, int *color) {
    if (!s->pix || !in_pix(s, x, y)) return AS_ERR_ILLEGAL_QUANTITY;
    *color = s->pix[y * s->gw + x];
    return AS_OK;
}

static void resolve_soft(AsScreen *s) {
    if (!s->graphics_on) {
        s->disp = AS_DISP_TEXT;
        s->mixed = 0;
        return;
    }
    if (s->disp == AS_DISP_GR80 || s->disp == AS_DISP_H640) return;
    if (s->hires) {
        if (s->page2 && !s->mixed) s->disp = AS_DISP_HGR2;
        else {
            s->disp = AS_DISP_HGR;
            if (s->gw != 280 || s->gh != 192) alloc_pix(s, 280, 192);
        }
    } else {
        s->disp = AS_DISP_GR;
        if (s->gw != 40 || s->gh != 48) alloc_pix(s, 40, 48);
    }
}

void as_screen_soft(AsScreen *s, int addr) {
    switch (addr) {
    case 0xC050: s->graphics_on = 1; break;
    case 0xC051:
        s->graphics_on = 0;
        s->disp = AS_DISP_TEXT;
        break;
    case 0xC052: s->mixed = 0; break;
    case 0xC053: s->mixed = 1; break;
    case 0xC054: s->page2 = 0; break;
    case 0xC055: s->page2 = 1; break;
    case 0xC056: s->hires = 0; break;
    case 0xC057: s->hires = 1; break;
    default: return;
    }
    resolve_soft(s);
}

int as_screen_poke(AsScreen *s, int addr, int value) {
    if (value < 0 || value > 255) return AS_ERR_ILLEGAL_QUANTITY;
    if (addr >= 0xC050 && addr <= 0xC057) {
        as_screen_soft(s, addr);
        return AS_OK;
    }
    if (addr == 0xC010 || addr == 49168) {
        if (s->key_strobe > 0) s->key_strobe &= 0x7f;
        return AS_OK;
    }
    switch (addr) {
    case 32:
        s->wnd_left = value > 39 ? 39 : value;
        break;
    case 33:
        s->wnd_width = value < 1 ? 1 : value;
        if (s->wnd_left + s->wnd_width > 40) s->wnd_width = 40 - s->wnd_left;
        break;
    case 34:
        s->wnd_top = value > 23 ? 23 : value;
        if (s->wnd_btm <= s->wnd_top) s->wnd_btm = s->wnd_top + 1;
        break;
    case 35:
        s->wnd_btm = value > 24 ? 24 : value;
        if (s->wnd_btm <= s->wnd_top) s->wnd_btm = s->wnd_top + 1;
        break;
    case 36: s->cur_x = value; break;
    case 37: s->cur_y = value > 23 ? 23 : value; break;
    case 50:
        if (value == 0x3F) s->style = 1;
        else if (value == 0x7F) s->style = 2;
        else s->style = 0;
        break;
    default: break;
    }
    return AS_OK;
}

int as_screen_peek(AsScreen *s, int addr) {
    if (addr >= 0xC050 && addr <= 0xC057) {
        as_screen_soft(s, addr);
        return 0;
    }
    if (addr == 0xC000 || addr == 49152) return s->key_strobe < 0 ? 0 : s->key_strobe;
    if (addr == 0xC010 || addr == 49168) {
        int v = s->key_strobe < 0 ? 0 : (s->key_strobe & 0x7f);
        if (s->key_strobe > 0) s->key_strobe &= 0x7f;
        return v;
    }
    if (addr == 49249 || addr == 0xC061) return s->btn0 ? 128 : 0;
    if (addr == 49250 || addr == 0xC062) return s->btn1 ? 128 : 0;
    switch (addr) {
    case 32: return s->wnd_left;
    case 33: return s->wnd_width;
    case 34: return s->wnd_top;
    case 35: return s->wnd_btm;
    case 36: return s->cur_x;
    case 37: return s->cur_y;
    case 50:
        if (s->style == 1) return 0x3F;
        if (s->style == 2) return 0x7F;
        return 0xFF;
    default: return 0;
    }
}
