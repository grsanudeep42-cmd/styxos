#pragma once

#include <stdint.h>

void keyboard_init(void);
char keyboard_get_char(void);

extern volatile uint8_t g_last_scancode;

