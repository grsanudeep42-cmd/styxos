#pragma once

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include "crypto.h"

void snapshot_init(void);
int  snapshot_save(void);
int  snapshot_restore(void);

/* Set the snapshot encryption key from the auth-derived master key.
 * Must be called after auth_preboot() succeeds, before snapshot_save(). */
void snapshot_set_key(const uint8_t *auth_derived_key);

/* Zero the snapshot key — called during self-destruct sequence. */
void snapshot_zero_key(void);
