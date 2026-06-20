/*
 * fb.c – framebuffer helpers.
 *
 * Limine hands us a linear pixel framebuffer (NOT VGA text mode).
 * Pixels are 32-bit 0x00RRGGBB (BGR or RGB depends on bpp/model field;
 * we assume the common 32-bpp RGB model QEMU exposes).
 */

#include "fb.h"
#include "font.h"

static uint32_t *fb_addr;
static uint32_t  fb_width;
static uint32_t  fb_height;
static uint32_t  fb_pitch_px;   /* pitch in pixels (bytes_per_line / 4) */

void fb_init(uint32_t *addr, uint32_t width, uint32_t height, uint32_t pitch) {
    fb_addr     = addr;
    fb_width    = width;
    fb_height   = height;
    fb_pitch_px = pitch / 4;   /* Limine gives pitch in bytes; we work in px */
}

void fb_clear(uint32_t colour) {
    for (uint32_t y = 0; y < fb_height; y++)
        for (uint32_t x = 0; x < fb_width; x++)
            fb_addr[y * fb_pitch_px + x] = colour;
}

void fb_put_pixel(uint32_t x, uint32_t y, uint32_t colour) {
    fb_addr[y * fb_pitch_px + x] = colour;
}

void fb_draw_string(int x, int y, const char *s, uint32_t colour) {
    int cx = x;
    while (*s) {
        draw_char(fb_addr, fb_pitch_px, cx, y, (unsigned char)*s, colour);
        cx += FONT_WIDTH;
        s++;
    }
}

uint32_t fb_get_width(void) {
    return fb_width;
}

uint32_t fb_get_height(void) {
    return fb_height;
}

void fb_draw_char(int x, int y, char c, uint32_t colour) {
    if ((uint32_t)x < fb_width && (uint32_t)y < fb_height) {
        draw_char(fb_addr, fb_pitch_px, x, y, (unsigned char)c, colour);
    }
}

void fb_draw_rect(int x, int y, int w, int h, uint32_t colour) {
    for (int dy = 0; dy < h; dy++) {
        for (int dx = 0; dx < w; dx++) {
            uint32_t px = x + dx;
            uint32_t py = y + dy;
            if (px < fb_width && py < fb_height) {
                fb_addr[py * fb_pitch_px + px] = colour;
            }
        }
    }
}

