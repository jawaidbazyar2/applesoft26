#ifndef AS_SCREEN_H
#define AS_SCREEN_H

#include <stdint.h>

enum {
    AS_DISP_TEXT = 0,
    AS_DISP_GR,
    AS_DISP_GR80,
    AS_DISP_HGR,
    AS_DISP_HGR2,
    AS_DISP_H320,
    AS_DISP_H640,
    AS_DISP_DHGR  /* & DHGR: 560x192 double hi-res, 16 colors */
};

typedef struct AsCell {
    uint8_t ch;
    uint8_t style; /* 0 normal, 1 inverse, 2 flash */
} AsCell;

typedef struct AsScreen {
    int disp;
    int mixed;
    int graphics_on;
    int hires;
    int page2;
    int gw, gh;
    uint8_t *pix;
    AsCell cell[24][40];
    int cur_x, cur_y;
    int wnd_left, wnd_width, wnd_top, wnd_btm;
    int style;
    int color;
    int hcolor;
    int hx, hy;
    int hvalid;
    int cursor_on;
    int flash_phase;
    int bell;
    double tone_hz; /* square wave for the run window; 0 is silent */
    int pdl[2];
    int btn0, btn1;
    int key_strobe;
} AsScreen;

AsScreen *as_screen_new(void);
void as_screen_free(AsScreen *s);
void as_screen_home(AsScreen *s);
void as_screen_putc(AsScreen *s, int ch);
void as_screen_puts(AsScreen *s, const char *z);
void as_screen_backspace(AsScreen *s);
void as_screen_comma_tab(AsScreen *s);
void as_screen_text_mode(AsScreen *s);
void as_screen_gr(AsScreen *s, int cols, int rows);
void as_screen_hgr(AsScreen *s, int which);
void as_screen_dhgr(AsScreen *s);
int as_screen_plot(AsScreen *s, int x, int y);
int as_screen_hlin(AsScreen *s, int x1, int x2, int y);
int as_screen_vlin(AsScreen *s, int y1, int y2, int x);
int as_screen_hplot(AsScreen *s, int x, int y);
int as_screen_hplot_to(AsScreen *s, int x, int y);
int as_screen_scrn(AsScreen *s, int x, int y, int *color);
void as_screen_soft(AsScreen *s, int addr);
int as_screen_poke(AsScreen *s, int addr, int value);
int as_screen_peek(AsScreen *s, int addr);
uint32_t as_lores_rgb(int index);
int as_screen_is_lores(const AsScreen *s);
int as_screen_is_hires(const AsScreen *s);

#endif
