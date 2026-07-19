#include "elf.h"
#include "vmm.h"
#include "pmm.h"
#include "serial.h"
#include "string.h"

/*
 * elf.c — Minimal ELF64 loader implementation.
 */

int elf_load(task_t *task, const uint8_t *elf_data, size_t size) {
    if (size < sizeof(elf64_header_t)) {
        serial_printf("[ELF] ERROR: ELF data too small\n");
        return -1;
    }

    const elf64_header_t *header = (const elf64_header_t *)elf_data;

    /* Validate ELF magic */
    uint32_t magic = *(const uint32_t *)header->e_ident;
    if (magic != ELF_MAGIC) {
        serial_printf("[ELF] ERROR: Invalid ELF magic %p\n", (void *)(uintptr_t)magic);
        return -2;
    }

    /* Validate 64-bit class (e_ident[4] == 2) */
    if (header->e_ident[4] != 2) {
        serial_printf("[ELF] ERROR: Not a 64-bit ELF binary\n");
        return -3;
    }

    /* Validate machine is EM_X86_64 (62) */
    if (header->e_machine != 62) {
        serial_printf("[ELF] ERROR: Machine is not x86-64 (got %d)\n", (int)header->e_machine);
        return -4;
    }

    const elf64_phdr_t *ph = (const elf64_phdr_t *)(elf_data + header->e_phoff);
    uint64_t hhdm_off = vmm_get_hhdm_offset();

    /* Save and set active PML4 for mapping */
    uint64_t *old_pml4 = vmm_get_pml4();
    vmm_set_pml4(task->pml4);

    for (uint16_t i = 0; i < header->e_phnum; i++) {
        if (ph[i].p_type != PT_LOAD) {
            continue;
        }

        uint64_t vaddr   = ph[i].p_vaddr;
        uint64_t filesz  = ph[i].p_filesz;
        uint64_t memsz   = ph[i].p_memsz;
        uint64_t offset  = ph[i].p_offset;

        if (offset + filesz > size) {
            serial_printf("[ELF] ERROR: segment %d offset + filesz exceeds total size\n", i);
            vmm_set_pml4(old_pml4);
            return -5;
        }

        /* Determine page-aligned boundaries */
        uint64_t start_page = vaddr & ~0xFFFULL;
        uint64_t end_page   = (vaddr + memsz + 4095) & ~0xFFFULL;

        serial_printf("[ELF] Loading PT_LOAD segment %d: virt [%p - %p] filesz=%d memsz=%d\n",
                      i, (void *)vaddr, (void *)(vaddr + memsz), (int)filesz, (int)memsz);

        for (uint64_t cur_vaddr = start_page; cur_vaddr < end_page; cur_vaddr += 4096) {
            void *phys = pmm_alloc_frame();
            if (!phys) {
                serial_printf("[ELF] ERROR: Out of physical memory loading segment\n");
                vmm_set_pml4(old_pml4);
                return -6;
            }

            /* Zero the page frame first */
            uint8_t *page_virt = (uint8_t *)((uintptr_t)phys + hhdm_off);
            memset(page_virt, 0, 4096);

            /* Copy segment file data into frame where overlapping */
            for (uint64_t j = 0; j < 4096; j++) {
                uint64_t byte_vaddr = cur_vaddr + j;
                if (byte_vaddr >= vaddr && byte_vaddr < (vaddr + filesz)) {
                    page_virt[j] = elf_data[offset + (byte_vaddr - vaddr)];
                }
            }

            /* Map this page as user accessible, present, writable */
            vmm_map_page(cur_vaddr, (uint64_t)phys, PTE_PRESENT | PTE_WRITABLE | PTE_USER);
        }
    }

    /* Restore active PML4 */
    vmm_set_pml4(old_pml4);

    serial_printf("[ELF] ELF successfully loaded. Entry point: %p\n", (void *)header->e_entry);
    return 0;
}

/* ── elf_load_from_vfs ───────────────────────────────────────────────────── */
#include "vfs.h"
#include "heap.h"

int elf_load_from_vfs(task_t *task, const char *path, uint64_t *entry_out) {
    vfs_node_t node;
    if (!vfs_open(path, &node)) {
        serial_printf("[ELF] vfs_open('%s') failed\n", path);
        return -1;
    }

    uint8_t *buf = (uint8_t *)kmalloc(node.size);
    if (!buf) {
        serial_printf("[ELF] kmalloc(%d) failed\n", (int)node.size);
        return -2;
    }

    size_t got = vfs_read(&node, buf, node.size);
    if (got < sizeof(elf64_header_t)) {
        serial_printf("[ELF] Short read: %d bytes\n", (int)got);
        kfree(buf);
        return -3;
    }

    int rc = elf_load(task, buf, got);
    if (rc == 0 && entry_out) {
        const elf64_header_t *h = (const elf64_header_t *)buf;
        *entry_out = h->e_entry;
    }
    kfree(buf);
    return rc;
}
