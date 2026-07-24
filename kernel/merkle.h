#pragma once
#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

/* merkle.h — SHA-256 Merkle tree over all encrypted USB sectors
 *
 * Tree covers up to MERKLE_MAX_SECTORS leaf sectors.
 * Each leaf = SHA-256 of the 512-byte plaintext sector.
 * Internal nodes = SHA-256(left_child || right_child).
 * Root hash stored in USB sector 3 with HMAC-SHA512 protection.
 *
 * USB sector layout:
 *   Sector 3  : Merkle root + HMAC (512 bytes)
 *   Sectors 10-265 : Merkle leaf/node hash table (256 sectors × 32 hashes = 8192 leaves)
 *
 * Initialised on first boot (compute from current disk state).
 * Updated on every write. Verified on every read.
 */

#define MERKLE_LEAF_SECTOR_BASE  10U    /* USB LBA where hash table starts */
#define MERKLE_MAX_SECTORS       8192U  /* Leaves = sectors we can cover */
#define SHA256_LEN               32

void merkle_init(void);
void merkle_update(uint32_t lba, const uint8_t plaintext[512]);
bool merkle_verify(uint32_t lba, const uint8_t plaintext[512]);
void merkle_get_root(uint8_t root_out[SHA256_LEN]);
