#ifndef AS_FONT_H
#define AS_FONT_H

#include <stdint.h>

/* 256 glyphs, 8 rows each. Bit 7 is unused; bit 6 is the leftmost visible pixel. */
extern const uint8_t as_font_rom[2048];

/* ascii is a 7-bit character. col is 0..6 left to right, row is 0..7 top to bottom. */
int as_glyph_on(int ascii, int row, int col);

#endif
