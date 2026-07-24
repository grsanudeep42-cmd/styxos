#pragma once

#include <stdint.h>

void keyboard_init(void);

extern volatile uint8_t g_last_scancode;
