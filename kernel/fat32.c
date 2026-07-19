/*
 * fat32.c — FAT32 read-only driver (M7).
 * Parses BPB, follows cluster chains, walks root directory.
 */
#include "fat32.h"
#include "usb_msc.h"
#include "serial.h"
#include "heap.h"
#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

/* ── BPB (BIOS Parameter Block) — first 512 bytes of partition ──────────── */
typedef struct __attribute__((packed)) {
    uint8_t  jmp[3];
    uint8_t  oem[8];
    uint16_t bytes_per_sector;
    uint8_t  sectors_per_cluster;
    uint16_t reserved_sectors;
    uint8_t  num_fats;
    uint16_t root_entry_count;    /* 0 for FAT32 */
    uint16_t total_sectors_16;
    uint8_t  media_type;
    uint16_t fat_size_16;         /* 0 for FAT32 */
    uint16_t sectors_per_track;
    uint16_t num_heads;
    uint32_t hidden_sectors;
    uint32_t total_sectors_32;
    /* FAT32 extended */
    uint32_t fat_size_32;
    uint16_t ext_flags;
    uint16_t fs_version;
    uint32_t root_cluster;
    uint16_t fs_info;
    uint16_t backup_boot;
    uint8_t  reserved[12];
    uint8_t  drive_number;
    uint8_t  reserved1;
    uint8_t  boot_sig;
    uint32_t volume_id;
    uint8_t  volume_label[11];
    uint8_t  fs_type[8];
} bpb_t;

/* ── Directory entry (32 bytes) ─────────────────────────────────────────── */
typedef struct __attribute__((packed)) {
    uint8_t  name[8];
    uint8_t  ext[3];
    uint8_t  attr;
    uint8_t  reserved;
    uint8_t  crt_time_tenth;
    uint16_t crt_time;
    uint16_t crt_date;
    uint16_t last_acc_date;
    uint16_t cluster_hi;
    uint16_t wrt_time;
    uint16_t wrt_date;
    uint16_t cluster_lo;
    uint32_t file_size;
} dirent_t;

#define ATTR_VOLUME_ID  0x08
#define ATTR_DIRECTORY  0x10
#define ATTR_LFN        0x0F

/* ── Filesystem state ────────────────────────────────────────────────────── */
static uint32_t g_fat_start;       /* LBA of FAT1               */
static uint32_t g_data_start;      /* LBA of cluster 2           */
static uint32_t g_root_cluster;
static uint8_t  g_spc;             /* sectors per cluster        */
static uint8_t  g_sector_buf[512];

static bool g_ready = false;

/* ── Helpers ─────────────────────────────────────────────────────────────── */

static int read_sector(uint32_t lba) {
    return usb_msc_read_sector(lba, g_sector_buf);
}

static uint32_t cluster_to_lba(uint32_t cluster) {
    return g_data_start + (cluster - 2) * g_spc;
}

/* Read the FAT entry for a cluster — returns next cluster number */
static uint32_t fat_next(uint32_t cluster) {
    uint32_t fat_offset = cluster * 4;
    uint32_t fat_sector = g_fat_start + fat_offset / 512;
    uint32_t entry_off  = fat_offset % 512;
    if (read_sector(fat_sector) != 0) return 0x0FFFFFFF;
    uint32_t val;
    __builtin_memcpy(&val, &g_sector_buf[entry_off], 4);
    return val & 0x0FFFFFFF;
}

static bool is_eof(uint32_t cluster) {
    return cluster >= 0x0FFFFFF8;
}

