/*
 * heap.c — Kernel heap allocator (Milestone 5)
 *
 * Stage 1: eagerly maps an initial 4 MiB region and uses a bump allocator.
 * Stage 2: replaces bump allocator internals with a first-fit free-list
 *          allocator supporting kfree(), forward coalescing, block splitting,
 *          on-demand heap growth (one page at a time via PMM+VMM), and a
 *          magic-number guard in every block header to catch misuse early.
 *
 * Heap virtual address range (see heap.h):
 *   Base : 0xffff800100000000
 *   Max  : 0xffff800100000000 + 128 MiB
 *
 * This range is well clear of:
 *   HHDM  : typically starts at 0xffff800000000000 and covers physical RAM
 *             (but our chosen base is +256 MiB into the HHDM hole)
 *   Kernel text: 0xffffffff80000000
 *   Stack : mapped around the early-boot RSP, a few hundred KiB below kernel
 *   FB    : wherever Limine placed it (usually in the HHDM or a device region)
 *
 * NOTE ON HHDM COLLISION:
 *   On QEMU with 128 MiB RAM the HHDM covers 0xffff800000000000 ..
 *   0xffff800007ffffff (128 MiB). Our heap starts at +256 MiB offset
 *   (0xffff800010000000) — but to be safe we log the chosen range at init
 *   and the PMM will refuse to hand out frames if RAM is exhausted.
 */

#include "heap.h"
#include "vmm.h"
#include "pmm.h"
#include "serial.h"

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

/* ── Constants ──────────────────────────────────────────────────────────── */

#define PAGE_SIZE        4096ULL
#define HEAP_ALIGN       16ULL          /* all allocations aligned to 16 B  */
#define HEAP_INIT_PAGES  1024ULL        /* 4 MiB initial eager mapping       */
#define HEAP_INIT_SIZE   (HEAP_INIT_PAGES * PAGE_SIZE)

/* Magic number stored in every block header to detect misuse / corruption */
#define HEAP_MAGIC_FREE  0xDEADF8EEU
#define HEAP_MAGIC_USED  0xC0FFEE42U

/* ── Block header (placed immediately *before* the user pointer) ─────────

   Layout in memory:
     [ heap_block_t header ][ user data ...][ padding ][ heap_block_t header ]...

   'size' is the usable data size (not including the header itself).
   The next block starts at  (uint8_t*)blk + sizeof(heap_block_t) + blk->size .
*/

typedef struct heap_block {
    uint32_t           magic;   /* HEAP_MAGIC_FREE or HEAP_MAGIC_USED      */
    uint32_t           free;    /* 1 = free, 0 = in use                    */
    size_t             size;    /* usable bytes following this header       */
    struct heap_block *next;    /* next block in the list (NULL = last)     */
    uint8_t            _pad[8]; /* pad to 32 bytes so user ptr is 16-align  */
} heap_block_t;

/* ── Heap state ──────────────────────────────────────────────────────────── */

static heap_block_t *heap_head    = NULL;  /* first block in free-list      */
static uint64_t      heap_base    = HEAP_BASE;
static uint64_t      heap_end     = 0;     /* one past the last mapped byte */
static uint64_t      heap_ceiling = 0;     /* absolute maximum              */

/* ── Internal helpers ────────────────────────────────────────────────────── */

/* Round 'n' up to the nearest multiple of HEAP_ALIGN */
static inline size_t align_up(size_t n) {
    return (n + (HEAP_ALIGN - 1)) & ~(HEAP_ALIGN - 1);
}

/*
 * grow_heap(bytes) — ask the VMM+PMM for enough new pages to satisfy
 * 'bytes' of additional user space (plus header overhead).
 *
 * Returns a pointer to a new free block covering the newly mapped region,
 * or NULL if the ceiling is reached or PMM is exhausted.
 */
