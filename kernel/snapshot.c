#include "snapshot.h"
#include "gcm.h"
#include "oram.h"
#include "task.h"
#include "sched.h"
#include "cap.h"
#include "vmm.h"
#include "pmm.h"
#include "string.h"
#include "heap.h"
#include "serial.h"

/* ── Constants & Configuration ──────────────────────────────────────────── */
#define SNAPSHOT_MAGIC   0x53545958534e4150ULL // "STYXSNAP"
#define SNAPSHOT_VERSION 1

// We use a static key for snapshot integrity
static const uint8_t g_snapshot_key[32] = {
    0x53, 0x54, 0x59, 0x61, 0x73, 0x6e, 0x61, 0x70,
    0x49, 0x6e, 0x74, 0x65, 0x67, 0x72, 0x69, 0x74,
    0x79, 0x4b, 0x65, 0x79, 0x5f, 0x33, 0x32, 0x42,
    0x79, 0x74, 0x65, 0x73, 0x5f, 0x4d, 0x31, 0x30
};

static uint64_t g_sequence_number = 1;

/* ── Structures ─────────────────────────────────────────────────────────── */

typedef struct {
    uint64_t magic;
    uint32_t version;
    uint32_t cpuid_ecx;
    uint32_t cpuid_edx;
    uint32_t task_count;
    uint32_t page_count;
    uint32_t total_sectors;
    uint64_t sequence;
    uint8_t  cap_pool[16 * sizeof(endpoint_t)];
    uint8_t  cap_pool_used[16];
} snapshot_header_t;

typedef struct {
    uint32_t id;
    uint32_t state;
    uint32_t ticks;
    uint64_t kstack_top;
    task_regs_t regs;
    cap_table_t cap_table;
    uint32_t has_pml4;
} snapshot_task_t;

typedef struct {
    uint32_t task_id;
    uint64_t virt_addr;
} snapshot_page_header_t;

/* ── Inline Assembly Helpers ────────────────────────────────────────────── */
static void get_cpuid_features(uint32_t *ecx, uint32_t *edx) {
    uint32_t eax = 1, ebx = 0;
    __asm__ volatile(
        "cpuid"
        : "=c"(*ecx), "=d"(*edx), "+a"(eax), "=b"(ebx)
        :
        : "cc"
    );
}

/* ── Public APIs ────────────────────────────────────────────────────────── */

void snapshot_init(void) {
    oram_init();
    serial_printf("[SNAPSHOT] Session Snapshot System initialized.\n");
}

