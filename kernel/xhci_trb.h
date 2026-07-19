#pragma once

/*
 * xhci_trb.h — Transfer Request Block (TRB) definitions for xHCI.
 *
 * All TRBs are 16 bytes. The cycle bit (bit 0 of dword3) tracks ring ownership.
 * Producer sets cycle = PCS (Producer Cycle State); controller owns when cycle
 * matches its Consumer Cycle State (CCS).
 *
 * Architecture Bible ref: §9 M7, §11 Driver Model
 */

#include <stdint.h>

/* ── TRB type codes (bits [15:10] of dword3) ─────────────────────────────── */

#define TRB_TYPE_NORMAL           1
#define TRB_TYPE_SETUP_STAGE      2
#define TRB_TYPE_DATA_STAGE       3
#define TRB_TYPE_STATUS_STAGE     4
#define TRB_TYPE_ISOCH            5
#define TRB_TYPE_LINK             6
#define TRB_TYPE_EVENT_DATA       7
#define TRB_TYPE_NOOP             8
/* Command TRB types */
#define TRB_TYPE_ENABLE_SLOT      9
#define TRB_TYPE_DISABLE_SLOT    10
#define TRB_TYPE_ADDRESS_DEVICE  11
#define TRB_TYPE_CONFIG_EP       12
#define TRB_TYPE_EVALUATE_CTX    13
#define TRB_TYPE_NOOP_CMD        23
/* Event TRB types */
#define TRB_TYPE_TRANSFER_EVT    32
#define TRB_TYPE_CMD_COMPLETE    33
#define TRB_TYPE_PORT_STATUS     34

/* ── Completion codes (bits [31:24] of event TRB dword2) ─────────────────── */

#define TRB_CC_SUCCESS           1
#define TRB_CC_DATA_BUFFER_ERR   2
#define TRB_CC_BABBLE_ERR        3
#define TRB_CC_USB_TRANS_ERR     4
#define TRB_CC_TRB_ERR           5
#define TRB_CC_STALL_ERR         6
#define TRB_CC_SHORT_PACKET     13

/* ── Generic TRB ─────────────────────────────────────────────────────────── */

typedef struct __attribute__((packed)) {
    uint32_t dword0;
    uint32_t dword1;
    uint32_t dword2;
    uint32_t dword3;
} xhci_trb_t;

/* Build dword3: type + cycle bit */
#define TRB_DWORD3(type, cycle) \
    (((uint32_t)(type) << 10) | ((cycle) & 1))

/* ── Setup Stage TRB (control transfers) ─────────────────────────────────── */
/* bmRequestType, bRequest, wValue, wIndex, wLength packed in dword0/1 */
#define TRB_SETUP_TRT_NO_DATA  0
#define TRB_SETUP_TRT_IN       3
#define TRB_SETUP_TRT_OUT      2

/* ── Normal TRB flags (dword3) ───────────────────────────────────────────── */
#define TRB_FLAG_IOC   (1 << 5)   /* Interrupt On Completion */
#define TRB_FLAG_ISP   (1 << 2)   /* Interrupt on Short Packet */
#define TRB_FLAG_ENT   (1 << 1)   /* Evaluate Next TRB */

/* ── Link TRB (wraps ring back to start) ────────────────────────────────── */
#define TRB_LINK_FLAG_TC (1 << 1) /* Toggle Cycle */

/* ── Ring helpers ────────────────────────────────────────────────────────── */

#define XHCI_RING_SIZE  64   /* TRBs per ring (last one is a Link TRB) */

typedef struct {
    xhci_trb_t  trbs[XHCI_RING_SIZE];
    uint32_t    enqueue;   /* next slot to write */
    uint8_t     pcs;       /* Producer Cycle State (toggled on wrap) */
} xhci_ring_t;

/* Initialise a ring: zero TRBs, set up Link TRB at end, PCS=1 */
static inline void xhci_ring_init(xhci_ring_t *r, uint64_t phys_base) {
    for (int i = 0; i < XHCI_RING_SIZE; i++) {
        r->trbs[i].dword0 = 0;
        r->trbs[i].dword1 = 0;
        r->trbs[i].dword2 = 0;
        r->trbs[i].dword3 = 0;
    }
    /* Link TRB at slot XHCI_RING_SIZE-1 */
    int link = XHCI_RING_SIZE - 1;
    r->trbs[link].dword0 = (uint32_t)(phys_base & 0xFFFFFFFF);
    r->trbs[link].dword1 = (uint32_t)(phys_base >> 32);
    r->trbs[link].dword2 = 0;
    r->trbs[link].dword3 = TRB_DWORD3(TRB_TYPE_LINK, 1) | TRB_LINK_FLAG_TC;
    r->enqueue = 0;
    r->pcs     = 1;
}
