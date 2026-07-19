#pragma once

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

// Initialize the USB-HID driver and probe for a FIDO2 USB-HID device.
// Returns true if a physical FIDO2 device is found and ready.
bool usb_hid_init(void);

// Send a raw 64-byte HID report to the FIDO2 device.
int usb_hid_send_report(const uint8_t *report);

// Receive a raw 64-byte HID report from the FIDO2 device.
int usb_hid_recv_report(uint8_t *report);