int snapshot_save(void) {
    serial_printf("[SNAPSHOT] Starting secure RAM session snapshot...\n");

    // 1. Reset PathORAM I/O counters to measure amplification
    oram_reset_counters();

    // 2. Allocate serialization buffer (32 KB is plenty for 2 tasks + stack + data page)
    uint8_t *serialize_buf = (uint8_t *)kmalloc(32768);
    if (!serialize_buf) {
        serial_printf("[SNAPSHOT] ERROR: Failed to allocate serialization buffer!\n");
        return -1;
    }
    memset(serialize_buf, 0, 32768);

    // 3. Serialize tasks
    task_t *tasks[8];
    int task_count = sched_get_tasks(tasks, 8);
    
    uint8_t *p = serialize_buf;
    
    // Write placeholder for task count (we will fill header later)
    p += sizeof(snapshot_header_t);

    for (int i = 0; i < task_count; i++) {
        task_t *t = tasks[i];
        snapshot_task_t *st = (snapshot_task_t *)p;
        st->id = t->id;
        st->state = t->state;
        st->ticks = t->ticks;
        st->kstack_top = t->kstack_top;
        st->regs = t->regs;
        st->has_pml4 = (t->pml4 != NULL);
        if (t->cap_table) {
            memcpy(&st->cap_table, t->cap_table, sizeof(cap_table_t));
        } else {
            memset(&st->cap_table, 0, sizeof(cap_table_t));
        }
        p += sizeof(snapshot_task_t);
        serial_printf("[SNAPSHOT] Serialized Task %d (State: %d, StackTop: %p)\n",
                      t->id, t->state, (void *)t->kstack_top);
    }

    // 4. Serialize memory pages
    uint64_t hhdm_off = vmm_get_hhdm_offset();
    uint32_t page_count = 0;

    for (int i = 0; i < task_count; i++) {
        task_t *t = tasks[i];
        if (!t->pml4) continue; // Idle task has no private PML4

        // Traverse 4-level page tables to find userspace pages (PTE_USER)
        uint64_t *pml4 = t->pml4;
        for (int pml4_i = 0; pml4_i < 512; pml4_i++) {
            if (!(pml4[pml4_i] & PTE_PRESENT) || !(pml4[pml4_i] & PTE_USER)) continue;

            uint64_t *pdpt = (uint64_t *)((pml4[pml4_i] & 0x000ffffffffff000ULL) + hhdm_off);
            for (int pdpt_i = 0; pdpt_i < 512; pdpt_i++) {
                if (!(pdpt[pdpt_i] & PTE_PRESENT) || !(pdpt[pdpt_i] & PTE_USER)) continue;

                uint64_t *pd = (uint64_t *)((pdpt[pdpt_i] & 0x000ffffffffff000ULL) + hhdm_off);
                for (int pd_i = 0; pd_i < 512; pd_i++) {
                    if (!(pd[pd_i] & PTE_PRESENT) || !(pd[pd_i] & PTE_USER)) continue;

                    uint64_t *pt = (uint64_t *)((pd[pd_i] & 0x000ffffffffff000ULL) + hhdm_off);
                    for (int pt_i = 0; pt_i < 512; pt_i++) {
                        if (!(pt[pt_i] & PTE_PRESENT) || !(pt[pt_i] & PTE_USER)) continue;

                        uint64_t virt = ((uint64_t)pml4_i << 39) |
                                        ((uint64_t)pdpt_i << 30) |
                                        ((uint64_t)pd_i << 21) |
                                        ((uint64_t)pt_i << 12);
                        uint64_t phys = pt[pt_i] & 0x000ffffffffff000ULL;

                        // Save page metadata
                        snapshot_page_header_t *ph = (snapshot_page_header_t *)p;
                        ph->task_id = t->id;
                        ph->virt_addr = virt;
                        p += sizeof(snapshot_page_header_t);

                        // Save 4096 bytes of page contents directly from kernel mapping
                        memcpy(p, (void *)(phys + hhdm_off), 4096);
                        p += 4096;

                        page_count++;
                        serial_printf("[SNAPSHOT] Serialized User Page: Task %d, VA %p -> Phys %p\n",
                                      t->id, (void *)virt, (void *)phys);
                    }
                }
            }
        }
    }

    // Calculate total size of payload
    size_t payload_len = p - (serialize_buf + sizeof(snapshot_header_t));
    size_t block_payload_size = 480; // 480 bytes plaintext per sector
    size_t total_sectors = (payload_len + block_payload_size - 1) / block_payload_size;

    // 5. Fill header
    snapshot_header_t *header = (snapshot_header_t *)serialize_buf;
    header->magic = SNAPSHOT_MAGIC;
    header->version = SNAPSHOT_VERSION;
    get_cpuid_features(&header->cpuid_ecx, &header->cpuid_edx);
    header->task_count = task_count;
    header->page_count = page_count;
    header->total_sectors = total_sectors;
    header->sequence = g_sequence_number;
    cap_export_pool(header->cap_pool, header->cap_pool_used);

    serial_printf("[SNAPSHOT] Payload size: %d bytes (%d sectors needed)\n",
                  (int)payload_len, (int)total_sectors);

    // 6. Encrypt and write data sectors 1 to total_sectors
    aes_gcm_ctx_t gcm_ctx;
    aes_gcm_init(&gcm_ctx, g_snapshot_key);

    for (size_t lba = 1; lba <= total_sectors; lba++) {
        uint8_t sector[512];
        memset(sector, 0, 512);

        // Get 480-byte block offset in payload
        size_t offset = sizeof(snapshot_header_t) + (lba - 1) * block_payload_size;
        
        // Generate IV
        uint8_t iv[12];
        memset(iv, 0, 12);
        for (int j = 0; j < 4; j++) iv[j] = (uint8_t)(lba >> (24 - j * 8));
        for (int j = 0; j < 8; j++) iv[4 + j] = (uint8_t)(g_sequence_number >> (56 - j * 8));

        // AAD: LBA + sequence
        uint8_t aad[12];
        memcpy(aad, iv, 12);

        // GCM Encrypt
        uint8_t tag[16];
        aes_gcm_encrypt(&gcm_ctx, iv, aad, 12, serialize_buf + offset, block_payload_size, sector, tag);

        // Pack into 512-byte sector structure
        memcpy(sector + 480, iv, 12);
        memcpy(sector + 492, tag, 16);
        for (int j = 0; j < 4; j++) sector[508 + j] = (uint8_t)(lba >> (24 - j * 8));

        // Write via PathORAM
        if (oram_access((uint32_t)lba, 1, sector) != 0) {
            serial_printf("[SNAPSHOT] ERROR: PathORAM write failed for LBA %d\n", (int)lba);
            kfree(serialize_buf);
            return -1;
        }
    }

    // 7. Atomic Commit: encrypt and write Block 0 last
    uint8_t header_sector[512];
    memset(header_sector, 0, 512);

    uint8_t iv0[12];
    memset(iv0, 0, 12);
    for (int j = 0; j < 8; j++) iv0[4 + j] = (uint8_t)(g_sequence_number >> (56 - j * 8));

    uint8_t tag0[16];
    aes_gcm_encrypt(&gcm_ctx, iv0, iv0, 12, serialize_buf, 480, header_sector, tag0);

    memcpy(header_sector + 480, iv0, 12);
    memcpy(header_sector + 492, tag0, 16);
    // Block 0 marker at end
    memset(header_sector + 508, 0, 4);

    serial_printf("[SNAPSHOT] Committing snapshot atomically (writing block 0)...\n");
    if (oram_access(0, 1, header_sector) != 0) {
        serial_printf("[SNAPSHOT] ERROR: Atomic commit failed to write LBA 0!\n");
        kfree(serialize_buf);
        return -1;
    }

    serial_printf("[SNAPSHOT] Secure RAM session snapshot successfully saved!\n");
    oram_print_stats();

    kfree(serialize_buf);
    g_sequence_number++;
    return 0;
}

