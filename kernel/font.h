#pragma once

#include <stdint.h>
#include <stddef.h>

/* Each glyph is 8 wide x 8 tall pixels. */
#define FONT_WIDTH  8
#define FONT_HEIGHT 8

/* Returns the 8-byte glyph for an ASCII character (32–126).
   Returns a question-mark glyph for anything out of range. */
const uint8_t *font_glyph(unsigned char c);

/* Draw a single character at pixel position (x, y) in the given colour. */
void draw_char(uint32_t *fb, uint32_t pitch_px,
               int x, int y, unsigned char c, uint32_t colour);

/* Draw a NUL-terminated string, advancing x by FONT_WIDTH per glyph. */
void draw_string(uint32_t *fb, uint32_t pitch_px,
                 int x, int y, const char *s, uint32_t colour);
