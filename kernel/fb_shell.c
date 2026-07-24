/*
 * fb_shell.c — Framebuffer interactive shell renderer
 */
#include "fb_shell.h"
#include "fb.h"
#include "font.h"
#include "serial.h"
#include "string.h"
#include <stdint.h>
#include <stdarg.h>

/* ── Internal state ─────────────────────────────────────────────────────── */
static int    g_col    = 0;
static int    g_row    = 0;
static bool   g_cursor_visible = true;
static uint32_t g_cursor_ticks  = 0;

/* Scrollback: 24 lines × 80 chars */
static char   g_lines[FB_SHELL_ROWS][FB_SHELL_COLS + 1];
static int    g_line_top = 0;  /* which logical line is at screen row 0 */

/* ── Helpers ─────────────────────────────────────────────────────────────── */
static int row_to_y(int row) {
    return FB_SHELL_MARGIN_Y + row * FB_SHELL_LINE_H;
}
static int col_to_x(int col) {
    return FB_SHELL_MARGIN_X + col * FONT_WIDTH;
}

static void redraw_row(int row) {
    /* Clear row background */
    fb_draw_rect(FB_SHELL_MARGIN_X, row_to_y(row),
                 FB_SHELL_COLS * FONT_WIDTH, FB_SHELL_LINE_H, FB_SHELL_BG);
    /* Draw characters */
    for (int c = 0; c < FB_SHELL_COLS; c++) {
        char ch = g_lines[row][c];
        if (!ch) break;
        fb_draw_char(col_to_x(c), row_to_y(row), ch, FB_SHELL_FG);
    }
}

static void scroll_up(void) {
    /* Shift all lines up by one */
    for (int r = 0; r < FB_SHELL_ROWS - 1; r++) {
        memcpy(g_lines[r], g_lines[r + 1], FB_SHELL_COLS + 1);
    }
    memset(g_lines[FB_SHELL_ROWS - 1], 0, FB_SHELL_COLS + 1);
    g_row = FB_SHELL_ROWS - 1;

    /* Redraw all rows */
    fb_draw_rect(FB_SHELL_MARGIN_X, FB_SHELL_MARGIN_Y,
                 FB_SHELL_COLS * FONT_WIDTH, FB_SHELL_ROWS * FB_SHELL_LINE_H,
                 FB_SHELL_BG);
    for (int r = 0; r < FB_SHELL_ROWS; r++) {
        redraw_row(r);
    }
}

/* ── Public API ─────────────────────────────────────────────────────────── */
void fb_shell_init(void) {
    fb_clear(FB_SHELL_BG);
    memset(g_lines, 0, sizeof(g_lines));
    g_col = 0; g_row = 0;
    g_cursor_visible = true;
    g_cursor_ticks   = 0;

    /* Header bar */
    fb_draw_rect(0, 0, fb_get_width(), FB_SHELL_LINE_H - 2, 0x001E293BUL);
    fb_draw_string(FB_SHELL_MARGIN_X, 2,
                   "StyxOS Security Shell v2.0  |  where data goes to die",
                   FB_SHELL_PROMPT);
}

void fb_shell_putchar(char c) {
    if (c == '\n' || c == '\r') {
        fb_shell_newline();
        return;
    }
    if (c == '\b') {
        if (g_col > 0) {
            g_col--;
            g_lines[g_row][g_col] = 0;
            /* Erase character on screen */
            fb_draw_rect(col_to_x(g_col), row_to_y(g_row),
                         FONT_WIDTH, FB_SHELL_LINE_H, FB_SHELL_BG);
        }
        return;
    }
    if (c < 32) return;  /* ignore other control chars */

    if (g_col >= FB_SHELL_COLS) fb_shell_newline();

    g_lines[g_row][g_col] = c;
    fb_draw_char(col_to_x(g_col), row_to_y(g_row), c, FB_SHELL_FG);
    g_col++;
}

void fb_shell_puts(const char *s) {
    if (!s) return;
    while (*s) fb_shell_putchar(*s++);
}

/* Minimal printf subset: %s %d %x %c %% */
void fb_shell_printf(const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);

    char tmp[32];
    for (const char *p = fmt; *p; p++) {
        if (*p != '%') { fb_shell_putchar(*p); continue; }
        p++;
        switch (*p) {
        case 's': {
            const char *sv = va_arg(ap, const char *);
            fb_shell_puts(sv ? sv : "(null)");
            break;
        }
        case 'd': {
            int iv = va_arg(ap, int);
            if (iv < 0) { fb_shell_putchar('-'); iv = -iv; }
            int pos = 31; tmp[pos] = 0;
            if (iv == 0) { fb_shell_putchar('0'); break; }
            while (iv && pos > 0) { tmp[--pos] = '0' + iv % 10; iv /= 10; }
            fb_shell_puts(tmp + pos);
            break;
        }
        case 'x': {
            unsigned uv = va_arg(ap, unsigned);
            int pos = 31; tmp[pos] = 0;
            if (uv == 0) { fb_shell_puts("0"); break; }
            while (uv && pos > 0) {
                int nibble = uv & 0xF;
                tmp[--pos] = (char)(nibble < 10 ? '0' + nibble : 'a' + nibble - 10);
                uv >>= 4;
            }
            fb_shell_puts(tmp + pos);
            break;
        }
        case 'c':
            fb_shell_putchar((char)va_arg(ap, int));
            break;
        case '%':
            fb_shell_putchar('%');
            break;
        default:
            fb_shell_putchar('%');
            fb_shell_putchar(*p);
            break;
        }
    }
    va_end(ap);
}

void fb_shell_newline(void) {
    serial_printf("\n");  /* Mirror to serial */
    g_col = 0;
    g_row++;
    if (g_row >= FB_SHELL_ROWS) scroll_up();
    else memset(g_lines[g_row], 0, FB_SHELL_COLS + 1);
}

void fb_shell_clear(void) {
    memset(g_lines, 0, sizeof(g_lines));
    g_col = 0; g_row = 0;
    fb_draw_rect(FB_SHELL_MARGIN_X, FB_SHELL_MARGIN_Y,
                 FB_SHELL_COLS * FONT_WIDTH, FB_SHELL_ROWS * FB_SHELL_LINE_H,
                 FB_SHELL_BG);
}

void fb_shell_cursor_tick(void) {
    g_cursor_ticks++;
    /* Blink every 50 ticks (500ms at 100Hz) */
    if (g_cursor_ticks % 50 == 0) {
        g_cursor_visible = !g_cursor_visible;
        uint32_t colour = g_cursor_visible ? FB_SHELL_FG : FB_SHELL_BG;
        fb_draw_rect(col_to_x(g_col), row_to_y(g_row) + FB_SHELL_LINE_H - 3,
                     FONT_WIDTH, 2, colour);
    }
}
