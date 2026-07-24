/*
 * oram.c — PathORAM wired to real USB sectors
 *
 * ORAM tree stored on USB at LBA ORAM_BASE_LBA through ORAM_BASE_LBA + ORAM_NUM_NODES * ORAM_Z.
 * Each bucket = ORAM_Z blocks of 512 bytes = one USB sector per block.
 * Physical sector for node N, slot J: ORAM_BASE_LBA + N * ORAM_Z + J
 *
 * On first boot (no magic in sector ORAM_BASE_LBA):
 *   - Write dummy blocks to all ORAM sectors
 *   - Write random position map to sectors ORAM_POSMAP_LBA onwards
 *   - Write magic marker to ORAM_BASE_LBA - 1
 *
 * Position map persisted to USB (sectors ORAM_POSMAP_LBA onwards).
 */
#include "oram.h"
#include "usb_msc.h"
#include "csprng.h"
#include "string.h"
#include "serial.h"
#include <stdint.h>

/* USB sector layout for ORAM */
#define ORAM_META_LBA    499U   /* single sector: magic + posmap header */
#define ORAM_BASE_LBA    500U   /* start of tree: ORAM_NUM_NODES * ORAM_Z sectors */
                                /* 127 nodes * 4 slots = 508 sectors (LBA 500-1007) */
#define ORAM_POSMAP_LBA  1008U  /* position map: ORAM_NUM_LEAVES * 4 bytes = 256 bytes = 1 sector */
#define ORAM_MAGIC       0x4F52414DU  /* "ORAM" */

/* In-RAM position map (small enough: 64 entries × 4 bytes = 256 bytes) */
static uint32_t pos_map[ORAM_NUM_LEAVES];

/* In-RAM stash (up to 64 blocks) */
static oram_block_t stash[64];
static size_t       stash_count = 0;

uint32_t g_oram_sector_reads  = 0;
uint32_t g_oram_sector_writes = 0;

/* ── CSPRNG-backed rand for position map ──────────────────────────────── */
static uint32_t oram_rand_leaf(void) {
    return csprng_u32() % ORAM_NUM_LEAVES;
}

/* ── Physical USB I/O for ORAM ────────────────────────────────────────── */
static void oram_read_block(uint32_t node, uint32_t slot, oram_block_t *out) {
    uint32_t lba = ORAM_BASE_LBA + node * ORAM_Z + slot;
    uint8_t  buf[512];
    /* Read raw sector (encryption is handled by usb_msc layer) */
    usb_msc_read_sector(lba, buf);
    memcpy(out, buf, sizeof(oram_block_t));
    g_oram_sector_reads++;
}

static void oram_write_block(uint32_t node, uint32_t slot, const oram_block_t *blk) {
    uint32_t lba = ORAM_BASE_LBA + node * ORAM_Z + slot;
    uint8_t  buf[512];
    memset(buf, 0, 512);
    memcpy(buf, blk, sizeof(oram_block_t));
    usb_msc_write_sector(lba, buf);
    g_oram_sector_writes++;
}

/* ── Persist / load position map ─────────────────────────────────────── */
static void posmap_save(void) {
    uint8_t buf[512];
    memset(buf, 0, 512);
    memcpy(buf, pos_map, ORAM_NUM_LEAVES * sizeof(uint32_t));
    bool saved = g_encryption_enabled;
    g_encryption_enabled = false;
    usb_msc_write_sector(ORAM_POSMAP_LBA, buf);
    g_encryption_enabled = saved;
}

static void posmap_load(void) {
    uint8_t buf[512];
    bool saved = g_encryption_enabled;
    g_encryption_enabled = false;
    usb_msc_read_sector(ORAM_POSMAP_LBA, buf);
    g_encryption_enabled = saved;
    memcpy(pos_map, buf, ORAM_NUM_LEAVES * sizeof(uint32_t));
}

/* ── PathORAM tree helpers ────────────────────────────────────────────── */
static int is_ancestor(uint32_t ancestor, uint32_t descendant) {
    uint32_t curr = descendant;
    while (curr > 0) {
        if (curr == ancestor) return 1;
        curr = (curr - 1) / 2;
    }
    return (ancestor == 0);
}

static void get_path(uint32_t leaf_idx, uint32_t path[ORAM_L + 1]) {
    uint32_t curr = (ORAM_NUM_LEAVES - 1) + leaf_idx;
    for (int i = ORAM_L; i >= 0; i--) {
        path[i] = curr;
        if (curr > 0) curr = (curr - 1) / 2;
    }
}

/* ── Format ORAM region on first boot ───────────────────────────────────── */
static void oram_format(void) {
    serial_printf("[ORAM] First boot: formatting ORAM region (LBA %d-%d)...\n",
                  ORAM_BASE_LBA, ORAM_BASE_LBA + ORAM_NUM_NODES * ORAM_Z - 1);

    bool saved = g_encryption_enabled;
    g_encryption_enabled = false;

    /* Write dummy blocks to all tree positions */
    oram_block_t dummy;
    dummy.lba = 0xFFFFFFFF;
    memset(dummy.data, 0, ORAM_BLOCK_SIZE);

    for (int n = 0; n < ORAM_NUM_NODES; n++) {
        for (int j = 0; j < ORAM_Z; j++) {
            oram_write_block((uint32_t)n, (uint32_t)j, &dummy);
        }
    }

    /* Randomize position map */
    for (int i = 0; i < ORAM_NUM_LEAVES; i++) {
        pos_map[i] = oram_rand_leaf();
    }
    posmap_save();

    /* Write magic */
    uint8_t meta[512];
    memset(meta, 0, 512);
    meta[0] = (uint8_t)(ORAM_MAGIC);
    meta[1] = (uint8_t)(ORAM_MAGIC >> 8);
    meta[2] = (uint8_t)(ORAM_MAGIC >> 16);
    meta[3] = (uint8_t)(ORAM_MAGIC >> 24);
    usb_msc_write_sector(ORAM_META_LBA, meta);

    g_encryption_enabled = saved;
    serial_printf("[ORAM] Format complete.\n");
}

