#pragma once

#include <stdint.h>
#include "ipc.h"
#include "endpoint.h"

/*
 * cap.h — Capability-based access control engine.
 *
 * Architecture Bible guarantee (§3, Layer L4 — Process Isolation):
 *   "Every process holds a cryptographic capability token.
 *    No token = no execution. Tokens define exactly which memory regions,
 *    devices, and syscalls a process may access.
 *    No ambient authority exists."
 *
 * Design properties enforced here:
 *   ① No ambient authority   — a task with no slot for a resource cannot name it.
 *   ② O(1) revocation        — bump the object's generation; all stale slots die.
 *   ③ No rights escalation   — cap_derive() enforces child_rights ⊆ parent_rights.
 *   ④ Synchronous IPC only   — cap_send / cap_recv. No shared memory without a cap.
 *
 * M5 scope: ring-0 sanity tests only. No scheduler, no userspace, no syscall gate.
 *           Static endpoint pool (ENDPOINT_POOL_SIZE slots in cap.c).
 * M6 scope: real task structs, heap-allocated tables, syscall interface.
 */

/* ── Rights bitfield ───────────────────────────────────────────────────── */

#define CAP_RIGHT_SEND    (1u << 0)   /* may send messages to this endpoint   */
#define CAP_RIGHT_RECV    (1u << 1)   /* may receive messages from endpoint   */
#define CAP_RIGHT_GRANT   (1u << 2)   /* may transfer cap to another task     */
#define CAP_RIGHT_DERIVE  (1u << 3)   /* may mint restricted child caps       */
#define CAP_RIGHT_ALL     (CAP_RIGHT_SEND | CAP_RIGHT_RECV | \
                           CAP_RIGHT_GRANT | CAP_RIGHT_DERIVE)

/* ── Capability types ──────────────────────────────────────────────────── */

typedef enum {
    CAP_TYPE_NULL     = 0,   /* empty / revoked slot         */
    CAP_TYPE_ENDPOINT,       /* IPC endpoint object          */
    CAP_TYPE_MEMORY,         /* memory region   (M6+)        */
    CAP_TYPE_IRQ,            /* hardware IRQ line (M7+)      */
    CAP_TYPE_DEVPORT,        /* I/O port range  (M7+)        */
} cap_type_t;

/* ── Error codes ───────────────────────────────────────────────────────── */

typedef enum {
    CAP_OK           = 0,
    CAP_ERR_NULL,        /* slot is empty / type is NULL                    */
    CAP_ERR_ACCESS,      /* required right not present in slot               */
    CAP_ERR_RIGHTS,      /* rights escalation: child_rights ⊄ parent_rights  */
    CAP_ERR_STALE,       /* generation mismatch — underlying object revoked   */
    CAP_ERR_FULL,        /* table has no free slots                          */
    CAP_ERR_BOUNDS,      /* slot index out of [0, CAP_TABLE_SIZE)            */
    CAP_ERR_TYPE,        /* slot type does not match expected type           */
    CAP_ERR_STATE,       /* endpoint in wrong state for operation            */
    CAP_ERR_POOL,        /* M5 static endpoint pool exhausted                */
    CAP_ERR_OCCUPIED,    /* destination slot already in use                  */
} cap_err_t;

/* ── Capability slot ───────────────────────────────────────────────────── */

/*
 * A capability slot is a typed, generation-tagged reference to a kernel object.
 * It lives inside a task's cap_table_t. Tasks never hold raw pointers.
 *
 * generation: copied from the object at creation time. cap_lookup() compares
 *             slot->generation with object->generation. Mismatch → CAP_ERR_STALE.
 */
typedef struct {
    cap_type_t  type;
    uint32_t    rights;       /* bitfield of CAP_RIGHT_* this slot grants  */
    uint64_t    generation;   /* snapshot of the object's generation       */
    void       *object;       /* kernel-managed object (endpoint_t, etc.)  */
} cap_slot_t;

/* ── Capability table ──────────────────────────────────────────────────── */

/*
 * 64 slots per task is sufficient through M12 (curated minimal userspace).
 * seL4's radix tree is overkill for StyxOS's threat model and user base.
 * M6 will heap-allocate cap_table_t per task via kmalloc().
 */
#define CAP_TABLE_SIZE  64

typedef struct {
    cap_slot_t slots[CAP_TABLE_SIZE];
} cap_table_t;

/* ══════════════════════════════════════════════════════════════════════════
 * Public API
 * ══════════════════════════════════════════════════════════════════════════ */

