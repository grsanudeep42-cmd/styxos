/*
 * vfs.c — Minimal read-only VFS backed by FAT32 (M7).
 */
#include "vfs.h"
#include "fat32.h"
#include "serial.h"

static bool g_ready = false;

void vfs_init(void) {
    g_ready = fat32_init();
    if (g_ready) serial_printf("[VFS] Mounted FAT32 on USB.\n");
    else          serial_printf("[VFS] FAT32 mount failed.\n");
}

bool vfs_open(const char *path, vfs_node_t *out) {
    if (!g_ready) { out->valid = false; return false; }
    uint32_t cluster, size;
    if (!fat32_open(path, &cluster, &size)) {
        out->valid = false; return false;
    }
    out->start_cluster = cluster;
    out->size          = size;
    out->valid         = true;
    serial_printf("[VFS] '%s' opened: cluster=%d size=%d\n", path, cluster, size);
    return true;
}

size_t vfs_read(const vfs_node_t *node, void *buf, size_t len) {
    if (!node->valid) return 0;
    if (len > node->size) len = node->size;
    return fat32_read(node->start_cluster, buf, len);
}
