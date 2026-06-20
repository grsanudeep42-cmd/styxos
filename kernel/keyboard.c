#include "keyboard.h"
#include "irq.h"
#include "io.h"
#include "fb.h"
#include "font.h"
#include "serial.h"
#include <stdbool.h>

#define KEYBOARD_DATA_PORT 0x60

#define MARGIN_X 8
#define MARGIN_Y 8
#define LINE_H (FONT_HEIGHT + 2)

static int cursor_x = MARGIN_X;
static int cursor_y = MARGIN_Y + 4 * LINE_H; // Starts on line 4

static bool shift_pressed = false;

// Scan code translation tables for standard US keyboard layout set 1
static const char scancode_to_ascii_nomod[] = {
    0,  27, '1', '2', '3', '4', '5', '6', '7', '8', /* 9 */
  '9', '0', '-', '=', '\b', /* Backspace */
  '\t',                 /* Tab */
  'q', 'w', 'e', 'r',   /* 19 */
  't', 'y', 'u', 'i', 'o', 'p', '[', ']', '\n', /* Enter key */
    0,                  /* 29 - Control */
  'a', 's', 'd', 'f', 'g', 'h', 'j', 'k', 'l', ';', /* 39 */
 '\'', '`',   0,        /* Left shift */
 '\\', 'z', 'x', 'c', 'v', 'b', 'n', 'm', ',', '.', /* 49 */
  '/',   0,             /* Right shift */
  '*',
    0,  /* Alt */
  ' ',  /* Space bar */
    0,  /* Caps lock */
    0,  /* F1 key ... > */
    0,   0,   0,   0,   0,   0,   0,   0,
    0,  /* < ... F10 */
    0,  /* Num lock*/
    0,  /* Scroll Lock */
    0,  /* Home key */
    0,  /* Up Arrow */
    0,  /* Page Up */
  '-',
    0,  /* Left Arrow */
    0,
    0,  /* Right Arrow */
  '+',
    0,  /* End key*/
    0,  /* Down Arrow */
    0,  /* Page Down */
    0,  /* Insert Key */
    0,  /* Delete Key */
    0,   0,   0,
    0,  /* F11 Key */
    0,  /* F12 Key */
};

static const char scancode_to_ascii_shift[] = {
    0,  27, '!', '@', '#', '$', '%', '^', '&', '*', /* 9 */
  '(', ')', '_', '+', '\b', /* Backspace */
  '\t',                 /* Tab */
  'Q', 'W', 'E', 'R',   /* 19 */
  'T', 'Y', 'U', 'I', 'O', 'P', '{', '}', '\n', /* Enter key */
    0,                  /* 29 - Control */
  'A', 'S', 'D', 'F', 'G', 'H', 'J', 'K', 'L', ':', /* 39 */
  '"', '~',   0,        /* Left shift */
  '|', 'Z', 'X', 'C', 'V', 'B', 'N', 'M', '<', '>', /* 49 */
  '?',   0,             /* Right shift */
  '*',
    0,  /* Alt */
  ' ',  /* Space bar */
    0,  /* Caps lock */
    0,  /* F1 key ... > */
    0,   0,   0,   0,   0,   0,   0,   0,
    0,  /* < ... F10 */
    0,  /* Num lock*/
    0,  /* Scroll Lock */
    0,  /* Home key */
    0,  /* Up Arrow */
    0,  /* Page Up */
  '-',
    0,  /* Left Arrow */
    0,
    0,  /* Right Arrow */
  '+',
    0,  /* End key*/
    0,  /* Down Arrow */
    0,  /* Page Down */
    0,  /* Insert Key */
    0,  /* Delete Key */
    0,   0,   0,
    0,  /* F11 Key */
    0,  /* F12 Key */
};

static void keyboard_callback(struct registers *regs) {
    (void)regs;
    uint8_t scancode = inb(KEYBOARD_DATA_PORT);

    // Track shift key presses and releases
    // Left shift press: 0x2A, Left shift release: 0xAA
    // Right shift press: 0x36, Right shift release: 0xB6
    if (scancode == 0x2A || scancode == 0x36) {
        shift_pressed = true;
        return;
    } else if (scancode == 0xAA || scancode == 0xB6) {
        shift_pressed = false;
        return;
    }

    // Ignore key release codes (break codes are scancode >= 0x80)
    if (scancode & 0x80) {
        return;
    }

    // Map scancode to ASCII
    char ascii = 0;
    if (scancode < sizeof(scancode_to_ascii_nomod)) {
        ascii = shift_pressed ? scancode_to_ascii_shift[scancode] : scancode_to_ascii_nomod[scancode];
    }

    if (ascii != 0) {
        serial_printf("Keyboard: scancode=%x char=%c\n", (uint32_t)scancode, ascii);

        uint32_t fb_w = fb_get_width();
        uint32_t fb_h = fb_get_height();

        if (ascii == '\n') {
            cursor_y += LINE_H;
            cursor_x = MARGIN_X;
        } else if (ascii == '\b') {
            if (cursor_x > MARGIN_X) {
                cursor_x -= FONT_WIDTH;
                fb_draw_rect(cursor_x, cursor_y, FONT_WIDTH, FONT_HEIGHT, 0x00000000); // Black rect
            }
        } else {
            // Draw printable character
            fb_draw_char(cursor_x, cursor_y, ascii, 0x00FFFFFF); // White character
            cursor_x += FONT_WIDTH;

            // Handle horizontal screen wrapping
            if (cursor_x + FONT_WIDTH >= (int)fb_w) {
                cursor_x = MARGIN_X;
                cursor_y += LINE_H;
            }
        }

        // Handle vertical screen wrapping (clear typing space when reaching bottom)
        if (cursor_y + LINE_H >= (int)fb_h) {
            cursor_y = MARGIN_Y + 4 * LINE_H;
            fb_draw_rect(0, cursor_y, fb_w, fb_h - cursor_y, 0x00000000); // Clear lines 4+
        }
    }
}

void keyboard_init(void) {
    irq_install_handler(1, keyboard_callback);
    serial_printf("Keyboard Init: Handler registered on IRQ1\n");
}
