#include "pmm.h"
#include "serial.h"
#include "string.h"
#include <stdbool.h>

#define PAGE_SIZE 4096

static uint8_t *bitmap = NULL;
static uint64_t total_frames = 0;
static uint64_t bitmap_size = 0;
static uint64_t bitmap_phys_addr = 0;

static uint64_t total_usable_memory = 0;
static uint64_t free_memory = 0;

static void pmm_panic(const char *msg) {
    serial_printf("\n!!! PMM KERNEL PANIC !!!\n");
    serial_printf("Reason: %s\n", msg);
    // Halt the CPU
    __asm__ volatile ("cli");
    for (;;) __asm__ volatile ("hlt");
}

void pmm_init(struct limine_memmap_response *memmap, uint64_t hhdm_offset) {
    uint64_t max_usable_addr = 0;
    total_usable_memory = 0;
    
    // 1. Walk the memory map to compute total usable memory and max usable physical address
    for (uint64_t i = 0; i < memmap->entry_count; i++) {
        struct limine_memmap_entry *entry = memmap->entries[i];
        
        serial_printf("PMM: Memmap entry %d: base=%x, len=%x, type=%d\n",
                      (int)i, (uint64_t)entry->base, (uint64_t)entry->length, (int)entry->type);
                      
        if (entry->type == LIMINE_MEMMAP_USABLE) {
            total_usable_memory += entry->length;
            uint64_t end = entry->base + entry->length;
            if (end > max_usable_addr) {
                max_usable_addr = end;
            }
        }
    }
    
    if (max_usable_addr == 0) {
        pmm_panic("No usable memory found in Limine memmap!");
    }
    
    // 2. Determine bitmap size (1 bit per 4KB frame)
    total_frames = max_usable_addr / PAGE_SIZE;
    bitmap_size = (total_frames + 7) / 8;
    
    serial_printf("PMM: Total frames tracked: %d, Bitmap size: %d bytes\n",
                  (int)total_frames, (int)bitmap_size);
                  
    // 3. Find a free spot in USABLE memmap entries large enough to hold the bitmap
    bool bitmap_placed = false;
    for (uint64_t i = 0; i < memmap->entry_count; i++) {
        struct limine_memmap_entry *entry = memmap->entries[i];
        if (entry->type == LIMINE_MEMMAP_USABLE) {
            // Align the base address up to a page boundary
            uint64_t aligned_base = (entry->base + PAGE_SIZE - 1) & ~(PAGE_SIZE - 1);
            if (entry->base + entry->length >= aligned_base &&
                (entry->base + entry->length - aligned_base) >= bitmap_size) {
                bitmap_phys_addr = aligned_base;
                bitmap_placed = true;
                break;
            }
        }
    }
    
    if (!bitmap_placed) {
        pmm_panic("Could not find a USABLE memmap entry large enough to hold the PMM bitmap!");
    }
    
    // Map the bitmap virtual address using HHDM
    bitmap = (uint8_t *)(bitmap_phys_addr + hhdm_offset);
    serial_printf("PMM: Bitmap placed at physical=%p (virtual=%p)\n",
                  (void*)bitmap_phys_addr, (void*)bitmap);
                  
    // 4. Initialize the bitmap:
    // Mark all frames as used/reserved by default (0xFF)
    memset(bitmap, 0xFF, bitmap_size);
    
    // For each USABLE memory region, mark its frames as free (0)
    for (uint64_t i = 0; i < memmap->entry_count; i++) {
        struct limine_memmap_entry *entry = memmap->entries[i];
        if (entry->type == LIMINE_MEMMAP_USABLE) {
            uint64_t start_frame = (entry->base + PAGE_SIZE - 1) / PAGE_SIZE;
            uint64_t end_frame = (entry->base + entry->length) / PAGE_SIZE;
            
            for (uint64_t f = start_frame; f < end_frame; f++) {
                if (f < total_frames) {
                    bitmap[f / 8] &= ~(1 << (f % 8));
                }
            }
        }
    }
    
    // Mark the bitmap's own pages as used in its own bitmap
    uint64_t bitmap_start_frame = bitmap_phys_addr / PAGE_SIZE;
    uint64_t bitmap_end_frame = (bitmap_phys_addr + bitmap_size + PAGE_SIZE - 1) / PAGE_SIZE;
    for (uint64_t f = bitmap_start_frame; f < bitmap_end_frame; f++) {
        if (f < total_frames) {
            bitmap[f / 8] |= (1 << (f % 8));
        }
    }
    
    // CRITICAL: Mark physical page 0 as used (prevents allocating physical address 0x00000000)
    bitmap[0] |= 1;
    
    // 5. Count free pages to set the initial free memory
    uint64_t free_frames = 0;
    for (uint64_t f = 0; f < total_frames; f++) {
        if ((bitmap[f / 8] & (1 << (f % 8))) == 0) {
            free_frames++;
        }
    }
    free_memory = free_frames * PAGE_SIZE;
    
    serial_printf("PMM: Initialization complete.\n");
    serial_printf("  - Total Usable Memory: %d MB\n", (int)(total_usable_memory / (1024 * 1024)));
    serial_printf("  - Free Memory: %d MB (%d frames)\n", (int)(free_memory / (1024 * 1024)), (int)free_frames);
}

void *pmm_alloc_frame(void) {
    for (uint64_t i = 0; i < bitmap_size; i++) {
        if (bitmap[i] != 0xFF) {
            for (int bit = 0; bit < 8; bit++) {
                uint64_t f = i * 8 + bit;
                if (f >= total_frames) {
                    return NULL;
                }
                if ((bitmap[i] & (1 << bit)) == 0) {
                    bitmap[i] |= (1 << bit);
                    if (free_memory >= PAGE_SIZE) {
                        free_memory -= PAGE_SIZE;
                    } else {
                        free_memory = 0;
                    }
                    return (void *)(f * PAGE_SIZE);
                }
            }
        }
    }
    return NULL;
}

void pmm_free_frame(void *phys_addr) {
    uintptr_t addr = (uintptr_t)phys_addr;
    
    if ((addr % PAGE_SIZE) != 0) {
        pmm_panic("pmm_free_frame called with unaligned address!");
    }
    
    uint64_t f = addr / PAGE_SIZE;
    
    if (f >= total_frames) {
        pmm_panic("pmm_free_frame called with address out of tracked range!");
    }
    
    uint64_t byte_idx = f / 8;
    uint8_t bit_idx = f % 8;
    
    if ((bitmap[byte_idx] & (1 << bit_idx)) == 0) {
        pmm_panic("pmm_free_frame called on an already-free page frame!");
    }
    
    bitmap[byte_idx] &= ~(1 << bit_idx);
    free_memory += PAGE_SIZE;
}

uint64_t pmm_get_total_memory(void) {
    return total_usable_memory;
}

uint64_t pmm_get_free_memory(void) {
    return free_memory;
}
