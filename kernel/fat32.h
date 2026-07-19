#pragma once
/*
 * fat32.h — FAT32 read-only driver.
 * Sits on top of usb_msc_read_sector(). No write support in M7.
 * Architecture Bible ref: §9 M7
 */
#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

bool fat32_init(void);

/*
 * fat32_open() — resolve a path like "/init.elf" to a start cluster + size.
 * Returns true on success. Path must be absolute, single-level (no subdirs in M7).
 */
bool fat32_open(const char *path, uint32_t *start_cluster, uint32_t *file_size);

/*
 * fat32_read() — read `len` bytes from file starting at cluster chain `cluster`
 * into `buf`. Returns bytes actually read.
 */
size_t fat32_read(uint32_t start_cluster, void *buf, size_t len);
