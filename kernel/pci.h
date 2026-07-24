#pragma once

/*
 * pci.h — PCI configuration space access and device discovery.
 *
 * Uses legacy port-based mechanism (CF8/CFC) — works on all x86 hardware
 * and in QEMU without ACPI/MCFG. ECAM (PCIe MMIO config) is a future upgrade.
 *
 * Architecture Bible ref: §9 M7
 */

#include <stdint.h>
#include <stdbool.h>

/* ── PCI address encoding ─────────────────────────────────────────────────── */

#define PCI_ADDR(bus, dev, fn, reg) \
    (0x80000000U | ((uint32_t)(bus) << 16) | ((uint32_t)(dev) << 11) | \
     ((uint32_t)(fn) << 8) | ((reg) & 0xFC))

#define PCI_CONFIG_ADDR  0xCF8
#define PCI_CONFIG_DATA  0xCFC

/* ── Standard config-space offsets ───────────────────────────────────────── */

#define PCI_VENDOR_ID     0x00
#define PCI_DEVICE_ID     0x02
#define PCI_COMMAND       0x04
#define PCI_CLASS_REV     0x08   /* [31:24]=class [23:16]=sub [15:8]=progIF [7:0]=rev */
#define PCI_BAR0          0x10
#define PCI_BAR1          0x14

/* PCI command register bits */
#define PCI_CMD_BUS_MASTER  (1 << 2)
#define PCI_CMD_MEM_SPACE   (1 << 1)

/* ── xHCI class codes ─────────────────────────────────────────────────────── */

#define PCI_CLASS_SERIAL_BUS  0x0C
#define PCI_SUB_USB           0x03
#define PCI_PROGIF_XHCI       0x30

/* ── Device location descriptor ──────────────────────────────────────────── */

typedef struct {
    uint8_t  bus;
    uint8_t  dev;
    uint8_t  fn;
    uint64_t bar0;       /* MMIO base address (64-bit BAR decoded) */
    uint32_t bar0_size;  /* BAR aperture size in bytes             */
} pci_device_t;

/* ── API ──────────────────────────────────────────────────────────────────── */

uint32_t pci_read32 (uint8_t bus, uint8_t dev, uint8_t fn, uint8_t reg);
void     pci_write32(uint8_t bus, uint8_t dev, uint8_t fn, uint8_t reg, uint32_t val);
uint16_t pci_read16 (uint8_t bus, uint8_t dev, uint8_t fn, uint8_t reg);
void     pci_write16(uint8_t bus, uint8_t dev, uint8_t fn, uint8_t reg, uint16_t val);

/*
 * pci_find_xhci() — scan all buses for an xHCI controller.
 * Returns true and fills *out on success. Returns false if not found.
 */
bool pci_find_xhci(pci_device_t *out);

/*
 * pci_find_e1000() — scan all buses for an Intel e1000 network controller.
 * Returns true and fills *out on success. Returns false if not found.
 */
bool pci_find_e1000(pci_device_t *out);

/*
 * pci_enable_device() — set Bus Master + Memory Space bits in command register.
 * Required before MMIO BAR access works.
 */
void pci_enable_device(const pci_device_t *dev);
