#!/usr/bin/env python3
"""
make_usb_img.py — Minimal FAT32 image creator for StyxOS M7.
Zero dependencies beyond Python3 — no root, no mtools, no loop mount.

Usage: python3 make_usb_img.py [elf_path] [img_path]
Default: user/user.elf → usb.img (64 MB FAT32)
"""

import struct, sys, os, math

# ── Parameters ────────────────────────────────────────────────────────────────
IMG_MB             = 64
SECTOR             = 512
SPC                = 8          # sectors per cluster → 4 KB clusters
RESERVED           = 32
NFATS              = 2
ROOT_CLUSTER       = 2
FILE_START_CLUSTER = 3          # first data cluster after root dir

total_sectors = (IMG_MB * 1024 * 1024) // SECTOR   # 131072

# ── FAT size calculation (iterative converge) ─────────────────────────────────
fat_size = math.ceil(total_sectors * 4 / SECTOR)   # upper bound
for _ in range(4):
    data_start   = RESERVED + NFATS * fat_size
    data_clusters = (total_sectors - data_start) // SPC
    fat_size      = math.ceil((data_clusters + 2) * 4 / SECTOR)

data_start    = RESERVED + NFATS * fat_size
data_clusters = (total_sectors - data_start) // SPC

# ── BPB ───────────────────────────────────────────────────────────────────────
def make_bpb():
    b = bytearray(SECTOR)
    b[0:3]   = bytes([0xEB, 0x58, 0x90])
    b[3:11]  = b'MSDOS5.0'
    struct.pack_into('<H', b, 11, SECTOR)
    b[13]    = SPC
    struct.pack_into('<H', b, 14, RESERVED)
    b[16]    = NFATS
    struct.pack_into('<H', b, 17, 0)          # root_entry_count = 0 → FAT32
    struct.pack_into('<H', b, 19, 0)          # total_sectors_16 = 0 → FAT32
    b[21]    = 0xF8                           # media type
    struct.pack_into('<H', b, 22, 0)          # fat_size_16 = 0 → FAT32
    struct.pack_into('<H', b, 24, 63)
    struct.pack_into('<H', b, 26, 255)
    struct.pack_into('<I', b, 28, 0)          # hidden sectors
    struct.pack_into('<I', b, 32, total_sectors)
    struct.pack_into('<I', b, 36, fat_size)   # fat_size_32
    struct.pack_into('<H', b, 40, 0)          # ext_flags
    struct.pack_into('<H', b, 42, 0)          # fs_version
    struct.pack_into('<I', b, 44, ROOT_CLUSTER)
    struct.pack_into('<H', b, 48, 1)          # fs_info sector
    struct.pack_into('<H', b, 50, 6)          # backup boot sector
    b[64]    = 0x80                           # drive number
    b[66]    = 0x29                           # boot sig
    struct.pack_into('<I', b, 67, 0x5A5A0001) # volume ID
    b[71:82] = b'STYXBOOT   '
    b[82:90] = b'FAT32   '
    b[510]   = 0x55
    b[511]   = 0xAA
    return bytes(b)

# ── FAT helpers ───────────────────────────────────────────────────────────────
def cluster_sector(c):
    return data_start + (c - 2) * SPC

def build_fat(clusters_needed):
    fat = bytearray(fat_size * SECTOR)
    def put(c, v):
        struct.pack_into('<I', fat, c * 4, v)
    put(0, 0x0FFFFFF8)           # media descriptor
    put(1, 0x0FFFFFFF)           # end of chain
    put(ROOT_CLUSTER, 0x0FFFFFFF)  # root dir: one cluster, EOF
    for i in range(clusters_needed):
        c = FILE_START_CLUSTER + i
        put(c, FILE_START_CLUSTER + i + 1 if i < clusters_needed - 1 else 0x0FFFFFFF)
    return fat

# ── Directory entry ───────────────────────────────────────────────────────────
def make_dirent(name8, ext3, start_cluster, file_size):
    d = bytearray(32)
    d[0:8]  = name8.ljust(8).encode()
    d[8:11] = ext3.ljust(3).encode()
    d[11]   = 0x20          # ARCHIVE attribute
    struct.pack_into('<H', d, 20, (start_cluster >> 16) & 0xFFFF)
    struct.pack_into('<H', d, 22, 0x5A00)   # write time
    struct.pack_into('<H', d, 24, 0x5A93)   # write date (2026-07-19)
    struct.pack_into('<H', d, 26,  start_cluster       & 0xFFFF)
    struct.pack_into('<I', d, 28, file_size)
    return bytes(d)

# ── Main ──────────────────────────────────────────────────────────────────────
def main():
    elf_path = sys.argv[1] if len(sys.argv) > 1 else 'user/user.elf'
    img_path = sys.argv[2] if len(sys.argv) > 2 else 'usb.img'

    if not os.path.exists(elf_path):
        print(f'[USB] ERROR: {elf_path} not found — run make first')
        sys.exit(1)

    with open(elf_path, 'rb') as f:
        elf = f.read()

    clusters_needed = max(1, math.ceil(len(elf) / (SPC * SECTOR)))
    print(f'[USB] {elf_path}: {len(elf)} bytes → {clusters_needed} cluster(s)')
    print(f'[USB] Image layout: total={total_sectors}s fat_size={fat_size}s data_start={data_start}s')

    # Allocate full image (zeroed)
    img = bytearray(total_sectors * SECTOR)

    # Sector 0: BPB
    bpb = make_bpb()
    img[0:SECTOR] = bpb

    # FAT1 + FAT2
    fat = build_fat(clusters_needed)
    f1  = RESERVED * SECTOR
    f2  = f1 + fat_size * SECTOR
    img[f1 : f1 + len(fat)] = fat
    img[f2 : f2 + len(fat)] = fat

    # Root directory (cluster 2)
    root_off = cluster_sector(ROOT_CLUSTER) * SECTOR
    dirent   = make_dirent('INIT', 'ELF', FILE_START_CLUSTER, len(elf))
    img[root_off : root_off + 32] = dirent

    # File data (cluster 3 onward)
    file_off = cluster_sector(FILE_START_CLUSTER) * SECTOR
    img[file_off : file_off + len(elf)] = elf

    with open(img_path, 'wb') as f:
        f.write(img)

    print(f'[USB] Written: {img_path} ({IMG_MB} MB FAT32)')
    print(f'[USB] INIT.ELF → cluster {FILE_START_CLUSTER}, '
          f'root dir @ sector {cluster_sector(ROOT_CLUSTER)}')
    print(f'[USB] Done. Run: make run')

if __name__ == '__main__':
    main()
