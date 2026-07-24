#include "e1000.h"
#include "pci.h"
#include "pmm.h"
#include "vmm.h"
#include "string.h"
#include "serial.h"
#include "anonymize.h"

/* ── e1000 Register Offsets ─────────────────────────────────────────────── */
#define REG_CTRL     0x0000
#define REG_STATUS   0x0008
#define REG_EERD     0x0014
#define REG_ICR      0x00C0
#define REG_IMS      0x00D0
#define REG_IMC      0x00D8
#define REG_RCTL     0x0100
#define REG_TCTL     0x0400
#define REG_RDBAL    0x2800
#define REG_RDBAH    0x2804
#define REG_RDLEN    0x2808
#define REG_RDH      0x2810
#define REG_RDT      0x2818
#define REG_TDBAL    0x3800
#define REG_TDBAH    0x3804
#define REG_TDLEN    0x3808
#define REG_TDH      0x3810
#define REG_TDT      0x3818
#define REG_MTA      0x5200
#define REG_RAL0     0x5400
#define REG_RAH0     0x5404

/* ── Global Driver State ─────────────────────────────────────────────────── */
static pci_device_t g_pci_e1000;
static uintptr_t    g_mmio_base = 0;

static e1000_rx_desc_t *g_rx_descs = NULL;
static e1000_tx_desc_t *g_tx_descs = NULL;

static uint8_t *g_rx_buffers[E1000_NUM_RX_DESC];
static uint8_t *g_tx_buffers[E1000_NUM_TX_DESC];

static uint16_t g_rx_cur = 0;
static uint16_t g_tx_cur = 0;

bool g_e1000_active = false;
uint32_t g_e1000_tx_count = 0;
uint32_t g_e1000_rx_count = 0;

/* ── MMIO Read/Write Helpers ────────────────────────────────────────────── */
static inline uint32_t mmio_read32(uint32_t reg) {
    return *(volatile uint32_t *)(g_mmio_base + reg);
}

static inline void mmio_write32(uint32_t reg, uint32_t val) {
    *(volatile uint32_t *)(g_mmio_base + reg) = val;
}

/* ── e1000 Initialization ───────────────────────────────────────────────── */
bool e1000_init(void) {
    serial_printf("[E1000] Scanning PCI bus for Intel Gigabit Ethernet controller...\n");
    if (!pci_find_e1000(&g_pci_e1000)) {
        serial_printf("[E1000] Controller not found on PCI bus.\n");
        return false;
    }

    pci_enable_device(&g_pci_e1000);

    uint64_t hhdm = vmm_get_hhdm_offset();
    uint32_t bar_pages = (g_pci_e1000.bar0_size + 0xFFF) / 0x1000;
    if (bar_pages == 0) bar_pages = 32; // Default 128KB if unknown

    for (uint32_t pg = 0; pg < bar_pages; pg++) {
        uint64_t phys = g_pci_e1000.bar0 + (uint64_t)pg * 0x1000;
        uint64_t virt = hhdm + phys;
        vmm_map_page(virt, phys, PTE_PRESENT | PTE_WRITABLE);
    }

    g_mmio_base = (uintptr_t)g_pci_e1000.bar0 + hhdm;
    serial_printf("[E1000] MMIO Base mapped at 0x%x (%d pages)\n", (uint32_t)g_mmio_base, bar_pages);

    // 1. Reset device
    mmio_write32(REG_CTRL, mmio_read32(REG_CTRL) | (1U << 26)); // Device Reset (RST)
    for (volatile int i = 0; i < 100000; i++);

    // 2. Disable all interrupts
    mmio_write32(REG_IMC, 0xFFFFFFFF);

    // 3. Program MAC Address from Anonymization Engine
    uint8_t mac[6];
    anonymize_get_mac(mac);

    uint32_t ral = (uint32_t)mac[0] | ((uint32_t)mac[1] << 8) |
                   ((uint32_t)mac[2] << 16) | ((uint32_t)mac[3] << 24);
    uint32_t rah = (uint32_t)mac[4] | ((uint32_t)mac[5] << 8) | (1U << 31); // Bit 31 = Address Valid (AV)

    mmio_write32(REG_RAL0, ral);
    mmio_write32(REG_RAH0, rah);

    serial_printf("[E1000] Hardware MAC programmed: %x:%x:%x:%x:%x:%x\n",
                  mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);

    // 4. Clear Multicast Table Array (MTA)
    for (int i = 0; i < 128; i++) {
        mmio_write32(REG_MTA + (i * 4), 0);
    }

    // 5. Setup Receive (RX) Descriptor Ring
    uintptr_t rx_phys = (uintptr_t)pmm_alloc_frame();
    g_rx_descs = (e1000_rx_desc_t *)(rx_phys + hhdm);
    memset(g_rx_descs, 0, E1000_NUM_RX_DESC * sizeof(e1000_rx_desc_t));

    for (int i = 0; i < E1000_NUM_RX_DESC; i++) {
        uintptr_t buf_phys = (uintptr_t)pmm_alloc_frame();
        g_rx_buffers[i] = (uint8_t *)(buf_phys + hhdm);
        g_rx_descs[i].addr = buf_phys;
        g_rx_descs[i].status = 0;
    }

    mmio_write32(REG_RDBAL, (uint32_t)(rx_phys & 0xFFFFFFFF));
    mmio_write32(REG_RDBAH, (uint32_t)(rx_phys >> 32));
    mmio_write32(REG_RDLEN, E1000_NUM_RX_DESC * sizeof(e1000_rx_desc_t));
    mmio_write32(REG_RDH, 0);
    mmio_write32(REG_RDT, E1000_NUM_RX_DESC - 1);
    g_rx_cur = 0;

    // Enable Receive (RCTL): EN(1) + SECRC(1) + BAM(1) + BSIZE_2048(0)
    mmio_write32(REG_RCTL, (1U << 1) | (1U << 3) | (1U << 15));

    // 6. Setup Transmit (TX) Descriptor Ring
    uintptr_t tx_phys = (uintptr_t)pmm_alloc_frame();
    g_tx_descs = (e1000_tx_desc_t *)(tx_phys + hhdm);
    memset(g_tx_descs, 0, E1000_NUM_TX_DESC * sizeof(e1000_tx_desc_t));

    for (int i = 0; i < E1000_NUM_TX_DESC; i++) {
        uintptr_t buf_phys = (uintptr_t)pmm_alloc_frame();
        g_tx_buffers[i] = (uint8_t *)(buf_phys + hhdm);
        g_tx_descs[i].addr = buf_phys;
        g_tx_descs[i].cmd = 0;
        g_tx_descs[i].status = 1; // Mark as done initially
    }

    mmio_write32(REG_TDBAL, (uint32_t)(tx_phys & 0xFFFFFFFF));
    mmio_write32(REG_TDBAH, (uint32_t)(tx_phys >> 32));
    mmio_write32(REG_TDLEN, E1000_NUM_TX_DESC * sizeof(e1000_tx_desc_t));
    mmio_write32(REG_TDH, 0);
    mmio_write32(REG_TDT, 0);
    g_tx_cur = 0;

    // Enable Transmit (TCTL): EN(1) + PSP(1) + CT(15) + COLD(64)
    mmio_write32(REG_TCTL, (1U << 1) | (1U << 3) | (0x0F << 4) | (0x40 << 12));

    g_e1000_active = true;
    serial_printf("[E1000] Intel 82540EM Gigabit Ethernet driver successfully initialized.\n");
    return true;
}

