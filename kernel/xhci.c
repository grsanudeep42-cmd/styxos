/*
 * xhci.c — xHCI Host Controller Driver (ring-0, poll-based, M7)
 */
#include "xhci.h"
#include "xhci_trb.h"
#include "pci.h"
#include "vmm.h"
#include "pmm.h"
#include "serial.h"
#include "heap.h"
#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

/* ── MMIO helpers ────────────────────────────────────────────────────────── */
static uint64_t g_mmio;  /* kernel virtual base of xHCI MMIO */
static uint32_t g_op;    /* offset to operational registers  */
static uint32_t g_rt;    /* offset to runtime registers      */
static uint32_t g_db;    /* offset to doorbell array         */

static inline uint32_t cap_rd(uint32_t off)
    { return *(volatile uint32_t*)(g_mmio + off); }
static inline uint32_t op_rd(uint32_t off)
    { return *(volatile uint32_t*)(g_mmio + g_op + off); }
static inline void op_wr(uint32_t off, uint32_t v)
    { *(volatile uint32_t*)(g_mmio + g_op + off) = v; }
static inline uint32_t rt_rd(uint32_t off)
    { return *(volatile uint32_t*)(g_mmio + g_rt + off); }
static inline void rt_wr(uint32_t off, uint32_t v)
    { *(volatile uint32_t*)(g_mmio + g_rt + off) = v; }
static inline void db_wr(uint8_t slot, uint32_t v)
    { *(volatile uint32_t*)(g_mmio + g_db + slot*4) = v; }

/* ── Spin-wait helpers ───────────────────────────────────────────────────── */
static bool wait_clear(uint32_t off, uint32_t mask, uint32_t timeout_ms) {
    for (uint32_t i = 0; i < timeout_ms * 1000; i++) {
        if (!(op_rd(off) & mask)) return true;
        for (volatile int d = 0; d < 100; d++);
    }
    return false;
}
static bool wait_set(uint32_t off, uint32_t mask, uint32_t timeout_ms) {
    for (uint32_t i = 0; i < timeout_ms * 1000; i++) {
        if (op_rd(off) & mask) return true;
        for (volatile int d = 0; d < 100; d++);
    }
    return false;
}

/* ── Physical page allocator (zeroed) ───────────────────────────────────── */
static void *alloc_page_virt(uint64_t *phys_out) {
    void *p = pmm_alloc_frame();
    if (!p) return NULL;
    *phys_out = (uint64_t)p;
    uint64_t hhdm = vmm_get_hhdm_offset();
    uint8_t *v = (uint8_t*)(hhdm + (uint64_t)p);
    for (int i = 0; i < 4096; i++) v[i] = 0;
    return v;
}

/* ── Global state ────────────────────────────────────────────────────────── */

static uint64_t  *g_dcbaa;
static uint64_t   g_dcbaa_phys;

/* All rings are PMM-allocated — real physical addresses for xHCI DMA */
static xhci_ring_t *g_cmd_ring;
static uint64_t     g_cmd_ring_phys;

static xhci_trb_t  *g_evt_ring;
static uint64_t     g_evt_ring_phys;
static uint64_t    *g_erst;
static uint64_t     g_erst_phys;
static uint32_t     g_evt_dequeue;
static uint8_t      g_evt_ccs;

static xhci_ring_t *g_xfer_ep0;
static uint64_t     g_xfer_ep0_phys;
static xhci_ring_t *g_xfer_bulk_in;
static uint64_t     g_xfer_bulk_in_phys;
static xhci_ring_t *g_xfer_bulk_out;
static uint64_t     g_xfer_bulk_out_phys;

static uint8_t  *g_out_ctx;
static uint64_t  g_out_ctx_phys;
static uint8_t  *g_in_ctx;
static uint64_t  g_in_ctx_phys;

static xhci_device_t g_msd;

/* ── Ring allocator: one PMM page → zeroed → init Link TRB ──────────────── */
static xhci_ring_t *alloc_ring(uint64_t *phys_out) {
    xhci_ring_t *r = (xhci_ring_t *)alloc_page_virt(phys_out);
    if (r) xhci_ring_init(r, *phys_out);
    return r;
}

