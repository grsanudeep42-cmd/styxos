#pragma once

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

bool manifest_verify_elf(const char *name, const uint8_t *data, size_t len);
