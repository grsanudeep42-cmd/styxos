#include "vmm.h"
#include "pmm.h"
#include "serial.h"
#include "string.h"
#include "limine.h"
#include <stddef.h>
#include <stdbool.h>

#define PAGE_SIZE 4096
#define PTE_ADDR_MASK 0x000FFFFFFFFFF000ULL

#define PML4_INDEX(virt) (((virt) >> 39) & 0x1FF)
#define PDPT_INDEX(virt) (((virt) >> 30) & 0x1FF)
#define PD_INDEX(virt)   (((virt) >> 21) & 0x1FF)
#define PT_INDEX(virt)   (((virt) >> 12) & 0x1FF)

#ifndef LIMINE_MEMMAP_KERNEL_AND_MODULES
#define LIMINE_MEMMAP_KERNEL_AND_MODULES 6
#endif

#ifndef LIMINE_MEMMAP_FRAMEBUFFER
#define LIMINE_MEMMAP_FRAMEBUFFER 7
#endif

static uint64_t hhdm_off = 0;
static uint64_t *current_pml4 = NULL;

/* ── Limine request references (defined once in main.c) ─────────────────── */

extern volatile struct limine_memmap_request          memmap_request;
extern volatile struct limine_kernel_address_request  kernel_address_request;
extern volatile struct limine_framebuffer_request     framebuffer_request;

/* ── Helper to translate address using Limine's page table ──────────────── */

static uint64_t translate_limine_address(uint64_t pml4_phys, uint64_t virt_addr) {
    uint64_t *pml4 = (uint64_t *)(pml4_phys + hhdm_off);
    uint64_t pml4_i = PML4_INDEX(virt_addr);
    if (!(pml4[pml4_i] & PTE_PRESENT)) return 0;
    
    uint64_t *pdpt = (uint64_t *)((pml4[pml4_i] & PTE_ADDR_MASK) + hhdm_off);
    uint64_t pdpt_i = PDPT_INDEX(virt_addr);
    if (!(pdpt[pdpt_i] & PTE_PRESENT)) return 0;
    
    if (pdpt[pdpt_i] & (1ULL << 7)) { // 1GB page
        return (pdpt[pdpt_i] & PTE_ADDR_MASK) + (virt_addr & 0x3FFFFFFF);
    }
    
    uint64_t *pd = (uint64_t *)((pdpt[pdpt_i] & PTE_ADDR_MASK) + hhdm_off);
    uint64_t pd_i = PD_INDEX(virt_addr);
    if (!(pd[pd_i] & PTE_PRESENT)) return 0;
    
    if (pd[pd_i] & (1ULL << 7)) { // 2MB page
        return (pd[pd_i] & PTE_ADDR_MASK) + (virt_addr & 0x1FFFFF);
    }
    
    uint64_t *pt = (uint64_t *)((pd[pd_i] & PTE_ADDR_MASK) + hhdm_off);
    uint64_t pt_i = PT_INDEX(virt_addr);
    if (!(pt[pt_i] & PTE_PRESENT)) return 0;
    
    return (pt[pt_i] & PTE_ADDR_MASK) + (virt_addr & 0xFFF);
}

/* ── VMM Public API ─────────────────────────────────────────────────────── */

void vmm_map_page(uint64_t virt_addr, uint64_t phys_addr, uint64_t flags) {
    if (!current_pml4) {
        serial_printf("VMM ERROR: vmm_map_page called before VMM initialized!\n");
        return;
    }

    uint64_t pml4_i = PML4_INDEX(virt_addr);
    uint64_t pdpt_i = PDPT_INDEX(virt_addr);
    uint64_t pd_i = PD_INDEX(virt_addr);
    uint64_t pt_i = PT_INDEX(virt_addr);

    // 1. PML4 -> PDPT
    if (!(current_pml4[pml4_i] & PTE_PRESENT)) {
        void *pdpt_phys = pmm_alloc_frame();
        if (!pdpt_phys) {
            serial_printf("VMM ERROR: Out of physical memory allocating PDPT!\n");
            for (;;) __asm__ volatile("hlt");
        }
        uint64_t *pdpt_virt = (uint64_t *)((uintptr_t)pdpt_phys + hhdm_off);
        memset(pdpt_virt, 0, PAGE_SIZE);
        current_pml4[pml4_i] = (uint64_t)pdpt_phys | PTE_PRESENT | PTE_WRITABLE;
    }
    uint64_t *pdpt = (uint64_t *)((current_pml4[pml4_i] & PTE_ADDR_MASK) + hhdm_off);

    // 2. PDPT -> PD
    if (!(pdpt[pdpt_i] & PTE_PRESENT)) {
        void *pd_phys = pmm_alloc_frame();
        if (!pd_phys) {
            serial_printf("VMM ERROR: Out of physical memory allocating PD!\n");
            for (;;) __asm__ volatile("hlt");
        }
        uint64_t *pd_virt = (uint64_t *)((uintptr_t)pd_phys + hhdm_off);
        memset(pd_virt, 0, PAGE_SIZE);
        pdpt[pdpt_i] = (uint64_t)pd_phys | PTE_PRESENT | PTE_WRITABLE;
    }
    uint64_t *pd = (uint64_t *)((pdpt[pdpt_i] & PTE_ADDR_MASK) + hhdm_off);

    // 3. PD -> PT
    if (!(pd[pd_i] & PTE_PRESENT)) {
        void *pt_phys = pmm_alloc_frame();
        if (!pt_phys) {
            serial_printf("VMM ERROR: Out of physical memory allocating PT!\n");
            for (;;) __asm__ volatile("hlt");
        }
        uint64_t *pt_virt = (uint64_t *)((uintptr_t)pt_phys + hhdm_off);
        memset(pt_virt, 0, PAGE_SIZE);
        pd[pd_i] = (uint64_t)pt_phys | PTE_PRESENT | PTE_WRITABLE;
    }
    uint64_t *pt = (uint64_t *)((pd[pd_i] & PTE_ADDR_MASK) + hhdm_off);

    // 4. Set PT entry
    pt[pt_i] = (phys_addr & PTE_ADDR_MASK) | flags;
}