/*
 * cap_table_init() — zero-initialize every slot to CAP_TYPE_NULL.
 * Must be called before any other cap_* operation on the table.
 */
void cap_table_init(cap_table_t *table);

/*
 * cap_alloc_slot() — return the index of the first free slot, or -1 if full.
 * Does not mark the slot as used — caller fills it via cap_endpoint_create()
 * or cap_derive().
 */
int cap_alloc_slot(cap_table_t *table);

/*
 * cap_free_slot() — zero the slot at idx (revoke this task's capability).
 * Does NOT revoke the underlying object — other tasks may still hold caps.
 * To invalidate ALL caps to an endpoint, call cap_revoke_endpoint() instead.
 */
cap_err_t cap_free_slot(cap_table_t *table, uint32_t idx);

/*
 * cap_lookup() — validate and return a pointer to a slot.
 *
 * Checks in order:
 *   1. idx within [0, CAP_TABLE_SIZE)                → CAP_ERR_BOUNDS
 *   2. slot->type != CAP_TYPE_NULL                   → CAP_ERR_NULL
 *   3. slot->type == expected_type                   → CAP_ERR_TYPE
 *   4. generation match (ENDPOINT only in M5)        → CAP_ERR_STALE
 *   5. (slot->rights & required_rights)==required    → CAP_ERR_ACCESS
 *
 * On CAP_OK, *out points to the validated slot. On any error, *out = NULL.
 */
cap_err_t cap_lookup(cap_table_t *table, uint32_t idx,
                     cap_type_t expected_type, uint32_t required_rights,
                     cap_slot_t **out);

/*
 * cap_derive() — mint a restricted child capability at dst_idx.
 *
 * child_rights must be a subset of src_slot->rights.
 * Any bit in child_rights not present in parent → CAP_ERR_RIGHTS.
 * dst_idx must be a free (NULL) slot → CAP_ERR_OCCUPIED otherwise.
 */
cap_err_t cap_derive(cap_table_t *table,
                     uint32_t src_idx, uint32_t dst_idx,
                     uint32_t child_rights);

/*
 * cap_endpoint_create() — allocate an endpoint from the static pool,
 * initialize it, and bind it to slot_idx with the given rights.
 *
 * slot_idx must be a NULL slot. owner_task = 0 for kernel/M5 tests.
 */
cap_err_t cap_endpoint_create(cap_table_t *table, uint32_t slot_idx,
                              uint32_t rights, uint32_t owner_task);

/*
 * cap_revoke_endpoint() — bump the endpoint's generation counter.
 *
 * The slot at slot_idx is NOT zeroed — it remains as a now-stale reference.
 * Every subsequent cap_lookup() on ANY slot pointing to this endpoint
 * (across all tasks) will fail with CAP_ERR_STALE. O(1) invalidation.
 *
 * To also release this task's slot, call cap_free_slot() after this.
 */
cap_err_t cap_revoke_endpoint(cap_table_t *table, uint32_t slot_idx);

/*
 * cap_send() — synchronous IPC send.
 *
 * Requires CAP_RIGHT_SEND on slot at slot_idx (must be CAP_TYPE_ENDPOINT).
 *
 * Rendezvous logic:
 *   EP_STATE_RECV_WAITING → deliver immediately, endpoint → IDLE.
 *   otherwise             → store msg ptr, endpoint → SEND_WAITING.
 *                           (M6: sender task blocks. M5: returns to caller.)
 */
cap_err_t cap_send(cap_table_t *table, uint32_t slot_idx, ipc_msg_t *msg);

/*
 * cap_recv() — synchronous IPC receive.
 *
 * Requires CAP_RIGHT_RECV on slot at slot_idx (must be CAP_TYPE_ENDPOINT).
 *
 * Rendezvous logic:
 *   EP_STATE_SEND_WAITING → consume pending msg into *out, endpoint → IDLE.
 *   otherwise             → store out ptr, endpoint → RECV_WAITING.
 *                           (M6: receiver task blocks. M5: returns to caller.)
 */
cap_err_t cap_recv(cap_table_t *table, uint32_t slot_idx, ipc_msg_t *out);

/*
 * cap_err_str() — human-readable name for a cap_err_t value.
 * Never returns NULL.
 */
const char *cap_err_str(cap_err_t err);

void cap_export_pool(void *dest_pool, void *dest_used);
void cap_import_pool(const void *src_pool, const void *src_used);