/* ── Ring enqueue ────────────────────────────────────────────────────────── */
static void ring_enqueue(xhci_ring_t *r, uint64_t phys,
                         uint32_t d0, uint32_t d1, uint32_t d2, uint32_t d3_nocy) {
    uint32_t idx = r->enqueue;
    r->trbs[idx].dword0 = d0;
    r->trbs[idx].dword1 = d1;
    r->trbs[idx].dword2 = d2;
    __asm__ volatile("" ::: "memory");
    r->trbs[idx].dword3 = d3_nocy | r->pcs;
    r->enqueue++;
    if (r->enqueue >= XHCI_RING_SIZE - 1) {
        /* Update Link TRB cycle bit and wrap */
        int link = XHCI_RING_SIZE - 1;
        __asm__ volatile("" ::: "memory");
        r->trbs[link].dword3 = TRB_DWORD3(TRB_TYPE_LINK, r->pcs) | TRB_LINK_FLAG_TC;
        r->pcs ^= 1;
        r->enqueue = 0;
    }
    (void)phys;
}

/* ── Consume one event from the event ring ────────────────────────────────── */
/* Returns true if an event was consumed, fills type/cc/slot. */
static bool consume_event(uint8_t *type_out, uint8_t *cc_out, uint8_t *slot_out) {
    xhci_trb_t *evt = &g_evt_ring[g_evt_dequeue];
    if ((evt->dword3 & 1) != g_evt_ccs)
        return false; /* no new event */

    uint8_t type = (evt->dword3 >> 10) & 0x3F;
    uint8_t cc   = (evt->dword2 >> 24) & 0xFF;
    uint8_t slot = (evt->dword3 >> 24) & 0xFF;
    if (type_out)  *type_out  = type;
    if (cc_out)    *cc_out    = cc;
    if (slot_out)  *slot_out  = slot;

    serial_printf("[XHCI EVT] dequeue=%d ccs=%d type=%d cc=%d slot=%d ptr=0x%x%08x\n",
                  g_evt_dequeue, g_evt_ccs, type, cc, slot, evt->dword1, evt->dword0);

    g_evt_dequeue++;
    if (g_evt_dequeue >= XHCI_EVENT_RING_SIZE) {
        g_evt_dequeue = 0;
        g_evt_ccs ^= 1;
    }
    /* Update ERDP — clear EHB (bit 3) to acknowledge */
    uint64_t erdp = g_evt_ring_phys + g_evt_dequeue * sizeof(xhci_trb_t);
    uint32_t ir   = g_rt + 0x20;
    *(volatile uint32_t*)(g_mmio + ir + 0x18) = (uint32_t)(erdp & 0xFFFFFFFF) | (1<<3);
    *(volatile uint32_t*)(g_mmio + ir + 0x1C) = (uint32_t)(erdp >> 32);
    return true;
}

/* ── Wait for a Command Completion Event (type 33) ────────────────────────── */
static bool wait_cmd_complete(uint8_t *cc_out, uint8_t *slot_out) {
    for (int i = 0; i < 2000000; i++) {
        uint8_t type = 0, cc = 0, slot = 0;
        if (consume_event(&type, &cc, &slot)) {
            if (type == TRB_TYPE_CMD_COMPLETE) {
                if (cc_out)   *cc_out   = cc;
                if (slot_out) *slot_out = slot;
                return cc == TRB_CC_SUCCESS;
            }
            /* else: discard Port Status Change or other events, keep polling */
        }
        for (volatile int d = 0; d < 10; d++);
    }
    serial_printf("[XHCI] wait_cmd_complete: timeout\n");
    return false;
}

/* ── Wait for a Transfer Event (type 32) ─────────────────────────────────── */
static bool wait_transfer_complete(void) {
    for (int i = 0; i < 2000000; i++) {
        uint8_t type = 0, cc = 0, slot = 0;
        if (consume_event(&type, &cc, &slot)) {
            if (type == TRB_TYPE_TRANSFER_EVT) {
                return cc == TRB_CC_SUCCESS || cc == TRB_CC_SHORT_PACKET;
            }
        }
        for (volatile int d = 0; d < 10; d++);
    }
    serial_printf("[XHCI] wait_transfer_complete: timeout\n");
    return false;
}

