#pragma once

#include <stdint.h>

#define PTE_PRESENT  (1ULL << 0)
#define PTE_WRITABLE (1ULL << 1)
#define PTE_USER     (1ULL << 2)

/**
 * Initializes the virtual memory manager, sets up the higher-half kernel mapping,
 * maps the HHDM, maps the framebuffer and stack, and loads the new PML4 into CR3.
 */
void vmm_init(uint64_t hhdm_offset);

/**
 * Maps a given virtual address to a physical address with flags.
 */
void vmm_map_page(uint64_t virt_addr, uint64_t phys_addr, uint64_t flags);

/**
 * Unmaps the given virtual address and invalidates the TLB entry.
 */
void vmm_unmap_page(uint64_t virt_addr);

/**
 * Returns the pointer to the current active PML4 table (virtual address).
 */
uint64_t *vmm_get_pml4(void);