void vmm_unmap_page(uint64_t virt_addr) {
    if (!current_pml4) return;

    uint64_t pml4_i = PML4_INDEX(virt_addr);
    if (!(current_pml4[pml4_i] & PTE_PRESENT)) return;
    uint64_t *pdpt = (uint64_t *)((current_pml4[pml4_i] & PTE_ADDR_MASK) + hhdm_off);

    uint64_t pdpt_i = PDPT_INDEX(virt_addr);
    if (!(pdpt[pdpt_i] & PTE_PRESENT)) return;
    uint64_t *pd = (uint64_t *)((pdpt[pdpt_i] & PTE_ADDR_MASK) + hhdm_off);

    uint64_t pd_i = PD_INDEX(virt_addr);
    if (!(pd[pd_i] & PTE_PRESENT)) return;
    uint64_t *pt = (uint64_t *)((pd[pd_i] & PTE_ADDR_MASK) + hhdm_off);

    uint64_t pt_i = PT_INDEX(virt_addr);
    pt[pt_i] = 0;

    // Invalidate TLB for this address
    __asm__ volatile("invlpg (%0)" : : "r"(virt_addr) : "memory");
}

uint64_t *vmm_get_pml4(void) {
    return current_pml4;
}

void vmm_init(uint64_t hhdm_offset) {
    hhdm_off = hhdm_offset;
    
    // Check responses from Limine
    if (memmap_request.response == NULL) {
        serial_printf("VMM ERROR: No memory map response from Limine!\n");
        for (;;) __asm__ volatile("hlt");
    }
    if (kernel_address_request.response == NULL) {
        serial_printf("VMM ERROR: No kernel address response from Limine!\n");
        for (;;) __asm__ volatile("hlt");
    }
    
    // Read the current cr3 to get Limine's active page table physical address
    uint64_t limine_pml4_phys;
    __asm__ volatile("mov %%cr3, %0" : "=r"(limine_pml4_phys));
    
    serial_printf("VMM: Initializing new page tables...\n");
    serial_printf("VMM: Limine active PML4 physical: %p\n", (void *)limine_pml4_phys);

    /* ── 1. Allocate + zero new PML4 ── */
    void *new_pml4_phys = pmm_alloc_frame();
    if (!new_pml4_phys) {
        serial_printf("VMM ERROR: Failed to allocate frame for new PML4!\n");
        for (;;) __asm__ volatile("hlt");
    }
    
    current_pml4 = (uint64_t *)((uintptr_t)new_pml4_phys + hhdm_off);
    memset(current_pml4, 0, PAGE_SIZE);
    serial_printf("VMM: New PML4 allocated at physical %p (virtual %p)\n", new_pml4_phys, current_pml4);

    /* ── 2. Map HHDM region into new PML4 ── */
    struct limine_memmap_response *memmap = memmap_request.response;
    serial_printf("VMM: Mapping HHDM region...\n");
    for (uint64_t i = 0; i < memmap->entry_count; i++) {
        struct limine_memmap_entry *entry = memmap->entries[i];
        uint64_t base = entry->base;
        uint64_t length = entry->length;
        
        // Ensure aligned to page boundaries
        uint64_t start = base & ~(PAGE_SIZE - 1);
        uint64_t end = (base + length + PAGE_SIZE - 1) & ~(PAGE_SIZE - 1);
        
        for (uint64_t phys = start; phys < end; phys += PAGE_SIZE) {
            vmm_map_page(phys + hhdm_off, phys, PTE_PRESENT | PTE_WRITABLE);
        }
    }
    serial_printf("VMM: HHDM region mapped successfully.\n");

    /* ── 3. Map kernel region into new PML4 ── */
    uint64_t kernel_phys_base = kernel_address_request.response->physical_base;
    uint64_t kernel_virt_base = kernel_address_request.response->virtual_base;
    
    uint64_t kernel_size = 0;
    for (uint64_t i = 0; i < memmap->entry_count; i++) {
        if (memmap->entries[i]->type == LIMINE_MEMMAP_KERNEL_AND_MODULES) {
            kernel_size = memmap->entries[i]->length;
            kernel_size = (kernel_size + PAGE_SIZE - 1) & ~(PAGE_SIZE - 1);
            break;
        }
    }
    
    if (kernel_size == 0) {
        serial_printf("VMM ERROR: Could not find kernel memory map entry!\n");
        for (;;) __asm__ volatile("hlt");
    }
    
    serial_printf("VMM: Mapping kernel region (physical=%p, virtual=%p, size=%d KB)...\n",
                  (void *)kernel_phys_base, (void *)kernel_virt_base, (int)(kernel_size / 1024));
                  
    for (uint64_t offset = 0; offset < kernel_size; offset += PAGE_SIZE) {
        vmm_map_page(kernel_virt_base + offset, kernel_phys_base + offset, PTE_PRESENT | PTE_WRITABLE);
    }
    serial_printf("VMM: Kernel region mapped successfully.\n");

    /* ── 4. Map framebuffer into new PML4 ── */
    if (framebuffer_request.response != NULL && framebuffer_request.response->framebuffer_count > 0) {
        struct limine_framebuffer *fb = framebuffer_request.response->framebuffers[0];
        uint64_t fb_virt = (uint64_t)fb->address;
        uint64_t fb_size = fb->pitch * fb->height;
        fb_size = (fb_size + PAGE_SIZE - 1) & ~(PAGE_SIZE - 1);
        
        serial_printf("VMM: Mapping framebuffer (virtual=%p, size=%d KB)...\n",
                      (void *)fb_virt, (int)(fb_size / 1024));
                      
        for (uint64_t offset = 0; offset < fb_size; offset += PAGE_SIZE) {
            uint64_t virt = fb_virt + offset;
            uint64_t phys = translate_limine_address(limine_pml4_phys, virt);
            if (phys != 0) {
                vmm_map_page(virt, phys, PTE_PRESENT | PTE_WRITABLE);
            } else {
                if (virt >= hhdm_off) {
                    vmm_map_page(virt, virt - hhdm_off, PTE_PRESENT | PTE_WRITABLE);
                } else {
                    serial_printf("VMM [WARN]: Failed to translate framebuffer page at %p\n", (void *)virt);
                }
            }
        }
        serial_printf("VMM: Framebuffer mapped successfully.\n");
    } else {
        serial_printf("VMM [WARN]: No framebuffer found, skipping framebuffer map.\n");
    }

    /* ── 5. Map current stack into new PML4 ── */
    uint64_t rsp;
    __asm__ volatile("mov %%rsp, %0" : "=r"(rsp));
    uint64_t stack_base = (rsp - 64 * 1024) & ~(PAGE_SIZE - 1);
    uint64_t stack_top = (rsp + 8 * 1024) & ~(PAGE_SIZE - 1);
    
    serial_printf("VMM: Mapping current stack around RSP=%p (range %p to %p)...\n",
                  (void *)rsp, (void *)stack_base, (void *)stack_top);
                  
    for (uint64_t virt = stack_base; virt <= stack_top; virt += PAGE_SIZE) {
        uint64_t phys = translate_limine_address(limine_pml4_phys, virt);
        if (phys != 0) {
            vmm_map_page(virt, phys, PTE_PRESENT | PTE_WRITABLE);
        } else {
            if (virt >= hhdm_off) {
                vmm_map_page(virt, virt - hhdm_off, PTE_PRESENT | PTE_WRITABLE);
            } else {
                serial_printf("VMM [WARN]: Failed to translate stack page at %p\n", (void *)virt);
            }
        }
    }
    serial_printf("VMM: Stack mapped successfully.\n");

    /* ── 6. ONLY THEN write CR3 ── */
    uint64_t new_cr3 = (uint64_t)new_pml4_phys;
    serial_printf("VMM: [CR3 Write] About to switch page table. CR3 = %p\n", (void *)new_cr3);
    
    __asm__ volatile("mov %0, %%cr3" : : "r"(new_cr3) : "memory");
    
    serial_printf("VMM: [CR3 Write] Switched to new PML4 successfully!\n");
}
