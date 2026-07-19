#pragma once

#include <stdint.h>
#include "ipc.h"

/*
 * endpoint.h — Kernel-managed IPC endpoint object.
 *
 * Endpoints are never exposed directly to tasks. Access is only possible
 * through a validated capability slot that holds the endpoint's address
 * and a matching generation counter.
 *
 * Generation counter enables O(1) revocation:
 *   endpoint_revoke() bumps generation → every slot holding the old
 *   generation fails cap_lookup() with CAP_ERR_STALE immediately.
 *   No slot enumeration required.
 *
 * M5: endpoints allocated from static pool (endpoint_pool[] in cap.c).
 * M6: endpoints allocated via kmalloc() — same struct, different allocator.
 *
 * Architecture Bible ref: §3 L4 — Process Isolation, §11 Driver Model
 */

typedef enum {
    EP_STATE_IDLE          = 0,  /* no pending operation */
    EP_STATE_SEND_WAITING  = 1,  /* sender parked; pending_msg points to their msg */
    EP_STATE_RECV_WAITING  = 2,  /* receiver parked; pending_msg points to their buf */
} ep_state_t;

typedef struct {
    uint64_t    generation;   /* bumped on every revocation — stale caps fail instantly */
    ep_state_t  state;        /* current blocking state */
    ipc_msg_t  *pending_msg;  /* pointer to in-flight msg (caller's stack or heap buf) */
    uint32_t    owner_task;   /* task ID of creator; 0 = kernel (M5 tests) */
    uint32_t    parked_task_id; /* task ID of currently parked sender/receiver */
} endpoint_t;

/*
 * endpoint_init() — zero-initialize a freshly-allocated endpoint.
 * generation=0, state=IDLE, pending_msg=NULL.
 */
void endpoint_init(endpoint_t *ep, uint32_t owner_task);

/*
 * endpoint_revoke() — bump generation, clear state.
 * All capability slots holding the prior generation are now stale.
 */
void endpoint_revoke(endpoint_t *ep);
