#pragma once

#include <stdint.h>
#include <stdbool.h>

// Trigger distress beacon and self-destruct key destruction.
// Wipes memory keys and disk sectors (1-3) using a 3-pass secure overwrite.
// Halts system.
void destruct_trigger(const char *reason);
