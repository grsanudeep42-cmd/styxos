#pragma once
/*
 * usb_msc.h — USB Mass Storage Class, Bulk-Only Transport (BOT).
 * Protocol: SCSI Transparent Command Set over USB BOT.
 * Architecture Bible ref: §9 M7
 */
#include <stdint.h>
#include <stdbool.h>
#include "xts.h"

extern bool g_encryption_enabled;
extern aes256_xts_ctx_t g_xts_ctx;

bool usb_msc_init(void);          /* INQUIRY + READ CAPACITY */
int  usb_msc_read_sector(uint32_t lba, void *buf); /* 512-byte sector */
int  usb_msc_write_sector(uint32_t lba, const void *buf);
uint32_t usb_msc_sector_count(void);
