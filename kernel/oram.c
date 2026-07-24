#include "oram.h"
#include "string.h"
#include "serial.h"

/* ── Global State ───────────────────────────────────────────────────────── */
static oram_bucket_t oram_tree[ORAM_NUM_NODES];
static uint32_t      pos_map[ORAM_NUM_LEAVES];

// Stash holds up to 64 blocks
static oram_block_t  stash[64];
static size_t        stash_count = 0;

uint32_t g_oram_sector_reads = 0;
uint32_t g_oram_sector_writes = 0;

/* ── LCG Random ─────────────────────────────────────────────────────────── */
static uint32_t lcg_rand(void) {
    static uint64_t seed = 0x123456789ABCDEFULL;
    uint64_t r = 0;
    __asm__ volatile("rdtsc" : "=A"(r));
    seed = seed * 6364136223846793005ULL + r + 1442695040888963407ULL;
    return (uint32_t)(seed >> 32);
}

/* ── PathORAM Helper Functions ──────────────────────────────────────────── */

// Returns 1 if ancestor is an ancestor of descendant (or they are the same)
static int is_ancestor(uint32_t ancestor, uint32_t descendant) {
    uint32_t curr = descendant;
    while (curr > 0) {
        if (curr == ancestor) return 1;
        curr = (curr - 1) / 2;
    }
    return (ancestor == 0); // Root is ancestor of all
}

// Get the path from root (0) to leaf node ((ORAM_NUM_LEAVES - 1) + leaf_idx)
static void get_path(uint32_t leaf_idx, uint32_t path[ORAM_L + 1]) {
    uint32_t curr = (ORAM_NUM_LEAVES - 1) + leaf_idx; // Leaf node index in tree
    for (int i = ORAM_L; i >= 0; i--) {
        path[i] = curr;
        if (curr > 0) {
            curr = (curr - 1) / 2;
        }
    }
}

/* ── Public APIs ────────────────────────────────────────────────────────── */

void oram_init(void) {
    // 1. Initialize all buckets to dummy blocks
    for (int i = 0; i < ORAM_NUM_NODES; i++) {
        for (int j = 0; j < ORAM_Z; j++) {
            oram_tree[i].blocks[j].lba = 0xFFFFFFFF;
            memset(oram_tree[i].blocks[j].data, 0, ORAM_BLOCK_SIZE);
        }
    }

    // 2. Map logical LBAs to random leaves
    for (int i = 0; i < ORAM_NUM_LEAVES; i++) {
        pos_map[i] = lcg_rand() % ORAM_NUM_LEAVES;
    }

    stash_count = 0;
    g_oram_sector_reads = 0;
    g_oram_sector_writes = 0;
    serial_printf("[ORAM] PathORAM initialized (Tree Depth L=%d, Z=%d)\n", ORAM_L, ORAM_Z);
}

void oram_reset_counters(void) {
    g_oram_sector_reads = 0;
    g_oram_sector_writes = 0;
}

void oram_print_stats(void) {
    uint32_t val_int = g_oram_sector_writes / ORAM_Z;
    uint32_t val_frac = (g_oram_sector_writes % ORAM_Z) * 10 / ORAM_Z;
    serial_printf("[ORAM] I/O Stats: Sector Reads=%d, Sector Writes=%d (Write Amplification: %d.%d)\n",
                  (int)g_oram_sector_reads, (int)g_oram_sector_writes,
                  (int)val_int, (int)val_frac);
}