bool e1000_send_packet(const uint8_t *data, uint16_t len) {
    if (!g_e1000_active || !data || len == 0 || len > E1000_BUFFER_SIZE) {
        return false;
    }

    uint16_t idx = g_tx_cur;
    e1000_tx_desc_t *desc = &g_tx_descs[idx];

    // Wait until current descriptor is free
    uint32_t retries = 100000;
    while (!(desc->status & 0x01) && --retries);

    if (!(desc->status & 0x01)) {
        serial_printf("[E1000] ERROR: TX Descriptor ring full\n");
        return false;
    }

    // Copy packet into physical TX buffer
    memcpy(g_tx_buffers[idx], data, len);

    desc->length = len;
    desc->status = 0;
    // CMD bits: EOP (End of Packet = 1<<0) | IFCS (Insert FCS/Checksum = 1<<1) | RS (Report Status = 1<<3)
    desc->cmd = (1U << 0) | (1U << 1) | (1U << 3);

    // Update Tail pointer to initiate hardware transmission
    g_tx_cur = (g_tx_cur + 1) % E1000_NUM_TX_DESC;
    mmio_write32(REG_TDT, g_tx_cur);

    g_e1000_tx_count++;
    return true;
}

bool e1000_poll_packet(uint8_t *buffer_out, uint16_t *len_out) {
    if (!g_e1000_active || !buffer_out || !len_out) {
        return false;
    }

    uint16_t idx = g_rx_cur;
    e1000_rx_desc_t *desc = &g_rx_descs[idx];

    // Descriptor Done (DD) bit 0
    if (!(desc->status & 0x01)) {
        return false;
    }

    uint16_t pkt_len = desc->length;
    memcpy(buffer_out, g_rx_buffers[idx], pkt_len);
    *len_out = pkt_len;

    desc->status = 0;
    g_rx_cur = (g_rx_cur + 1) % E1000_NUM_RX_DESC;
    mmio_write32(REG_RDT, idx);

    g_e1000_rx_count++;
    return true;
}

void e1000_disable(void) {
    if (!g_mmio_base) return;

    serial_printf("[E1000] HARDWARE KILL-SWITCH TRIGGERED: Disabling Network Interface...\n");
    // Disable RX & TX
    mmio_write32(REG_RCTL, 0);
    mmio_write32(REG_TCTL, 0);
    // Disable interrupts
    mmio_write32(REG_IMC, 0xFFFFFFFF);

    g_e1000_active = false;
}