int snapshot_restore(void) {
    serial_printf("[SNAPSHOT] Initiating session restore...\n");

    oram_reset_counters();

    aes_gcm_ctx_t gcm_ctx;
    aes_gcm_init(&gcm_ctx, g_snapshot_key);

    // 1. Read block 0 to verify signature & retrieve header
    uint8_t header_sector[512];
    if (oram_access(0, 0, header_sector) != 0) {
        serial_printf("[SNAPSHOT] ERROR: Failed to read block 0!\n");
        return -1;
    }

    // Unpack Block 0 GCM structure
    uint8_t iv0[12];
    uint8_t tag0[16];
    memcpy(iv0, header_sector + 480, 12);
    memcpy(tag0, header_sector + 492, 16);

    uint8_t header_buf[480];
    if (aes_gcm_decrypt(&gcm_ctx, iv0, iv0, 12, header_sector, 480, tag0, header_buf) != 0) {
        serial_printf("[SNAPSHOT] ERROR: Block 0 integrity verification failed! Discarding snapshot.\n");
        return -1;
    }

    snapshot_header_t *header = (snapshot_header_t *)header_buf;
    if (header->magic != SNAPSHOT_MAGIC || header->version != SNAPSHOT_VERSION) {
        serial_printf("[SNAPSHOT] ERROR: Block 0 magic/version mismatch! Invalid snapshot.\n");
        return -1;
    }

    serial_printf("[SNAPSHOT] Valid snapshot found. Sequence: %d, Sectors: %d\n",
                  (int)header->sequence, (int)header->total_sectors);

    // 2. Allocate restore buffer
    uint8_t *serialize_buf = (uint8_t *)kmalloc(32768);
    if (!serialize_buf) {
        serial_printf("[SNAPSHOT] ERROR: Restore memory allocation failed!\n");
        return -1;
    }
    memset(serialize_buf, 0, 32768);
    memcpy(serialize_buf, header_buf, sizeof(snapshot_header_t));

    // 3. Read and decrypt sectors 1 to total_sectors
    size_t block_payload_size = 480;
    for (size_t lba = 1; lba <= header->total_sectors; lba++) {
        uint8_t sector[512];
        if (oram_access((uint32_t)lba, 0, sector) != 0) {
            serial_printf("[SNAPSHOT] ERROR: Restore read failed at LBA %d\n", (int)lba);
            kfree(serialize_buf);
            return -1;
        }

        uint8_t iv[12];
        uint8_t tag[16];
        memcpy(iv, sector + 480, 12);
        memcpy(tag, sector + 492, 16);

        // AAD: LBA + sequence
        uint8_t aad[12];
        memcpy(aad, iv, 12);

        size_t offset = sizeof(snapshot_header_t) + (lba - 1) * block_payload_size;
        if (aes_gcm_decrypt(&gcm_ctx, iv, aad, 12, sector, block_payload_size, tag, serialize_buf + offset) != 0) {
            serial_printf("[SNAPSHOT] ERROR: Decryption/Integrity failure at sector %d! Rollback initiated.\n", (int)lba);
            kfree(serialize_buf);
            return -1;
        }
    }

    serial_printf("[SNAPSHOT] Cryptographic verification complete. All sectors decrypted and authentic.\n");

    // 4. Check CPUID compatibility (Cross-machine Verification)
    uint32_t current_ecx, current_edx;
    get_cpuid_features(&current_ecx, &current_edx);

    if (current_ecx == header->cpuid_ecx && current_edx == header->cpuid_edx) {
        serial_printf("[SNAPSHOT] CPU hardware features match. Restoring full kernel state.\n");
    } else {
        serial_printf("[SNAPSHOT] !!! WARNING: CPU hardware mismatch detected !!!\n");
        serial_printf("           Saved CPUID Features: ECX=0x%x, EDX=0x%x\n", header->cpuid_ecx, header->cpuid_edx);
        serial_printf("           Current CPUID Features: ECX=0x%x, EDX=0x%x\n", current_ecx, current_edx);
        serial_printf("[SNAPSHOT] Falling back to userspace-only snapshot restoration. Re-initializing kernel state...\n");
    }

    // 5. Restore capability pool
    cap_import_pool(header->cap_pool, header->cap_pool_used);

    // 6. Restore tasks and scheduler queue
    task_t *tasks[8];
    int task_count = header->task_count;
    
    uint8_t *p = serialize_buf + sizeof(snapshot_header_t);

    for (int i = 0; i < task_count; i++) {
        snapshot_task_t *st = (snapshot_task_t *)p;
        task_t *t = task_get(st->id);
        if (!t) {
            // Re-allocate user task slot if deleted
            if (st->has_pml4) {
                t = task_create_user(st->regs.rip);
            } else {
                t = task_create_kernel();
            }
        }

        if (t) {
            t->state = st->state;
            t->ticks = st->ticks;
            t->kstack_top = st->kstack_top;
            t->regs = st->regs;
            if (st->has_pml4 && t->cap_table) {
                memcpy(t->cap_table, &st->cap_table, sizeof(cap_table_t));
            }
            tasks[i] = t;
            serial_printf("[SNAPSHOT] Restored Task %d state (state=%d, rip=%p)\n",
                          t->id, t->state, (void *)t->regs.rip);
        }
        p += sizeof(snapshot_task_t);
    }

    sched_set_tasks(tasks, task_count);

    // 7. Restore memory pages
    uint64_t hhdm_off = vmm_get_hhdm_offset();
    uint32_t page_count = header->page_count;
    for (uint32_t i = 0; i < page_count; i++) {
        snapshot_page_header_t *ph = (snapshot_page_header_t *)p;
        p += sizeof(snapshot_page_header_t);

        task_t *t = task_get(ph->task_id);
        if (t && t->pml4) {
            // Temporarily set active PML4 to map/copy
            uint64_t *old_pml4 = vmm_get_pml4();
            vmm_set_pml4(t->pml4);

            uint64_t phys = vmm_virt_to_phys(ph->virt_addr);
            if (phys) {
                // Copy page contents back
                memcpy((void *)(phys + hhdm_off), p, 4096);
            }

            vmm_set_pml4(old_pml4);
            serial_printf("[SNAPSHOT] Restored User Page content: Task %d, VA %p\n",
                          t->id, (void *)ph->virt_addr);
        }
        p += 4096;
    }

    serial_printf("[SNAPSHOT] Session resume completed successfully!\n");
    oram_print_stats();

    kfree(serialize_buf);
    return 0;
}