/* ── FAT32 init ──────────────────────────────────────────────────────────── */
bool fat32_init(void) {
    if (read_sector(0) != 0) {
        serial_printf("[FAT32] Cannot read sector 0\n"); return false;
    }
    bpb_t *bpb = (bpb_t *)g_sector_buf;

    if (bpb->bytes_per_sector != 512) {
        serial_printf("[FAT32] Unsupported sector size: %d\n", bpb->bytes_per_sector);
        return false;
    }
    if (bpb->fat_size_32 == 0) {
        serial_printf("[FAT32] Not a FAT32 volume\n"); return false;
    }

    g_spc          = bpb->sectors_per_cluster;
    g_fat_start    = bpb->reserved_sectors;
    g_root_cluster = bpb->root_cluster;
    g_data_start   = g_fat_start + bpb->num_fats * bpb->fat_size_32;

    serial_printf("[FAT32] BPB ok. SPC=%d FAT@%d DATA@%d root_cluster=%d\n",
                  g_spc, g_fat_start, g_data_start, g_root_cluster);
    g_ready = true;
    return true;
}

/* ── 8.3 name comparison ─────────────────────────────────────────────────── */
/* Convert "init.elf" → "INIT    ELF" for matching against directory entry */
static void path_to_83(const char *name, char out[11]) {
    for (int i = 0; i < 11; i++) out[i] = ' ';
    int i = 0, j = 0;
    /* skip leading '/' */
    if (name[0] == '/') name++;
    while (name[i] && name[i] != '.' && j < 8) {
        char c = name[i++];
        out[j++] = (c >= 'a' && c <= 'z') ? c - 32 : c;
    }
    if (name[i] == '.') {
        i++; j = 8;
        while (name[i] && j < 11) {
            char c = name[i++];
            out[j++] = (c >= 'a' && c <= 'z') ? c - 32 : c;
        }
    }
}

static bool name83_eq(const uint8_t *dir_name, const char target[11]) {
    for (int i = 0; i < 11; i++)
        if (dir_name[i] != (uint8_t)target[i]) return false;
    return true;
}

/* ── fat32_open ──────────────────────────────────────────────────────────── */
bool fat32_open(const char *path, uint32_t *start_cluster, uint32_t *file_size) {
    if (!g_ready) return false;
    char target[11];
    path_to_83(path, target);

    uint32_t cluster = g_root_cluster;
    while (!is_eof(cluster)) {
        uint32_t lba = cluster_to_lba(cluster);
        for (uint8_t s = 0; s < g_spc; s++) {
            if (read_sector(lba + s) != 0) return false;
            dirent_t *dir = (dirent_t *)g_sector_buf;
            for (int e = 0; e < 512 / 32; e++) {
                uint8_t first = dir[e].name[0];
                if (first == 0x00) return false; /* no more entries */
                if (first == 0xE5) continue;     /* deleted */
                if (dir[e].attr == ATTR_LFN)     continue; /* LFN */
                if (dir[e].attr & ATTR_VOLUME_ID) continue;
                if (name83_eq(dir[e].name, target)) {
                    *start_cluster = ((uint32_t)dir[e].cluster_hi << 16) | dir[e].cluster_lo;
                    *file_size     = dir[e].file_size;
                    serial_printf("[FAT32] Found '%s' cluster=%d size=%d\n",
                                  path, *start_cluster, *file_size);
                    return true;
                }
            }
        }
        cluster = fat_next(cluster);
    }
    serial_printf("[FAT32] '%s' not found\n", path);
    return false;
}

/* ── fat32_read ──────────────────────────────────────────────────────────── */
size_t fat32_read(uint32_t start_cluster, void *buf, size_t len) {
    if (!g_ready) return 0;
    uint8_t *dst  = (uint8_t *)buf;
    size_t   done = 0;
    uint32_t cluster = start_cluster;

    while (!is_eof(cluster) && done < len) {
        uint32_t lba = cluster_to_lba(cluster);
        for (uint8_t s = 0; s < g_spc && done < len; s++) {
            if (read_sector(lba + s) != 0) return done;
            size_t chunk = 512;
            if (done + chunk > len) chunk = len - done;
            __builtin_memcpy(dst + done, g_sector_buf, chunk);
            done += chunk;
        }
        cluster = fat_next(cluster);
    }
    return done;
}