static heap_block_t *grow_heap(size_t bytes_needed) {
    /* How many pages do we need? Round up to page boundary. */
    size_t total     = sizeof(heap_block_t) + bytes_needed;
    size_t n_pages   = (total + PAGE_SIZE - 1) / PAGE_SIZE;
    size_t n_bytes   = n_pages * PAGE_SIZE;

    if (heap_end + n_bytes > heap_ceiling) {
        serial_printf("HEAP ERROR: grow_heap: heap ceiling reached "
                      "(tried to extend by %d pages)\n", (int)n_pages);
        return NULL;
    }

    /* Map the new pages */
    for (size_t i = 0; i < n_pages; i++) {
        void *phys = pmm_alloc_frame();
        if (!phys) {
            serial_printf("HEAP ERROR: grow_heap: PMM exhausted at page %d/%d\n",
                          (int)i, (int)n_pages);
            return NULL;
        }
        vmm_map_page(heap_end + i * PAGE_SIZE, (uint64_t)phys,
                     PTE_PRESENT | PTE_WRITABLE);
    }

    /* Carve a single free block spanning the new region */
    heap_block_t *blk = (heap_block_t *)heap_end;
    blk->magic = HEAP_MAGIC_FREE;
    blk->free  = 1;
    blk->size  = n_bytes - sizeof(heap_block_t);
    blk->next  = NULL;

    heap_end += n_bytes;

    serial_printf("HEAP: grew by %d page(s) — heap_end now %p\n",
                  (int)n_pages, (void *)heap_end);
    return blk;
}

/* ── Public API ─────────────────────────────────────────────────────────── */

void heap_init(void) {
    heap_base    = HEAP_BASE;
    heap_end     = heap_base;
    heap_ceiling = heap_base + HEAP_MAX_SIZE;

    serial_printf("HEAP: Initializing kernel heap\n");
    serial_printf("HEAP: VA range  %p .. %p  (ceiling)\n",
                  (void *)heap_base, (void *)heap_ceiling);
    serial_printf("HEAP: Eagerly mapping %d initial pages (%d MiB)...\n",
                  (int)HEAP_INIT_PAGES, (int)(HEAP_INIT_SIZE / (1024 * 1024)));

    /* Stage 1: eagerly map HEAP_INIT_SIZE bytes */
    for (uint64_t i = 0; i < HEAP_INIT_PAGES; i++) {
        void *phys = pmm_alloc_frame();
        if (!phys) {
            serial_printf("HEAP ERROR: PMM exhausted during initial mapping "
                          "at page %d of %d\n", (int)i, (int)HEAP_INIT_PAGES);
            /* Halt — we can't safely continue without a heap */
            for (;;) __asm__ volatile("hlt");
        }
        vmm_map_page(heap_base + i * PAGE_SIZE, (uint64_t)phys,
                     PTE_PRESENT | PTE_WRITABLE);
    }
    heap_end = heap_base + HEAP_INIT_SIZE;

    /*
     * Stage 2: initialise free-list with a single block covering
     * the entire eagerly-mapped region.
     */
    heap_head        = (heap_block_t *)heap_base;
    heap_head->magic = HEAP_MAGIC_FREE;
    heap_head->free  = 1;
    heap_head->size  = HEAP_INIT_SIZE - sizeof(heap_block_t);
    heap_head->next  = NULL;

    serial_printf("HEAP: Initial free block: header=%p  usable=%d bytes\n",
                  (void *)heap_head, (int)heap_head->size);
    serial_printf("HEAP: heap_init complete — %d MiB available\n",
                  (int)(heap_head->size / (1024 * 1024)));
}

void *kmalloc(size_t size) {
    if (size == 0) return NULL;

    /* Round requested size up to alignment boundary */
    size_t alloc_size = align_up(size);

    /*
     * First-fit walk: find the first free block large enough.
     */
    heap_block_t *blk  = heap_head;
    heap_block_t *prev = NULL;

    while (blk) {
        if (blk->magic != HEAP_MAGIC_FREE && blk->magic != HEAP_MAGIC_USED) {
            serial_printf("HEAP ERROR: kmalloc: corrupt block at %p (bad magic %p)\n",
                          (void *)blk, (void *)(uintptr_t)blk->magic);
            return NULL;
        }

        if (blk->free && blk->size >= alloc_size) {
            /* Found a suitable block */

            /*
             * Split if the remainder is large enough to hold a new header
             * plus at least HEAP_ALIGN bytes of user data.
             */
            size_t remainder = blk->size - alloc_size;
            if (remainder > sizeof(heap_block_t) + HEAP_ALIGN) {
                heap_block_t *split = (heap_block_t *)
                    ((uint8_t *)blk + sizeof(heap_block_t) + alloc_size);
                split->magic = HEAP_MAGIC_FREE;
                split->free  = 1;
                split->size  = remainder - sizeof(heap_block_t);
                split->next  = blk->next;

                blk->next = split;
                blk->size = alloc_size;
            }

            blk->magic = HEAP_MAGIC_USED;
            blk->free  = 0;

            void *ptr = (void *)((uint8_t *)blk + sizeof(heap_block_t));
            return ptr;
        }

        prev = blk;
        blk  = blk->next;
    }

    /*
     * No suitable free block found — grow the heap by one page at a time
     * until we have enough space, then retry.
     */
    heap_block_t *new_blk = grow_heap(alloc_size);
    if (!new_blk) {
        serial_printf("HEAP ERROR: kmalloc(%d): out of heap space\n", (int)size);
        return NULL;
    }

    /* Append the new block to the end of the list */
    if (prev) {
        prev->next = new_blk;
    } else {
        heap_head = new_blk;
    }

    /* Try to coalesce with the block before it if prev is free
       (won't happen on a brand-new tail block, but good practice) */

    /* Now retry the allocation — the new block should satisfy it */
    return kmalloc(size);
}