/* ── Issue a command TRB ─────────────────────────────────────────────────── */
static bool issue_cmd(uint32_t d0, uint32_t d1, uint32_t d2, uint32_t d3_nocy,
                      uint8_t *slot_out) {
    ring_enqueue(g_cmd_ring, g_cmd_ring_phys, d0, d1, d2, d3_nocy);
    db_wr(0, 0); /* ring host controller doorbell */
    uint8_t cc = 0, slot = 0;
    bool ok = wait_cmd_complete(&cc, &slot);
    if (slot_out) *slot_out = slot;
    return ok;
}

/* ── Port reset ──────────────────────────────────────────────────────────── */
static void port_reset(uint8_t port) {
    uint32_t portsc = *(volatile uint32_t*)(g_mmio + g_op + XHCI_PORT_BASE + port*16);
    portsc |= PORTSC_PR;
    portsc &= ~(PORTSC_PRC | PORTSC_CSC);
    *(volatile uint32_t*)(g_mmio + g_op + XHCI_PORT_BASE + port*16) = portsc;
    /* Wait for reset to complete */
    for (int i = 0; i < 100000; i++) {
        portsc = *(volatile uint32_t*)(g_mmio + g_op + XHCI_PORT_BASE + port*16);
        if (!(portsc & PORTSC_PR)) break;
        for (volatile int d = 0; d < 100; d++);
    }
}

/* ── xhci_control ────────────────────────────────────────────────────────── */
int xhci_control(uint8_t slot, uint8_t bmReqType, uint8_t bReq,
                 uint16_t wValue, uint16_t wIndex, uint16_t wLength,
                 void *buf, size_t len) {
    xhci_ring_t *r = g_xfer_ep0;
    bool in = (bmReqType & 0x80) != 0;

    /* Setup Stage TRB — IDT (Immediate Data Transfer, bit 6) must be set */
    uint32_t setup_d0 = bmReqType | ((uint32_t)bReq << 8) | ((uint32_t)wValue << 16);
    uint32_t setup_d1 = wIndex | ((uint32_t)wLength << 16);
    uint32_t trt = len ? (in ? TRB_SETUP_TRT_IN : TRB_SETUP_TRT_OUT) : TRB_SETUP_TRT_NO_DATA;
    ring_enqueue(r, g_xfer_ep0_phys, setup_d0, setup_d1, 8,
                 TRB_DWORD3(TRB_TYPE_SETUP_STAGE, 0) | (trt << 16) | TRB_FLAG_IOC | (1 << 6));

    if (len && buf) {
        uint64_t phys = vmm_virt_to_phys((uint64_t)buf);
        uint32_t dir = in ? (1<<16) : 0;
        ring_enqueue(r, g_xfer_ep0_phys,
                     (uint32_t)(phys & 0xFFFFFFFF),
                     (uint32_t)(phys >> 32),
                     (uint32_t)len,
                     TRB_DWORD3(TRB_TYPE_DATA_STAGE, 0) | dir | TRB_FLAG_IOC);
    }

    /* Status Stage TRB */
    uint32_t status_dir = (len && !in) ? (1<<16) : 0;
    ring_enqueue(r, g_xfer_ep0_phys, 0, 0, 0,
                 TRB_DWORD3(TRB_TYPE_STATUS_STAGE, 0) | status_dir | TRB_FLAG_IOC);

    db_wr(slot, 1); /* doorbell EP0 = 1 */
    return wait_transfer_complete() ? 0 : -1;
}

/* ── xhci_bulk_out / xhci_bulk_in ───────────────────────────────────────── */
static int bulk_xfer(xhci_ring_t *r, uint64_t r_phys, uint8_t ep_id,
                     void *buf, size_t len) {
    if (!r || !r_phys) return -1;
    uint64_t phys = vmm_virt_to_phys((uint64_t)buf);
    ring_enqueue(r, r_phys,
                 (uint32_t)(phys & 0xFFFFFFFF),
                 (uint32_t)(phys >> 32),
                 (uint32_t)len,
                 TRB_DWORD3(TRB_TYPE_NORMAL, 0) | TRB_FLAG_IOC | TRB_FLAG_ISP);
    db_wr(g_msd.slot_id, ep_id);
    return wait_transfer_complete() ? 0 : -1;
}


