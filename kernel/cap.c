#include "cap.h"
#include "serial.h"
#include "task.h"
#include "sched.h"
#include "string.h"

/*
 * cap.c — Capability engine implementation.
 *
 * M5 uses a fixed static pool for endpoint objects.
 * In M6, cap_endpoint_create() will call kmalloc() instead.
 *
 * Architecture Bible ref: §3 L4, §11 Driver Model.
 */

/* ── M5 static endpoint pool ──────────────────────────────────────────────
 *
 * 16 endpoints is more than sufficient for M5 ring-0 tests.
 * Pool management: simple linear scan (O(n), n=16 — perfectly acceptable).
 * ─────────────────────────────────────────────────────────────────────── */

#define ENDPOINT_POOL_SIZE  16

static endpoint_t ep_pool[ENDPOINT_POOL_SIZE];
static uint8_t    ep_pool_used[ENDPOINT_POOL_SIZE];  /* 0 = free, 1 = in use */

static endpoint_t *ep_pool_alloc(void) {
    for (int i = 0; i < ENDPOINT_POOL_SIZE; i++) {
        if (!ep_pool_used[i]) {
            ep_pool_used[i] = 1;
            return &ep_pool[i];
        }
    }
    return (endpoint_t *)0;
}

/* ── cap_table_init ──────────────────────────────────────────────────────── */

void cap_table_init(cap_table_t *table) {
    for (int i = 0; i < CAP_TABLE_SIZE; i++) {
        table->slots[i].type       = CAP_TYPE_NULL;
        table->slots[i].rights     = 0;
        table->slots[i].generation = 0;
        table->slots[i].object     = (void *)0;
    }
}

/* ── cap_alloc_slot ──────────────────────────────────────────────────────── */

int cap_alloc_slot(cap_table_t *table) {
    for (int i = 0; i < CAP_TABLE_SIZE; i++) {
        if (table->slots[i].type == CAP_TYPE_NULL)
            return i;
    }
    return -1;
}

/* ── cap_free_slot ───────────────────────────────────────────────────────── */

cap_err_t cap_free_slot(cap_table_t *table, uint32_t idx) {
    if (idx >= CAP_TABLE_SIZE) return CAP_ERR_BOUNDS;
    table->slots[idx].type       = CAP_TYPE_NULL;
    table->slots[idx].rights     = 0;
    table->slots[idx].generation = 0;
    table->slots[idx].object     = (void *)0;
    return CAP_OK;
}

/* ── cap_lookup ──────────────────────────────────────────────────────────── */

cap_err_t cap_lookup(cap_table_t *table, uint32_t idx,
                     cap_type_t expected_type, uint32_t required_rights,
                     cap_slot_t **out) {
    *out = (cap_slot_t *)0;

    /* 1. Bounds check */
    if (idx >= CAP_TABLE_SIZE)
        return CAP_ERR_BOUNDS;

    cap_slot_t *slot = &table->slots[idx];

    /* 2. Null check */
    if (slot->type == CAP_TYPE_NULL)
        return CAP_ERR_NULL;

    /* 3. Type check */
    if (slot->type != expected_type)
        return CAP_ERR_TYPE;

    /* 4. Generation check (ENDPOINT type only in M5) */
    if (slot->type == CAP_TYPE_ENDPOINT) {
        endpoint_t *ep = (endpoint_t *)slot->object;
        if (ep->generation != slot->generation)
            return CAP_ERR_STALE;
    }

    /* 5. Rights check — caller must hold ALL required bits */
    if ((slot->rights & required_rights) != required_rights)
        return CAP_ERR_ACCESS;

    *out = slot;
    return CAP_OK;
}

/* ── cap_derive ──────────────────────────────────────────────────────────── */

