#pragma once
#include <stdint.h>
#include <stdbool.h>

/* USB-backed tamper counter. Stores attempt count in USB sector 5,
 * protected by HMAC-SHA512 under a device-unique key.
 * Survives power-off — enforces lockout across reboots. */

#define AUTH_COUNTER_LBA      5      /* USB sector for tamper counter */
#define AUTH_COUNTER_MAX      3      /* Max attempts before destruction */

void    auth_counter_init(void);         /* Load counter from USB */
int     auth_counter_get(void);          /* Current attempt count */
void    auth_counter_increment(void);    /* Record a failed attempt */
void    auth_counter_reset(void);        /* Reset on successful auth */
bool    auth_counter_locked(void);       /* True if attempts >= MAX */
