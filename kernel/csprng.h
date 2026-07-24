#pragma once
#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>


/* Per-boot CSPRNG. Must call csprng_init() once before use.
 * Entropy sources: rdtsc, PIT counter, CPUID timing jitter, boot counter.
 * Output passes through a ChaCha20-based mixing step (set after chacha20.c). */

void    csprng_init(void);
void    csprng_get_bytes(uint8_t *out, size_t len);
uint32_t csprng_u32(void);
uint64_t csprng_u64(void);