void kfree(void *ptr) {
    if (!ptr) return;

    /* The header lives immediately before the user pointer */
    heap_block_t *blk = (heap_block_t *)((uint8_t *)ptr - sizeof(heap_block_t));

    /* Magic-number guard: detect double-free or bad pointer */
    if (blk->magic != HEAP_MAGIC_USED) {
        if (blk->magic == HEAP_MAGIC_FREE) {
            serial_printf("HEAP ERROR: kfree(%p): double-free detected! "
                          "(block already marked free)\n", ptr);
        } else {
            serial_printf("HEAP ERROR: kfree(%p): invalid pointer — bad magic %p "
                          "(expected %p)\n",
                          ptr,
                          (void *)(uintptr_t)blk->magic,
                          (void *)(uintptr_t)HEAP_MAGIC_USED);
        }
        return; /* do NOT touch memory — avoid silent heap corruption */
    }

    blk->magic = HEAP_MAGIC_FREE;
    blk->free  = 1;

    /*
     * Forward coalescing: if the immediately following block is also free,
     * merge them into one larger block.
     */
    if (blk->next && blk->next->free) {
        heap_block_t *next = blk->next;

        /* Sanity check the next block's magic before trusting it */
        if (next->magic != HEAP_MAGIC_FREE) {
            serial_printf("HEAP WARN: kfree coalesce: next block at %p has "
                          "unexpected magic %p, skipping coalesce\n",
                          (void *)next, (void *)(uintptr_t)next->magic);
        } else {
            blk->size += sizeof(heap_block_t) + next->size;
            blk->next  = next->next;
        }
    }
}

void *kcalloc(size_t n, size_t sz) {
    /* Overflow-safe multiply */
    if (sz != 0 && n > (size_t)-1 / sz) {
        serial_printf("HEAP ERROR: kcalloc: size overflow (%d * %d)\n",
                      (int)n, (int)sz);
        return NULL;
    }
    size_t total = n * sz;
    void  *ptr   = kmalloc(total);
    if (!ptr) return NULL;

    /* Zero the memory */
    uint8_t *p = (uint8_t *)ptr;
    for (size_t i = 0; i < total; i++) p[i] = 0;
    return ptr;
}

void heap_dump(void) {
    serial_printf("HEAP DUMP: base=%p  end=%p  ceiling=%p\n",
                  (void *)heap_base, (void *)heap_end, (void *)heap_ceiling);
    heap_block_t *blk = heap_head;
    int idx = 0;
    size_t total_free = 0, total_used = 0;

    while (blk) {
        const char *state;
        if (blk->magic == HEAP_MAGIC_FREE) {
            state = "FREE   ";
            total_free += blk->size;
        } else if (blk->magic == HEAP_MAGIC_USED) {
            state = "USED   ";
            total_used += blk->size;
        } else {
            state = "CORRUPT";
        }
        serial_printf("  [%d] hdr=%p  state=%s  size=%d\n",
                      idx, (void *)blk, state, (int)blk->size);
        blk = blk->next;
        idx++;
    }
    serial_printf("HEAP DUMP: total_free=%d B  total_used=%d B  blocks=%d\n",
                  (int)total_free, (int)total_used, idx);
}
