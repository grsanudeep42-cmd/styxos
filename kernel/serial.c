#include "serial.h"
#include "io.h"
#include <stdarg.h>
#include <stdbool.h>

#define COM1_PORT 0x3F8

void serial_init(void) {
    outb(COM1_PORT + 1, 0x00);    // Disable all interrupts
    outb(COM1_PORT + 3, 0x80);    // Enable DLAB (set baud rate divisor)
    outb(COM1_PORT + 0, 0x03);    // Set divisor to 3 (lo byte) 38400 baud
    outb(COM1_PORT + 1, 0x00);    //                  (hi byte)
    outb(COM1_PORT + 3, 0x03);    // 8 bits, no parity, one stop bit
    outb(COM1_PORT + 2, 0xC7);    // Enable FIFO, clear them, with 14-byte threshold
    outb(COM1_PORT + 4, 0x0B);    // IRQs enabled, RTS/DSR set
}

static int is_transmit_empty(void) {
    return inb(COM1_PORT + 5) & 0x20;
}

void write_serial_char(char c) {
    while (is_transmit_empty() == 0);
    outb(COM1_PORT, c);
}

void write_serial_string(const char *s) {
    while (*s) {
        if (*s == '\n') {
            write_serial_char('\r');
        }
        write_serial_char(*s);
        s++;
    }
}

void serial_printf(const char *fmt, ...) {
    va_list args;
    va_start(args, fmt);

    while (*fmt) {
        if (*fmt == '%') {
            fmt++;
            if (*fmt == '\0') break;
            switch (*fmt) {
                case 's': {
                    const char *s = va_arg(args, const char *);
                    write_serial_string(s ? s : "(null)");
                    break;
                }
                case 'c': {
                    char c = (char)va_arg(args, int);
                    write_serial_char(c);
                    break;
                }
                case 'd': {
                    int64_t val = va_arg(args, int64_t);
                    if (val < 0) {
                        write_serial_char('-');
                        val = -val;
                    }
                    char buf[32];
                    int idx = 0;
                    do {
                        buf[idx++] = '0' + (val % 10);
                        val /= 10;
                    } while (val > 0);
                    for (int i = idx - 1; i >= 0; i--) {
                        write_serial_char(buf[i]);
                    }
                    break;
                }
                case 'x':
                case 'p': {
                    uint64_t val = va_arg(args, uint64_t);
                    char hex_chars[] = "0123456789ABCDEF";
                    write_serial_string("0x");
                    bool started = false;
                    for (int i = 60; i >= 0; i -= 4) {
                        uint8_t digit = (val >> i) & 0xF;
                        if (digit || started || i == 0) {
                            write_serial_char(hex_chars[digit]);
                            started = true;
                        }
                    }
                    break;
                }
                case '%':
                    write_serial_char('%');
                    break;
                default:
                    write_serial_char('%');
                    write_serial_char(*fmt);
                    break;
            }
        } else {
            if (*fmt == '\n') {
                write_serial_char('\r');
            }
            write_serial_char(*fmt);
        }
        fmt++;
    }
    va_end(args);
}
