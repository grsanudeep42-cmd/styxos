#pragma once

#include <stdint.h>
#include <stddef.h>

#define ORAM_L          6   // Tree depth
#define ORAM_Z          4   // Bucket capacity
#define ORAM_BLOCK_SIZE 512 // 512-byte blocks (matching sector size)
#define ORAM_NUM_LEAVES 64  // 2^L
#define ORAM_NUM_NODES  127 // 2^(L+1) - 1

typedef struct {
    uint32_t lba;       // Logical LBA (0xFFFFFFFF = dummy)
    uint8_t  data[ORAM_BLOCK_SIZE];
} oram_block_t;

typedef struct {
    oram_block_t blocks[ORAM_Z];
} oram_bucket_t;

extern uint32_t g_oram_sector_reads;
extern uint32_t g_oram_sector_writes;

void oram_init(void);
int oram_access(uint32_t lba, int write, uint8_t *data);
void oram_reset_counters(void);
void oram_print_stats(void);