int oram_access(uint32_t lba, int write, uint8_t *data) {
    if (lba >= ORAM_NUM_LEAVES) {
        serial_printf("[ORAM] ERROR: LBA %d exceeds max leaves %d\n", (int)lba, (int)ORAM_NUM_LEAVES);
        return -1;
    }

    // 1. Fetch leaf path
    uint32_t leaf = pos_map[lba];
    uint32_t path[ORAM_L + 1];
    get_path(leaf, path);

    serial_printf("[ORAM] Access LBA %d (Leaf %d, Path: %d->%d->%d->%d->%d->%d->%d)\n",
                  (int)lba, (int)leaf, (int)path[0], (int)path[1], (int)path[2], (int)path[3], (int)path[4], (int)path[5], (int)path[6]);

    // 2. Read path buckets into stash
    for (int i = 0; i <= ORAM_L; i++) {
        uint32_t node = path[i];
        
        // Simulating physical disk reads (each bucket consists of Z=4 sectors)
        g_oram_sector_reads += ORAM_Z;

        // Copy non-empty blocks to stash
        for (int j = 0; j < ORAM_Z; j++) {
            oram_block_t *b = &oram_tree[node].blocks[j];
            if (b->lba != 0xFFFFFFFF) {
                if (stash_count < 64) {
                    memcpy(&stash[stash_count++], b, sizeof(oram_block_t));
                } else {
                    serial_printf("[ORAM] ERROR: Stash overflow!\n");
                    return -1;
                }
            }
        }
    }

    // 3. Process read/write operation in stash
    int found_idx = -1;
    for (size_t i = 0; i < stash_count; i++) {
        if (stash[i].lba == lba) {
            found_idx = (int)i;
            break;
        }
    }

    if (write) {
        if (found_idx != -1) {
            memcpy(stash[found_idx].data, data, ORAM_BLOCK_SIZE);
        } else {
            // New block insert
            if (stash_count < 64) {
                stash[stash_count].lba = lba;
                memcpy(stash[stash_count].data, data, ORAM_BLOCK_SIZE);
                stash_count++;
            } else {
                serial_printf("[ORAM] ERROR: Stash overflow during write!\n");
                return -1;
            }
        }
    } else {
        if (found_idx != -1) {
            memcpy(data, stash[found_idx].data, ORAM_BLOCK_SIZE);
        } else {
            // Block never written: return zeroes
            memset(data, 0, ORAM_BLOCK_SIZE);
        }
    }

    // 4. Update position map to a new random leaf
    uint32_t new_leaf = lcg_rand() % ORAM_NUM_LEAVES;
    pos_map[lba] = new_leaf;

    // 5. Evict blocks from stash back to the path buckets (from leaf up to root)
    for (int i = ORAM_L; i >= 0; i--) {
        uint32_t node = path[i];
        size_t placed_count = 0;

        // Prepare new bucket configuration
        oram_bucket_t new_bucket;
        for (int j = 0; j < ORAM_Z; j++) {
            new_bucket.blocks[j].lba = 0xFFFFFFFF;
            memset(new_bucket.blocks[j].data, 0, ORAM_BLOCK_SIZE);
        }

        // Fill bucket with blocks from stash that can reside in this node
        for (size_t s = 0; s < stash_count && placed_count < ORAM_Z; ) {
            uint32_t s_lba = stash[s].lba;
            uint32_t s_leaf = pos_map[s_lba];
            uint32_t s_leaf_node = (ORAM_NUM_LEAVES - 1) + s_leaf;

            if (is_ancestor(node, s_leaf_node)) {
                // Move from stash to bucket
                memcpy(&new_bucket.blocks[placed_count++], &stash[s], sizeof(oram_block_t));
                // Remove from stash by shifting remaining elements
                for (size_t k = s; k < stash_count - 1; k++) {
                    memcpy(&stash[k], &stash[k + 1], sizeof(oram_block_t));
                }
                stash_count--;
                // Do not increment s since the current index has a new element
            } else {
                s++;
            }
        }

        // Write the bucket back to the tree
        memcpy(&oram_tree[node], &new_bucket, sizeof(oram_bucket_t));
        
        // Simulating physical disk writes (each bucket write = Z=4 sectors)
        g_oram_sector_writes += ORAM_Z;
    }

    return 0; // Success
}
