#pragma once
/*
 * usb_msc.h — USB Mass Storage Class, Bulk-Only Transport (BOT).
 * Protocol: SCSI Transparent Command Set over USB BOT.
 * Architecture Bible ref: §9 M7
 */
#include <stdint.h>
#include <stdbool.h>

bool usb_msc_init(void);          /* INQUIRY + READ CAPACITY */
int  usb_msc_read_sector(uint32_t lba, void *buf); /* 512-byte sector */
uint32_t usb_msc_sector_count(void);