/* ── oram_init ───────────────────────────────────────────────────────────── */
void oram_init(void) {
    stash_count = 0;
    g_oram_sector_reads = g_oram_sector_writes = 0;

    if (usb_msc_sector_count() == 0) {
        serial_printf("[ORAM] No USB storage — ORAM disabled\n");
        return;
    }

    /* Check for existing ORAM region */
    uint8_t meta[512];
    bool saved = g_encryption_enabled;
    g_encryption_enabled = false;
    usb_msc_read_sector(ORAM_META_LBA, meta);
    g_encryption_enabled = saved;

    uint32_t magic = (uint32_t)meta[0] | ((uint32_t)meta[1] << 8) |
                     ((uint32_t)meta[2] << 16) | ((uint32_t)meta[3] << 24);

    if (magic != ORAM_MAGIC) {
        oram_format();
    } else {
        serial_printf("[ORAM] Existing ORAM region found — loading position map\n");
        posmap_load();
    }

    serial_printf("[ORAM] PathORAM ready (L=%d Z=%d, USB LBA %d-%d)\n",
                  ORAM_L, ORAM_Z, ORAM_BASE_LBA,
                  ORAM_BASE_LBA + ORAM_NUM_NODES * ORAM_Z - 1);
}

void oram_reset_counters(void) {
    g_oram_sector_reads = g_oram_sector_writes = 0;
}

void oram_print_stats(void) {
    uint32_t val_int  = g_oram_sector_writes / ORAM_Z;
    uint32_t val_frac = (g_oram_sector_writes % ORAM_Z) * 10 / ORAM_Z;
    serial_printf("[ORAM] Stats: reads=%d writes=%d amplification=%d.%d\n",
                  g_oram_sector_reads, g_oram_sector_writes, val_int, val_frac);
}

/* ── oram_access ─────────────────────────────────────────────────────────── */
int oram_access(uint32_t lba, int write, uint8_t *data) {
    if (lba >= ORAM_NUM_LEAVES) {
        serial_printf("[ORAM] ERROR: LBA %d >= max %d\n", lba, ORAM_NUM_LEAVES);
        return -1;
    }

    /* 1. Fetch leaf path */
    uint32_t leaf = pos_map[lba];
    uint32_t path[ORAM_L + 1];
    get_path(leaf, path);

    /* 2. Read path buckets from USB into stash */
    for (int i = 0; i <= ORAM_L; i++) {
        uint32_t node = path[i];
        for (int j = 0; j < ORAM_Z; j++) {
            oram_block_t b;
            oram_read_block(node, (uint32_t)j, &b);
            if (b.lba != 0xFFFFFFFF) {
                if (stash_count < 64) {
                    memcpy(&stash[stash_count++], &b, sizeof(oram_block_t));
                } else {
                    serial_printf("[ORAM] Stash overflow!\n");
                    return -1;
                }
            }
        }
    }

    /* 3. Process operation */
    int found_idx = -1;
    for (size_t i = 0; i < stash_count; i++) {
        if (stash[i].lba == lba) { found_idx = (int)i; break; }
    }

    if (write) {
        if (found_idx != -1) {
            memcpy(stash[found_idx].data, data, ORAM_BLOCK_SIZE);
        } else {
            if (stash_count < 64) {
                stash[stash_count].lba = lba;
                memcpy(stash[stash_count].data, data, ORAM_BLOCK_SIZE);
                stash_count++;
            } else {
                serial_printf("[ORAM] Stash overflow on write!\n");
                return -1;
            }
        }
    } else {
        if (found_idx != -1) memcpy(data, stash[found_idx].data, ORAM_BLOCK_SIZE);
        else memset(data, 0, ORAM_BLOCK_SIZE);
    }

    /* 4. New random leaf */
    uint32_t new_leaf = oram_rand_leaf();
    pos_map[lba] = new_leaf;

    /* 5. Evict stash back to path (leaf → root) */
    for (int i = ORAM_L; i >= 0; i--) {
        uint32_t node = path[i];
        size_t   placed = 0;

        oram_block_t new_bucket[ORAM_Z];
        for (int j = 0; j < ORAM_Z; j++) {
            new_bucket[j].lba = 0xFFFFFFFF;
            memset(new_bucket[j].data, 0, ORAM_BLOCK_SIZE);
        }

        for (size_t s = 0; s < stash_count && placed < (size_t)ORAM_Z; ) {
            uint32_t s_leaf      = pos_map[stash[s].lba];
            uint32_t s_leaf_node = (ORAM_NUM_LEAVES - 1) + s_leaf;

            if (is_ancestor(node, s_leaf_node)) {
                memcpy(&new_bucket[placed++], &stash[s], sizeof(oram_block_t));
                for (size_t k = s; k < stash_count - 1; k++)
                    memcpy(&stash[k], &stash[k + 1], sizeof(oram_block_t));
                stash_count--;
            } else {
                s++;
            }
        }

        for (int j = 0; j < ORAM_Z; j++)
            oram_write_block(node, (uint32_t)j, &new_bucket[j]);
    }

    /* 6. Persist updated position map */
    posmap_save();

    return 0;
}