cap_err_t cap_derive(cap_table_t *table,
                     uint32_t src_idx, uint32_t dst_idx,
                     uint32_t child_rights) {
    if (src_idx >= CAP_TABLE_SIZE || dst_idx >= CAP_TABLE_SIZE)
        return CAP_ERR_BOUNDS;

    cap_slot_t *src = &table->slots[src_idx];

    /* Source must be a live, non-NULL slot */
    if (src->type == CAP_TYPE_NULL)
        return CAP_ERR_NULL;

    /* Generation check on source */
    if (src->type == CAP_TYPE_ENDPOINT) {
        endpoint_t *ep = (endpoint_t *)src->object;
        if (ep->generation != src->generation)
            return CAP_ERR_STALE;
    }

    /* No rights escalation — child_rights must be a subset of parent's rights */
    if ((child_rights & ~src->rights) != 0)
        return CAP_ERR_RIGHTS;

    /* Destination slot must be free */
    if (table->slots[dst_idx].type != CAP_TYPE_NULL)
        return CAP_ERR_OCCUPIED;

    /* Mint the child capability */
    table->slots[dst_idx].type       = src->type;
    table->slots[dst_idx].rights     = child_rights;
    table->slots[dst_idx].generation = src->generation;
    table->slots[dst_idx].object     = src->object;

    return CAP_OK;
}

/* ── cap_endpoint_create ─────────────────────────────────────────────────── */

cap_err_t cap_endpoint_create(cap_table_t *table, uint32_t slot_idx,
                              uint32_t rights, uint32_t owner_task) {
    if (slot_idx >= CAP_TABLE_SIZE)
        return CAP_ERR_BOUNDS;

    if (table->slots[slot_idx].type != CAP_TYPE_NULL)
        return CAP_ERR_OCCUPIED;

    endpoint_t *ep = ep_pool_alloc();
    if (!ep)
        return CAP_ERR_POOL;

    endpoint_init(ep, owner_task);

    table->slots[slot_idx].type       = CAP_TYPE_ENDPOINT;
    table->slots[slot_idx].rights     = rights;
    table->slots[slot_idx].generation = ep->generation;   /* 0 on fresh init */
    table->slots[slot_idx].object     = (void *)ep;

    return CAP_OK;
}

/* ── cap_revoke_endpoint ─────────────────────────────────────────────────── */

cap_err_t cap_revoke_endpoint(cap_table_t *table, uint32_t slot_idx) {
    if (slot_idx >= CAP_TABLE_SIZE)
        return CAP_ERR_BOUNDS;

    cap_slot_t *slot = &table->slots[slot_idx];

    if (slot->type != CAP_TYPE_ENDPOINT)
        return CAP_ERR_TYPE;

    endpoint_t *ep = (endpoint_t *)slot->object;

    /*
     * Bump the endpoint's generation counter.
     * slot->generation still holds the OLD value — it is now stale.
     * Every subsequent cap_lookup() on this slot (or any other slot
     * pointing to this endpoint) will see the mismatch and return
     * CAP_ERR_STALE. O(1) invalidation — no slot enumeration needed.
     */
    endpoint_revoke(ep);

    return CAP_OK;
}

/* ── cap_send ────────────────────────────────────────────────────────────── */

cap_err_t cap_send(cap_table_t *table, uint32_t slot_idx, ipc_msg_t *msg) {
    cap_slot_t *slot;
    cap_err_t err = cap_lookup(table, slot_idx,
                               CAP_TYPE_ENDPOINT, CAP_RIGHT_SEND, &slot);
    if (err != CAP_OK) return err;

    endpoint_t *ep = (endpoint_t *)slot->object;

    if (ep->state == EP_STATE_RECV_WAITING) {
        /*
         * Receiver is parked and waiting. Deliver immediately:
         * copy our message into their output buffer, unpark them.
         */
        if (ep->pending_msg)
            *ep->pending_msg = *msg;
        ep->state       = EP_STATE_IDLE;
        ep->pending_msg = (ipc_msg_t *)0;

        uint32_t rx_task_id = ep->parked_task_id;
        ep->parked_task_id = 0;
        if (rx_task_id != 0) {
            task_t *rx_task = task_get(rx_task_id);
            if (rx_task) {
                sched_unblock(rx_task);
            }
        }
    } else if (ep->state == EP_STATE_IDLE) {
        /*
         * No receiver yet. Park the sender: store message pointer
         * and set SEND_WAITING.
         */
        ep->state       = EP_STATE_SEND_WAITING;
        ep->pending_msg = msg;

        task_t *current = sched_get_current();
        if (current && current->id != 0) {
            ep->parked_task_id = current->id;
            sched_block(current);
        } else {
            ep->parked_task_id = 0;
        }
    } else {
        /* Another sender is already parked — endpoint busy */
        return CAP_ERR_STATE;
    }

    return CAP_OK;
}

