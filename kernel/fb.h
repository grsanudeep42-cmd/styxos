#pragma once

#include <stdint.h>
#include <stddef.h>

/* Initialise the framebuffer module with the Limine-provided parameters. */
void fb_init(uint32_t *addr, uint32_t width, uint32_t height, uint32_t pitch);

/* Fill the entire framebuffer with colour (0x00RRGGBB). */
void fb_clear(uint32_t colour);

/* Write a single pixel. No bounds-checking – keep x/y in range. */
void fb_put_pixel(uint32_t x, uint32_t y, uint32_t colour);

/* Draw a NUL-terminated string at pixel position (x, y).
   Line spacing = FONT_HEIGHT + 2 px; '\n' is NOT interpreted here –
   call this once per line with the correct y offset. */
void fb_draw_string(int x, int y, const char *s, uint32_t colour);

uint32_t fb_get_width(void);
uint32_t fb_get_height(void);
void fb_draw_char(int x, int y, char c, uint32_t colour);
void fb_draw_rect(int x, int y, int w, int h, uint32_t colour);

