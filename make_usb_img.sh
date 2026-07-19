#!/usr/bin/env bash
# make_usb_img.sh — Build a FAT32 USB test image for StyxOS M7.
# Delegates to make_usb_img.py (zero system dependencies, no root).
set -e

ELF="user/user.elf"
IMG="usb.img"

if [ ! -f "$ELF" ]; then
    echo "[USB] $ELF not found — run 'make all' first"
    exit 1
fi

python3 make_usb_img.py "$ELF" "$IMG"
