#include "endpoint.h"

void endpoint_init(endpoint_t *ep, uint32_t owner_task) {
    ep->generation     = 0;
    ep->state          = EP_STATE_IDLE;
    ep->pending_msg    = (ipc_msg_t *)0;
    ep->owner_task     = owner_task;
    ep->parked_task_id = 0;
}

void endpoint_revoke(endpoint_t *ep) {
    ep->generation++;          /* invalidates all slots holding old generation */
    ep->state          = EP_STATE_IDLE;
    ep->pending_msg    = (ipc_msg_t *)0;
    ep->parked_task_id = 0;
}
