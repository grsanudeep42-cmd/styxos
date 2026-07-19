/*
 * pci.c — PCI configuration space access (CF8/CFC port-based).
 *
 * Architecture Bible ref: §9 M7
 */

#include "pci.h"
#include "io.h"
#include "serial.h"

/* ── Raw config-space read/write ─────────────────────────────────────────── */

uint32_t pci_read32(uint8_t bus, uint8_t dev, uint8_t fn, uint8_t reg) {
    outl(PCI_CONFIG_ADDR, PCI_ADDR(bus, dev, fn, reg));
    return inl(PCI_CONFIG_DATA);
}

void pci_write32(uint8_t bus, uint8_t dev, uint8_t fn, uint8_t reg, uint32_t val) {
    outl(PCI_CONFIG_ADDR, PCI_ADDR(bus, dev, fn, reg));
    outl(PCI_CONFIG_DATA, val);
}

uint16_t pci_read16(uint8_t bus, uint8_t dev, uint8_t fn, uint8_t reg) {
    outl(PCI_CONFIG_ADDR, PCI_ADDR(bus, dev, fn, reg));
    return (uint16_t)(inl(PCI_CONFIG_DATA) >> ((reg & 2) * 8));
}

void pci_write16(uint8_t bus, uint8_t dev, uint8_t fn, uint8_t reg, uint16_t val) {
    uint32_t dword = pci_read32(bus, dev, fn, reg & 0xFC);
    int shift = (reg & 2) * 8;
    dword = (dword & ~(0xFFFFU << shift)) | ((uint32_t)val << shift);
    pci_write32(bus, dev, fn, reg & 0xFC, dword);
}

/* ── BAR decode ──────────────────────────────────────────────────────────── */

/*
 * Decode a 64-bit MMIO BAR at offset `bar_off`.
 * Writes all-ones to determine size, then restores original value.
 * Returns base address and size via pointers.
 */
static void decode_bar64(uint8_t bus, uint8_t dev, uint8_t fn,
                          uint8_t bar_off, uint64_t *base, uint32_t *size) {
    uint32_t lo = pci_read32(bus, dev, fn, bar_off);
    uint32_t hi = pci_read32(bus, dev, fn, bar_off + 4);

    /* Size probe */
    pci_write32(bus, dev, fn, bar_off,     0xFFFFFFFF);
    pci_write32(bus, dev, fn, bar_off + 4, 0xFFFFFFFF);
    uint32_t sz_lo = pci_read32(bus, dev, fn, bar_off);
    pci_write32(bus, dev, fn, bar_off,     lo);
    pci_write32(bus, dev, fn, bar_off + 4, hi);

    *base = ((uint64_t)hi << 32) | (lo & ~0xFU);
    sz_lo &= ~0xFU;
    *size  = ~sz_lo + 1;
}

/* ── xHCI scan ───────────────────────────────────────────────────────────── */

bool pci_find_xhci(pci_device_t *out) {
    for (uint16_t bus = 0; bus < 256; bus++) {
        for (uint8_t dev = 0; dev < 32; dev++) {
            for (uint8_t fn = 0; fn < 8; fn++) {
                uint32_t vid = pci_read32((uint8_t)bus, dev, fn, PCI_VENDOR_ID);
                if ((vid & 0xFFFF) == 0xFFFF) continue; /* no device */

                uint32_t cr  = pci_read32((uint8_t)bus, dev, fn, PCI_CLASS_REV);
                uint8_t  cls = (cr >> 24) & 0xFF;
                uint8_t  sub = (cr >> 16) & 0xFF;
                uint8_t  pif = (cr >>  8) & 0xFF;

                if (cls == PCI_CLASS_SERIAL_BUS &&
                    sub == PCI_SUB_USB          &&
                    pif == PCI_PROGIF_XHCI) {

                    uint32_t bar0_lo = pci_read32((uint8_t)bus, dev, fn, PCI_BAR0);
                    uint64_t base;
                    uint32_t sz;

                    if ((bar0_lo & 0x6) == 0x4) {
                        /* 64-bit BAR */
                        decode_bar64((uint8_t)bus, dev, fn, PCI_BAR0, &base, &sz);
                    } else {
                        base = bar0_lo & ~0xFU;
                        pci_write32((uint8_t)bus, dev, fn, PCI_BAR0, 0xFFFFFFFF);
                        uint32_t sz_lo = pci_read32((uint8_t)bus, dev, fn, PCI_BAR0) & ~0xFU;
                        pci_write32((uint8_t)bus, dev, fn, PCI_BAR0, bar0_lo);
                        sz = ~sz_lo + 1;
                    }

                    out->bus      = (uint8_t)bus;
                    out->dev      = dev;
                    out->fn       = fn;
                    out->bar0     = base;
                    out->bar0_size = sz;

                    serial_printf("[PCI] xHCI found: bus=%d dev=%d fn=%d "
                                  "BAR0=0x%x size=%d B\n",
                                  bus, dev, fn, (uint32_t)base, sz);
                    return true;
                }
            }
        }
    }
    serial_printf("[PCI] No xHCI controller found.\n");
    return false;
}

/* ── Enable Bus Master + Memory Space ────────────────────────────────────── */

void pci_enable_device(const pci_device_t *d) {
    uint16_t cmd = pci_read16(d->bus, d->dev, d->fn, PCI_COMMAND);
    cmd |= PCI_CMD_BUS_MASTER | PCI_CMD_MEM_SPACE;
    pci_write16(d->bus, d->dev, d->fn, PCI_COMMAND, cmd);
}
