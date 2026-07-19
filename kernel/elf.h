#pragma once

#include <stdint.h>
#include <stddef.h>
#include "task.h"

/*
 * elf.h — Minimal ELF64 executable parser and loader.
 *
 * Architecture Bible ref: §9 Milestone 6 (ELF loader).
 */

#define ELF_MAGIC 0x464C457FU /* "\x7fELF" in little-endian */

typedef struct {
    uint8_t  e_ident[16];
    uint16_t e_type;
    uint16_t e_machine;
    uint32_t e_version;
    uint64_t e_entry;
    uint64_t e_phoff;
    uint64_t e_shoff;
    uint32_t e_flags;
    uint16_t e_ehsize;
    uint16_t e_phentsize;
    uint16_t e_phnum;
    uint16_t e_shentsize;
    uint16_t e_shnum;
    uint16_t e_shstrndx;
} __attribute__((packed)) elf64_header_t;

typedef struct {
    uint32_t p_type;
    uint32_t p_flags;
    uint64_t p_offset;
    uint64_t p_vaddr;
    uint64_t p_paddr;
    uint64_t p_filesz;
    uint64_t p_memsz;
    uint64_t p_align;
} __attribute__((packed)) elf64_phdr_t;

#define PT_LOAD 1

#define PF_X 1
#define PF_W 2
#define PF_R 4

/*
 * elf_load() — Parse ELF64 binary from elf_data buffer and load its PT_LOAD
 * segments into the given user task's virtual address space.
 *
 * Returns 0 on success, or negative on failure.
 */
int elf_load(task_t *task, const uint8_t *elf_data, size_t size);

/*
 * elf_load_from_vfs() — open path via VFS, read into a temporary kernel
 * buffer, then call elf_load(). Returns 0 on success, negative on failure.
 * On success, sets *entry_out to the ELF entry point virtual address.
 */
int elf_load_from_vfs(task_t *task, const char *path, uint64_t *entry_out);
