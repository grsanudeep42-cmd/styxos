#pragma once
/*
 * vfs.h — Minimal read-only VFS for M7.
 * Single backend: FAT32 on USB. No mounting, no inodes, no writeback.
 */
#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

typedef struct {
    uint32_t start_cluster;
    uint32_t size;
    bool     valid;
} vfs_node_t;

void vfs_init(void);                                      /* call after fat32_init() */
bool vfs_open(const char *path, vfs_node_t *out);
size_t vfs_read(const vfs_node_t *node, void *buf, size_t len);
