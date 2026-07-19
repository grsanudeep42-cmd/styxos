/*
 * usb_msc.c — USB Mass Storage Bulk-Only Transport (BOT)
 * CBW → data → CSW.  SCSI: INQUIRY, READ CAPACITY (10), READ (10).
 */
#include "usb_msc.h"
#include "xhci.h"
#include "serial.h"
#include "heap.h"
#include "xts.h"
#include "integrity.h"
#include "destruct.h"
#include <stdint.h>

bool g_encryption_enabled = false;
aes256_xts_ctx_t g_xts_ctx;

/* ── BOT structures ──────────────────────────────────────────────────────── */

#define CBW_SIGNATURE  0x43425355U
#define CSW_SIGNATURE  0x53425355U
#define CBW_SIZE       31
#define CSW_SIZE       13

typedef struct __attribute__((packed)) {
    uint32_t dCBWSignature;
    uint32_t dCBWTag;
    uint32_t dCBWDataTransferLength;
    uint8_t  bmCBWFlags;          /* 0x80 = IN, 0x00 = OUT */
    uint8_t  bCBWLUN;
    uint8_t  bCBWCBLength;
    uint8_t  CBWCB[16];
} cbw_t;

typedef struct __attribute__((packed)) {
    uint32_t dCSWSignature;
    uint32_t dCSWTag;
    uint32_t dCSWDataResidue;
    uint8_t  bCSWStatus;          /* 0=good, 1=failed, 2=phase error */
} csw_t;

static uint32_t g_tag        = 1;
static uint32_t g_sector_cnt = 0;

/* ── Send CBW + transfer + receive CSW ───────────────────────────────────── */
static int bot_execute(cbw_t *cbw, void *data, uint32_t len, bool data_in) {
    /* OUT: CBW */
    if (xhci_bulk_out(cbw, CBW_SIZE) != 0) {
        serial_printf("[MSC] CBW send failed\n"); return -1;
    }
    /* Data phase */
    if (len > 0 && data) {
        int rc = data_in ? xhci_bulk_in(data, len)
                         : xhci_bulk_out(data, len);
        if (rc != 0) {
            serial_printf("[MSC] Data phase failed\n"); return -2;
        }
    }
    /* IN: CSW */
    csw_t csw;
    if (xhci_bulk_in(&csw, CSW_SIZE) != 0) {
        serial_printf("[MSC] CSW recv failed\n"); return -3;
    }
    if (csw.dCSWSignature != CSW_SIGNATURE) {
        serial_printf("[MSC] Bad CSW signature\n"); return -4;
    }
    if (csw.bCSWStatus != 0) {
        serial_printf("[MSC] CSW status=%d\n", csw.bCSWStatus); return -5;
    }
    return 0;
}

static cbw_t make_cbw(uint32_t xfer_len, uint8_t flags, uint8_t cb_len) {
    cbw_t c = {0};
    c.dCBWSignature        = CBW_SIGNATURE;
    c.dCBWTag              = g_tag++;
    c.dCBWDataTransferLength = xfer_len;
    c.bmCBWFlags           = flags;
    c.bCBWLUN              = 0;
    c.bCBWCBLength         = cb_len;
    return c;
}

/* ── INQUIRY ─────────────────────────────────────────────────────────────── */
static int msc_inquiry(void) {
    cbw_t cbw = make_cbw(36, 0x80, 6);
    cbw.CBWCB[0] = 0x12; /* INQUIRY */
    cbw.CBWCB[4] = 36;
    uint8_t buf[36];
    int rc = bot_execute(&cbw, buf, 36, true);
    if (rc == 0)
        serial_printf("[MSC] INQUIRY ok. Vendor=%.8s\n", (char*)&buf[8]);
    return rc;
}

/* ── READ CAPACITY (10) ──────────────────────────────────────────────────── */
static int msc_read_capacity(void) {
    cbw_t cbw = make_cbw(8, 0x80, 10);
    cbw.CBWCB[0] = 0x25; /* READ CAPACITY (10) */
    uint8_t buf[8];
    int rc = bot_execute(&cbw, buf, 8, true);
    if (rc == 0) {
        g_sector_cnt = ((uint32_t)buf[0]<<24)|((uint32_t)buf[1]<<16)|
                       ((uint32_t)buf[2]<<8)|buf[3];
        g_sector_cnt += 1; /* last LBA → count */
        serial_printf("[MSC] Capacity: %d sectors (512 B each)\n", g_sector_cnt);
    }
    return rc;
}

/* ── READ (10) ───────────────────────────────────────────────────────────── */
int usb_msc_read_sector(uint32_t lba, void *buf) {
    cbw_t cbw = make_cbw(512, 0x80, 10);
    cbw.CBWCB[0] = 0x28; /* READ (10) */
    cbw.CBWCB[2] = (lba >> 24) & 0xFF;
    cbw.CBWCB[3] = (lba >> 16) & 0xFF;
    cbw.CBWCB[4] = (lba >>  8) & 0xFF;
    cbw.CBWCB[5] =  lba        & 0xFF;
    cbw.CBWCB[8] = 1; /* transfer length = 1 block */
    int rc = bot_execute(&cbw, buf, 512, true);
    if (rc == 0 && g_encryption_enabled) {
        aes256_xts_decrypt_sector(&g_xts_ctx, lba, (const uint8_t *)buf, (uint8_t *)buf);
        if (!integrity_verify_sector(lba, (const uint8_t *)buf)) {
            destruct_trigger("Sector integrity verification mismatch");
        }
    }
    return rc;
}

int usb_msc_write_sector(uint32_t lba, const void *buf) {
    uint8_t temp_buf[512];
    const void *data_ptr = buf;
    if (g_encryption_enabled) {
        aes256_xts_encrypt_sector(&g_xts_ctx, lba, (const uint8_t *)buf, temp_buf);
        data_ptr = temp_buf;
    }
    cbw_t cbw = make_cbw(512, 0x00, 10);
    cbw.CBWCB[0] = 0x2A; /* WRITE (10) */
    cbw.CBWCB[2] = (lba >> 24) & 0xFF;
    cbw.CBWCB[3] = (lba >> 16) & 0xFF;
    cbw.CBWCB[4] = (lba >>  8) & 0xFF;
    cbw.CBWCB[5] =  lba        & 0xFF;
    cbw.CBWCB[8] = 1; /* transfer length = 1 block */
    return bot_execute(&cbw, (void *)data_ptr, 512, false);
}

uint32_t usb_msc_sector_count(void) { return g_sector_cnt; }

/* ── Init ────────────────────────────────────────────────────────────────── */
bool usb_msc_init(void) {
    if (!xhci_get_msd()) {
        serial_printf("[MSC] No mass storage device.\n"); return false;
    }
    if (msc_inquiry()       != 0) return false;
    if (msc_read_capacity() != 0) return false;
    serial_printf("[MSC] Ready.\n");
    return true;
}