/* ── cap_recv ────────────────────────────────────────────────────────────── */

cap_err_t cap_recv(cap_table_t *table, uint32_t slot_idx, ipc_msg_t *out) {
    cap_slot_t *slot;
    cap_err_t err = cap_lookup(table, slot_idx,
                               CAP_TYPE_ENDPOINT, CAP_RIGHT_RECV, &slot);
    if (err != CAP_OK) return err;

    endpoint_t *ep = (endpoint_t *)slot->object;

    if (ep->state == EP_STATE_SEND_WAITING) {
        /*
         * Sender is parked. Consume their message and unpark them.
         */
        if (ep->pending_msg)
            *out = *ep->pending_msg;
        ep->state       = EP_STATE_IDLE;
        ep->pending_msg = (ipc_msg_t *)0;

        uint32_t tx_task_id = ep->parked_task_id;
        ep->parked_task_id = 0;
        if (tx_task_id != 0) {
            task_t *tx_task = task_get(tx_task_id);
            if (tx_task) {
                sched_unblock(tx_task);
            }
        }
    } else if (ep->state == EP_STATE_IDLE) {
        /*
         * No sender yet. Park the receiver.
         */
        ep->state       = EP_STATE_RECV_WAITING;
        ep->pending_msg = out;

        task_t *current = sched_get_current();
        if (current && current->id != 0) {
            ep->parked_task_id = current->id;
            sched_block(current);
        } else {
            ep->parked_task_id = 0;
        }
    } else {
        /* Another receiver is already parked */
        return CAP_ERR_STATE;
    }

    return CAP_OK;
}

/* ── cap_err_str ─────────────────────────────────────────────────────────── */

const char *cap_err_str(cap_err_t err) {
    switch (err) {
        case CAP_OK:           return "CAP_OK";
        case CAP_ERR_NULL:     return "CAP_ERR_NULL";
        case CAP_ERR_ACCESS:   return "CAP_ERR_ACCESS";
        case CAP_ERR_RIGHTS:   return "CAP_ERR_RIGHTS";
        case CAP_ERR_STALE:    return "CAP_ERR_STALE";
        case CAP_ERR_FULL:     return "CAP_ERR_FULL";
        case CAP_ERR_BOUNDS:   return "CAP_ERR_BOUNDS";
        case CAP_ERR_TYPE:     return "CAP_ERR_TYPE";
        case CAP_ERR_STATE:    return "CAP_ERR_STATE";
        case CAP_ERR_POOL:     return "CAP_ERR_POOL";
        case CAP_ERR_OCCUPIED: return "CAP_ERR_OCCUPIED";
        default:               return "CAP_ERR_UNKNOWN";
    }
}

void cap_export_pool(void *dest_pool, void *dest_used) {
    memcpy(dest_pool, ep_pool, sizeof(ep_pool));
    memcpy(dest_used, ep_pool_used, sizeof(ep_pool_used));
}

void cap_import_pool(const void *src_pool, const void *src_used) {
    memcpy(ep_pool, src_pool, sizeof(ep_pool));
    memcpy(ep_pool_used, src_used, sizeof(ep_pool_used));
}