int xhci_bulk_out(const void *buf, size_t len) {
    return bulk_xfer(g_xfer_bulk_out, g_xfer_bulk_out_phys,
                     g_msd.bulk_out_ep * 2, (void*)buf, len);
}

int xhci_bulk_in(void *buf, size_t len) {
    return bulk_xfer(g_xfer_bulk_in, g_xfer_bulk_in_phys,
                     g_msd.bulk_in_ep * 2 + 1, buf, len);
}

xhci_device_t *xhci_get_msd(void) {
    return g_msd.present ? &g_msd : NULL;
}

/* ── xhci_init ───────────────────────────────────────────────────────────── */
bool xhci_init(void) {
    /* 1. Find xHCI via PCI */
    pci_device_t pdev;
    if (!pci_find_xhci(&pdev)) return false;
    pci_enable_device(&pdev);

    /* 2. Map xHCI MMIO BAR into kernel address space.
     *    The HHDM covers RAM — device MMIO must be explicitly mapped.
     *    We map it at (hhdm + bar0) using PTE_PRESENT|PTE_WRITABLE (no PTE_USER).
     *    bar0_size is rounded up to 4 KB pages. */
    uint64_t hhdm = vmm_get_hhdm_offset();
    uint32_t bar_pages = (pdev.bar0_size + 0xFFF) / 0x1000;
    if (bar_pages == 0) bar_pages = 4; /* minimum 4 pages if size unknown */
    for (uint32_t pg = 0; pg < bar_pages; pg++) {
        uint64_t phys = pdev.bar0 + (uint64_t)pg * 0x1000;
        uint64_t virt = hhdm + phys;
        vmm_map_page(virt, phys, PTE_PRESENT | PTE_WRITABLE);
    }
    if (pdev.bar0 == 0) {
        serial_printf("[XHCI] ERROR: Invalid BAR0 (0x0). Controller not configured.\n");
        return false;
    }
    g_mmio = hhdm + pdev.bar0;
    g_op   = cap_rd(XHCI_CAPLENGTH) & 0xFF;
    g_rt   = cap_rd(XHCI_RTSOFF) & ~0x1Fu;
    g_db   = cap_rd(XHCI_DBOFF)  & ~0x3u;


    uint32_t max_slots = cap_rd(XHCI_HCSPARAMS1) & 0xFF;
    uint32_t max_ports = (cap_rd(XHCI_HCSPARAMS1) >> 24) & 0xFF;
    serial_printf("[XHCI] MMIO=0x%x op=+%d rt=+%d db=+%d slots=%d ports=%d\n",
                  (uint32_t)pdev.bar0, g_op, g_rt, g_db, max_slots, max_ports);

    /* 3. Reset controller */
    op_wr(XHCI_USBCMD, op_rd(XHCI_USBCMD) & ~USBCMD_RUN);
    if (!wait_set(XHCI_USBSTS, USBSTS_HCH, 20)) {
        serial_printf("[XHCI] HCH timeout\n"); return false;
    }
    op_wr(XHCI_USBCMD, op_rd(XHCI_USBCMD) | USBCMD_HCRST);
    if (!wait_clear(XHCI_USBCMD, USBCMD_HCRST, 100)) {
        serial_printf("[XHCI] HCRST timeout\n"); return false;
    }
    if (!wait_clear(XHCI_USBSTS, USBSTS_CNR, 100)) {
        serial_printf("[XHCI] CNR timeout\n"); return false;
    }

    /* 4. Set max slots in CONFIG register */
    op_wr(XHCI_CONFIG, XHCI_MAX_SLOTS);

    /* 5. Allocate + set up DCBAA */
    g_dcbaa = alloc_page_virt(&g_dcbaa_phys);
    op_wr(XHCI_DCBAAP_LO, (uint32_t)(g_dcbaa_phys & 0xFFFFFFFF));
    op_wr(XHCI_DCBAAP_HI, (uint32_t)(g_dcbaa_phys >> 32));

    /* 6. Command ring — PMM allocated */
    g_cmd_ring = alloc_ring(&g_cmd_ring_phys);
    op_wr(XHCI_CRCR_LO, (uint32_t)(g_cmd_ring_phys & 0xFFFFFFFF) | g_cmd_ring->pcs);
    op_wr(XHCI_CRCR_HI, (uint32_t)(g_cmd_ring_phys >> 32));

    /* 7. Event ring + ERST */
    g_evt_ring = alloc_page_virt(&g_evt_ring_phys);
    g_erst     = alloc_page_virt(&g_erst_phys);
    g_erst[0]  = g_evt_ring_phys;
    g_erst[1]  = XHCI_EVENT_RING_SIZE;
    g_evt_dequeue = 0;
    g_evt_ccs     = 1;

    /* Interrupter 0 (runtime space offset 0x20 for IR[0]) */
    uint32_t ir = g_rt + 0x20;  /* absolute offset from g_mmio base */
    /* ERSTSZ: number of segments = 1 */
    *(volatile uint32_t*)(g_mmio + ir + 0x08) = 1;
    /* ERSTBA: physical base of Event Ring Segment Table */
    *(volatile uint32_t*)(g_mmio + ir + 0x10) = (uint32_t)(g_erst_phys & 0xFFFFFFFF);
    *(volatile uint32_t*)(g_mmio + ir + 0x14) = (uint32_t)(g_erst_phys >> 32);
    /* ERDP: initial dequeue pointer = first TRB in event ring (EHB=0) */
    *(volatile uint32_t*)(g_mmio + ir + 0x18) = (uint32_t)(g_evt_ring_phys & 0xFFFFFFFF);
    *(volatile uint32_t*)(g_mmio + ir + 0x1C) = (uint32_t)(g_evt_ring_phys >> 32);
    /* IMAN: clear IP, enable IE */
    uint32_t iman = *(volatile uint32_t*)(g_mmio + ir + 0x00);
    *(volatile uint32_t*)(g_mmio + ir + 0x00) = (iman | 2) & ~1u; /* IE=1, IP cleared */

    /* 8. Start controller */
    op_wr(XHCI_USBCMD, op_rd(XHCI_USBCMD) | USBCMD_RUN);
    if (!wait_clear(XHCI_USBSTS, USBSTS_HCH, 20)) {
        serial_printf("[XHCI] RUN timeout\n"); return false;
    }
    serial_printf("[XHCI] Controller running.\n");

    /* 9. Scan ports for a connected device */
    int connected_port = -1;
    if (max_ports > XHCI_MAX_PORTS) max_ports = XHCI_MAX_PORTS;
    for (uint32_t p = 0; p < max_ports; p++) {
        uint32_t psc = *(volatile uint32_t*)(g_mmio + g_op + XHCI_PORT_BASE + p*16);
        if (psc & PORTSC_CCS) {
            serial_printf("[XHCI] Device on port %d\n", p);
            connected_port = (int)p;
            break;
        }
    }
    if (connected_port < 0) {
        serial_printf("[XHCI] No device found on any port.\n"); return false;
    }

    /* 10. Reset port and wait for device to settle */
    port_reset((uint8_t)connected_port);

    /* Brief settle: poll PORTSC until PED (Port Enabled) is set or timeout */
    for (int i = 0; i < 500000; i++) {
        uint32_t psc = *(volatile uint32_t*)(g_mmio + g_op + XHCI_PORT_BASE + connected_port*16);
        if (psc & PORTSC_PED) break;
        for (volatile int d = 0; d < 20; d++);
    }
    uint8_t port_speed = 0;
    {
        uint32_t psc = *(volatile uint32_t*)(g_mmio + g_op + XHCI_PORT_BASE + connected_port*16);
        port_speed = (psc >> 10) & 0xF; /* 1=FS 2=LS 3=HS 4=SS */
        serial_printf("[XHCI] Port %d speed=%d PED=%d\n",
                      connected_port, port_speed, (psc >> 1) & 1);
    }
    /* EP0 initial max packet based on speed */
    uint16_t ep0_mps = (port_speed == 4) ? 512 :
                       (port_speed == 3) ? 64  :
                       (port_speed == 2) ? 8   : 64;
    serial_printf("[XHCI] EP0 initial MPS=%d\n", ep0_mps);

    /* 11. Enable Slot */
    uint8_t slot_id = 0;
    if (!issue_cmd(0, 0, 0, TRB_DWORD3(TRB_TYPE_ENABLE_SLOT, 0), &slot_id)) {
        serial_printf("[XHCI] Enable Slot failed\n"); return false;
    }
    serial_printf("[XHCI] Slot %d enabled\n", slot_id);

    /* 12. Allocate Output Device Context + EP0 transfer ring */
    g_out_ctx = alloc_page_virt(&g_out_ctx_phys);
    g_dcbaa[slot_id] = g_out_ctx_phys;

    /* EP0 transfer ring — PMM allocated */
    g_xfer_ep0 = alloc_ring(&g_xfer_ep0_phys);

    /* 13. Build Input Context for Address Device */
    g_in_ctx = alloc_page_virt(&g_in_ctx_phys);
    /* Input Control Context: A0=1 (slot), A1=1 (EP0) */
    uint32_t *icc = (uint32_t*)g_in_ctx;
    icc[1] = 0x3; /* Add bits: slot + EP0 */

    /* Slot Context (offset 32) */
    uint32_t *slot_ctx = (uint32_t*)(g_in_ctx + 32);
    slot_ctx[0] = (1 << 27) | ((uint32_t)port_speed << 20); /* Context Entries = 1, Speed */
    slot_ctx[1] = (uint32_t)(connected_port + 1) << 16; /* Root Hub Port Number */

    /* EP0 Context (offset 64) */
    uint32_t *ep0_ctx = (uint32_t*)(g_in_ctx + 64);
    ep0_ctx[1] = (EP_TYPE_CTRL << 3) | (3 << 1) | ((uint32_t)ep0_mps << 16);  /* EP Type=Control, CErr=3, MPS */
    ep0_ctx[2] = (uint32_t)(g_xfer_ep0_phys & 0xFFFFFFFF) | g_xfer_ep0->pcs;
    ep0_ctx[3] = (uint32_t)(g_xfer_ep0_phys >> 32);
    ep0_ctx[4] = 8; /* Average TRB Length */

    /* 14. Address Device */
    if (!issue_cmd((uint32_t)(g_in_ctx_phys & 0xFFFFFFFF),
                   (uint32_t)(g_in_ctx_phys >> 32),
                   0,
                   TRB_DWORD3(TRB_TYPE_ADDRESS_DEVICE, 0) | ((uint32_t)slot_id << 24),
                   NULL)) {
        serial_printf("[XHCI] Address Device failed\n"); return false;
    }
    serial_printf("[XHCI] Device addressed on slot %d\n", slot_id);

    /* 15. Get Device Descriptor to find max packet size + class */
    uint8_t *desc_buf = alloc_page_virt(&(uint64_t){0});
    xhci_control(slot_id, 0x80, 6 /*GET_DESCRIPTOR*/, 0x0100 /*DEVICE*/, 0, 18, desc_buf, 18);
    uint8_t bDevClass = desc_buf[4];
    uint8_t bMaxPkt   = desc_buf[7];
    serial_printf("[XHCI] DevClass=0x%x MaxPkt=%d\n", bDevClass, bMaxPkt);

    /* Update EP0 max packet size in output context */
    uint32_t *ep0_out = (uint32_t*)(g_out_ctx + 32); /* EP0 in output ctx is at offset 32 from g_out_ctx */
    /* Wait! In Output Context, Slot context is at 0, EP0 context is at 32.
     * Dword 1 of EP0 context is at offset 32 + 4 = 36.
     * Let's update Max Packet Size (bits 31:16) of Dword 1. */
    ep0_out[1] = (ep0_out[1] & 0x0000FFFF) | ((uint32_t)bMaxPkt << 16);

    /* 16. Get Configuration Descriptor to find bulk endpoints */
    uint8_t *cfg_buf = alloc_page_virt(&(uint64_t){0});
    xhci_control(slot_id, 0x80, 6, 0x0200 /*CONFIG*/, 0, 255, cfg_buf, 255);

    /* Parse interface + endpoint descriptors */
    uint8_t bulk_in = 0, bulk_out = 0;
    uint16_t max_pkt = 512;
    uint8_t total_len = cfg_buf[2];
    uint8_t *p2 = cfg_buf;
    uint8_t *end = cfg_buf + total_len;
    while (p2 < end) {
        uint8_t len2 = p2[0], typ = p2[1];
        if (len2 == 0) break;
        if (typ == USB_DESC_ENDPOINT) {
            uint8_t addr = p2[2];
            uint8_t attr = p2[3];
            if ((attr & 0x3) == 2) { /* Bulk */
                if (addr & 0x80) bulk_in  = addr & 0x0F;
                else             bulk_out = addr & 0x0F;
                max_pkt = p2[4] | ((uint16_t)p2[5] << 8);
            }
        }
        p2 += len2;
    }
    if (!bulk_in || !bulk_out) {
        serial_printf("[XHCI] No bulk endpoints found — not a mass storage device\n");
        return false;
    }
    serial_printf("[XHCI] Bulk IN=EP%d OUT=EP%d MaxPkt=%d\n",
                  bulk_in, bulk_out, max_pkt);

    /* 17. Set Configuration */
    uint8_t config_val = cfg_buf[5];
    xhci_control(slot_id, 0x00, 9 /*SET_CONFIGURATION*/, config_val, 0, 0, NULL, 0);

    /* 18. Configure Endpoint — add bulk IN + OUT to slot */
    /* Rebuild input context with bulk endpoints */
    for (int i = 0; i < 4096; i++) g_in_ctx[i] = 0;
    icc[1] = (1 << 0) | (1 << (bulk_out*2)) | (1 << (bulk_in*2+1)); /* A bits */

    uint32_t max_ep_idx = (bulk_in * 2 + 1 > bulk_out * 2) ? (bulk_in * 2 + 1) : (bulk_out * 2);
    slot_ctx[0] = (max_ep_idx << 27) | ((uint32_t)port_speed << 20);
    slot_ctx[1] = (uint32_t)(connected_port + 1) << 16;

    /* Bulk OUT — PMM allocated */
    g_xfer_bulk_out = alloc_ring(&g_xfer_bulk_out_phys);
    uint32_t *ep_out_ctx = (uint32_t*)(g_in_ctx + 32 + bulk_out * 2 * 32);
    ep_out_ctx[1] = (EP_TYPE_BULK_OUT << 3) | (3 << 1) | ((uint32_t)max_pkt << 16);
    ep_out_ctx[2] = (uint32_t)(g_xfer_bulk_out_phys & 0xFFFFFFFF) | g_xfer_bulk_out->pcs;
    ep_out_ctx[3] = (uint32_t)(g_xfer_bulk_out_phys >> 32);
    ep_out_ctx[4] = 1024; /* Average TRB Length */

    /* Bulk IN — PMM allocated */
    g_xfer_bulk_in = alloc_ring(&g_xfer_bulk_in_phys);
    uint32_t *ep_in_ctx = (uint32_t*)(g_in_ctx + 32 + (bulk_in*2+1) * 32);
    ep_in_ctx[1] = (EP_TYPE_BULK_IN << 3) | (3 << 1) | ((uint32_t)max_pkt << 16);
    ep_in_ctx[2] = (uint32_t)(g_xfer_bulk_in_phys & 0xFFFFFFFF) | g_xfer_bulk_in->pcs;
    ep_in_ctx[3] = (uint32_t)(g_xfer_bulk_in_phys >> 32);
    ep_in_ctx[4] = 1024; /* Average TRB Length */

    if (!issue_cmd((uint32_t)(g_in_ctx_phys & 0xFFFFFFFF),
                   (uint32_t)(g_in_ctx_phys >> 32),
                   0,
                   TRB_DWORD3(TRB_TYPE_CONFIG_EP, 0) | ((uint32_t)slot_id << 24),
                   NULL)) {
        serial_printf("[XHCI] Configure Endpoint failed\n"); return false;
    }

    g_msd.present     = true;
    g_msd.slot_id     = slot_id;
    g_msd.port        = (uint8_t)connected_port;
    g_msd.bulk_in_ep  = bulk_in;
    g_msd.bulk_out_ep = bulk_out;
    g_msd.max_packet  = max_pkt;

    serial_printf("[XHCI] Mass storage device ready.\n");
    return true;
}
