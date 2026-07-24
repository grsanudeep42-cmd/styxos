#pragma once
#include <stdint.h>
#include <stdbool.h>

/* fb_shell.h — Framebuffer interactive shell renderer
 * Provides: cursor management, line editing, scrollback (16 lines),
 * and direct character output to framebuffer + serial. */

#define FB_SHELL_COLS    80
#define FB_SHELL_ROWS    24
#define FB_SHELL_MARGIN_X  8
#define FB_SHELL_MARGIN_Y  8
#define FB_SHELL_LINE_H   18    /* pixels per line */

/* Colours */
#define FB_SHELL_BG      0x000F172AUL   /* slate-900 */
#define FB_SHELL_FG      0x0034D399UL   /* neon green */
#define FB_SHELL_PROMPT  0x0038BDF8UL   /* cyan */
#define FB_SHELL_WARN    0x00F87171UL   /* red */

void fb_shell_init(void);
void fb_shell_putchar(char c);
void fb_shell_puts(const char *s);
void fb_shell_printf(const char *fmt, ...);  /* subset: %s %d %x %c */
void fb_shell_newline(void);
void fb_shell_clear(void);
void fb_shell_cursor_tick(void);             /* call from PIT handler */
