#pragma once

#include <stdint.h>
#include <stddef.h>

/*
 * heap.h — Kernel heap allocator (kmalloc / kfree)
 *
 * Virtual address range: HEAP_BASE .. HEAP_BASE + HEAP_MAX_SIZE
 *   0xffff800100000000  (well clear of HHDM / kernel text / stack /
 *                        framebuffer regions established by Milestone 4)
 *
 * Stage 1: bump allocator (eagerly mapped, no kfree)
 * Stage 2: first-fit free-list, forward coalescing, magic-number guard
 */

#define HEAP_BASE     0xffff800100000000ULL  /* start of kernel heap VA */
#define HEAP_MAX_SIZE (128ULL * 1024 * 1024) /* 128 MiB absolute ceiling */

/* ── Public API ─────────────────────────────────────────────────────────── */

/**
 * heap_init() — reserve and eagerly map the initial heap region,
 * install the first free block covering the whole region.
 * Must be called after vmm_init().
 */
void heap_init(void);

/**
 * kmalloc(size) — allocate at least 'size' bytes of kernel heap memory.
 * Returns a pointer aligned to HEAP_ALIGN bytes, or NULL on failure.
 */
void *kmalloc(size_t size);

/**
 * kfree(ptr) — release memory previously returned by kmalloc/kcalloc.
 * Logs a serial error and returns without touching memory if ptr does not
 * look like a valid live allocation (magic-number check).
 */
void kfree(void *ptr);

/**
 * kcalloc(n, sz) — kmalloc(n*sz) then zero the memory.
 * Returns NULL on overflow or allocation failure.
 */
void *kcalloc(size_t n, size_t sz);

/**
 * heap_dump() — print current heap free-list state to serial (for testing).
 */
void heap_dump(void);
