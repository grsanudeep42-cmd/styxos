#pragma once

#include <stdint.h>

void serial_init(void);
void write_serial_char(char c);
void write_serial_string(const char *s);
void serial_printf(const char *fmt, ...);
