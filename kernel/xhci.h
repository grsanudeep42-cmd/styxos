#pragma once

/*
 * xhci.h — xHCI Host Controller Driver public interface.
 *
 * Runs in ring-0 for M7. Ring-3 driver migration is planned for M11.
 *
 * Provides:
 *   xhci_init()          — find controller, map MMIO, reset, set up rings
 *   xhci_bulk_in()       — receive data from a bulk-IN endpoint
 *   xhci_bulk_out()      — send data to a bulk-OUT endpoint
 *   xhci_control()       — send a control transfer (used for USB enumeration)
 *
 * Architecture Bible ref: §9 M7, §11 Driver Model
 */

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

/* ── xHCI MMIO register offsets (Capability Registers) ───────────────────── */

#define XHCI_CAPLENGTH      0x00   /* Capability Register Length (1 byte) */
#define XHCI_HCSPARAMS1     0x04   /* Structural Parameters 1 */
#define XHCI_HCSPARAMS2     0x08   /* Structural Parameters 2 */
#define XHCI_HCCPARAMS1     0x10   /* Capability Parameters 1 */
#define XHCI_DBOFF          0x14   /* Doorbell Array Offset */
#define XHCI_RTSOFF         0x18   /* Runtime Register Space Offset */

/* ── Operational Register Offsets (base = MMIO + CAPLENGTH) ──────────────── */

#define XHCI_USBCMD         0x00
#define XHCI_USBSTS         0x04
#define XHCI_PAGESIZE       0x08
#define XHCI_DNCTRL         0x14
#define XHCI_CRCR_LO        0x18   /* Command Ring Control (low 32) */
#define XHCI_CRCR_HI        0x1C
#define XHCI_DCBAAP_LO      0x30   /* Device Context Base Address Array Pointer */
#define XHCI_DCBAAP_HI      0x34
#define XHCI_CONFIG         0x38

/* USBCMD bits */
#define USBCMD_RUN          (1 << 0)
#define USBCMD_HCRST        (1 << 1)
#define USBCMD_INTE         (1 << 2)

/* USBSTS bits */
#define USBSTS_HCH          (1 << 0)   /* Host Controller Halted */
#define USBSTS_CNR          (1 << 11)  /* Controller Not Ready */

/* ── Port register base (in operational space) ───────────────────────────── */
#define XHCI_PORT_BASE      0x400
#define XHCI_PORT_SIZE      0x10
#define XHCI_PORTSC(n)      (XHCI_PORT_BASE + (n) * XHCI_PORT_SIZE)

/* PORTSC bits */
#define PORTSC_CCS          (1 << 0)   /* Current Connect Status */
#define PORTSC_PED          (1 << 1)   /* Port Enabled */
#define PORTSC_PR           (1 << 4)   /* Port Reset */
#define PORTSC_PRC          (1 << 21)  /* Port Reset Change */
#define PORTSC_CSC          (1 << 17)  /* Connect Status Change */

/* ── Interrupter registers (runtime space) ───────────────────────────────── */
#define XHCI_IR0_IMAN       0x20   /* offset from runtime base */
#define XHCI_IR0_IMOD       0x24
#define XHCI_IR0_ERSTSZ     0x28
#define XHCI_IR0_ERSTBA_LO  0x30
#define XHCI_IR0_ERSTBA_HI  0x34
#define XHCI_IR0_ERDP_LO    0x38
#define XHCI_IR0_ERDP_HI    0x3C

/* ── Slot / endpoint context sizes ──────────────────────────────────────────*/
#define XHCI_CTX_SIZE       32     /* bytes per context entry (CSZ=0) */

/* ── USB Standard Descriptor types ─────────────────────────────────────────*/
#define USB_DESC_DEVICE     0x01
#define USB_DESC_CONFIG     0x02
#define USB_DESC_STRING     0x03
#define USB_DESC_INTERFACE  0x04
#define USB_DESC_ENDPOINT   0x05

/* ── Endpoint types ─────────────────────────────────────────────────────────*/
#define EP_TYPE_CTRL        4
#define EP_TYPE_BULK_OUT    2
#define EP_TYPE_BULK_IN     6

/* ── Max slots / ports we support ───────────────────────────────────────────*/
#define XHCI_MAX_SLOTS      8
#define XHCI_MAX_PORTS      16
#define XHCI_ERST_SIZE      1      /* single Event Ring Segment */
#define XHCI_EVENT_RING_SIZE 64

/* ── USB device state (our internal tracking) ───────────────────────────────*/
typedef struct {
    bool     present;
    uint8_t  slot_id;
    uint8_t  port;
    uint8_t  bulk_in_ep;    /* endpoint number (1-based) */
    uint8_t  bulk_out_ep;
    uint16_t max_packet;
} xhci_device_t;

/* ── Public API ─────────────────────────────────────────────────────────────*/

/*
 * xhci_init() — detect xHCI via PCI, map MMIO, reset controller,
 *               set up command ring, event ring, DCBAA, enumerate ports.
 * Returns true if a USB mass storage device was found and addressed.
 */
bool xhci_init(void);

/*
 * xhci_control() — issue a USB control transfer on the default pipe (EP0).
 *   slot    : device slot id
 *   bmReqType, bReq, wValue, wIndex, wLength : standard USB setup packet fields
 *   buf     : data buffer (direction determined by bmReqType bit 7)
 *   len     : transfer length
 * Returns 0 on success, negative on error.
 */
int xhci_control(uint8_t slot, uint8_t bmReqType, uint8_t bReq,
                 uint16_t wValue, uint16_t wIndex, uint16_t wLength,
                 void *buf, size_t len);

/*
 * xhci_bulk_out() / xhci_bulk_in()
 *   Transfer `len` bytes to/from the bulk endpoint of the first mass storage device.
 *   Returns 0 on success, negative on error.
 */
int xhci_bulk_out(const void *buf, size_t len);
int xhci_bulk_in (void *buf, size_t len);

/* Returns pointer to found mass storage device, or NULL. */
xhci_device_t *xhci_get_msd(void);
