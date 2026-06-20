#pragma once

#include <stdint.h>
#include <stddef.h>
#include "limine.h"

/**
 * Initializes the physical memory manager using the Limine memory map
 * and the Higher Half Direct Map (HHDM) offset.
 */
void pmm_init(struct limine_memmap_response *memmap, uint64_t hhdm_offset);

/**
 * Allocates a 4KB page frame and returns its physical address.
 * Returns NULL (0) if no free frames remain.
 */
void *pmm_alloc_frame(void);

/**
 * Frees a page frame given its physical address.
 * Asserts/halts if the address is out of range or already free.
 */
void pmm_free_frame(void *phys_addr);

/**
 * Returns the total usable memory in bytes (sum of usable memory map entry lengths).
 */
uint64_t pmm_get_total_memory(void);

/**
 * Returns the amount of free memory currently available, in bytes.
 */
uint64_t pmm_get_free_memory(void);
