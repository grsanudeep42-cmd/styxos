#pragma once

#include <stdint.h>
#include <stddef.h>

void snapshot_init(void);
int snapshot_save(void);
int snapshot_restore(void);
