#!/usr/bin/env bash
# build_iso.sh – Styx OS Milestone 1 ISO builder.
#
# This script is a self-contained alternative to `make all`.
# Use it when you want to see every step spelled out, or when
# you are setting up the project for the first time.
#
# Steps:
#   1. Clone/refresh the Limine v8 binary release.
#   2. Build the Limine host utility (needed for bios-install).
#   3. Compile all kernel C sources.
#   4. Link the higher-half kernel ELF.
#   5. Assemble the ISO root tree.
#   6. Call xorriso to create the bootable hybrid ISO.
#   7. Call `limine bios-install` to embed the BIOS bootblock.
#
# Prerequisites (apt):
#   sudo apt install build-essential xorriso git curl
#
# Usage:
#   chmod +x build_iso.sh
#   ./build_iso.sh

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
cd "$SCRIPT_DIR"

ISO="styx.iso"
KERNEL_BIN="kernel/kernel"
LIMINE_DIR="limine-binary"
LIMINE_H="limine.h"

# Colours for pretty output
RED='\033[0;31m'; GRN='\033[0;32m'; YLW='\033[1;33m'; NC='\033[0m'
info()  { echo -e "${GRN}[INFO]${NC}  $*"; }
warn()  { echo -e "${YLW}[WARN]${NC}  $*"; }
die()   { echo -e "${RED}[FAIL]${NC}  $*" >&2; exit 1; }

# ── 1. Limine binary release ──────────────────────────────────────────────
if [[ ! -f "$LIMINE_DIR/limine" ]]; then
    info "Downloading Limine binary release..."
    rm -rf "$LIMINE_DIR"
    curl -fsSL \
        "https://github.com/limine-bootloader/limine/releases/latest/download/limine-binary.tar.gz" \
        | tar -xzf -
    info "Building Limine host utility..."
    make -C "$LIMINE_DIR" CC=cc
else
    info "Limine binary already present – skipping download."
fi

# ── 2. limine.h protocol header ───────────────────────────────────────────
if [[ ! -f "$LIMINE_H" ]]; then
    info "Fetching limine.h..."
    curl -fsSL \
        "https://raw.githubusercontent.com/limine-bootloader/limine/v8.x-binary/limine.h" \
        -o "$LIMINE_H"
else
    info "limine.h already present."
fi

# ── 3. Compile kernel sources ─────────────────────────────────────────────
CFLAGS=(
    -std=gnu11
    -ffreestanding
    -fno-stack-protector
    -fno-stack-check
    -fno-lto
    -fno-PIC
    -m64
    -march=x86-64
    -mcmodel=kernel
    -mno-80387
    -mno-mmx
    -mno-sse
    -mno-sse2
    -mno-red-zone
    -Wall
    -Wextra
    -I.
)

SRCS=(
    kernel/main.c
    kernel/font.c
    kernel/fb.c
    kernel/string.c
)

OBJS=()
for src in "${SRCS[@]}"; do
    obj="${src%.c}.o"
    info "Compiling $src → $obj"
    cc "${CFLAGS[@]}" -c "$src" -o "$obj"
    OBJS+=("$obj")
done

# ── 4. Link kernel ELF ────────────────────────────────────────────────────
info "Linking $KERNEL_BIN..."
ld  -m elf_x86_64           \
    -nostdlib               \
    --no-dynamic-linker     \
    -z max-page-size=0x1000 \
    -T linker.ld            \
    "${OBJS[@]}"            \
    -o "$KERNEL_BIN"

# ── 5. Assemble ISO root ──────────────────────────────────────────────────
info "Assembling ISO root..."
rm -rf iso_root
mkdir -p iso_root/boot/limine
mkdir -p iso_root/EFI/BOOT

cp "$KERNEL_BIN"                            iso_root/boot/kernel

# limine.cfg is our source; copy under both names for compatibility:
#   limine.conf – expected by Limine v6+
#   limine.cfg  – expected by older Limine v5
cp limine.cfg                               iso_root/boot/limine/limine.conf
cp limine.cfg                               iso_root/boot/limine/limine.cfg

# BIOS boot files
cp "$LIMINE_DIR/limine-bios.sys"            iso_root/boot/limine/
cp "$LIMINE_DIR/limine-bios-cd.bin"         iso_root/boot/limine/
cp "$LIMINE_DIR/limine-uefi-cd.bin"         iso_root/boot/limine/

# UEFI boot files
cp "$LIMINE_DIR/BOOTX64.EFI"               iso_root/EFI/BOOT/
cp "$LIMINE_DIR/BOOTIA32.EFI"              iso_root/EFI/BOOT/

# ── 6. xorriso ────────────────────────────────────────────────────────────
info "Creating $ISO with xorriso..."
xorriso -as mkisofs              \
    -R -r -J                     \
    -b boot/limine/limine-bios-cd.bin \
    -no-emul-boot                \
    -boot-load-size 4            \
    -boot-info-table             \
    -hfsplus                     \
    -apm-block-size 2048         \
    --efi-boot boot/limine/limine-uefi-cd.bin \
    -efi-boot-part               \
    --efi-boot-image             \
    --protective-msdos-label     \
    iso_root                     \
    -o "$ISO"

# ── 7. BIOS install ───────────────────────────────────────────────────────
info "Running limine bios-install..."
./"$LIMINE_DIR/limine" bios-install "$ISO"

rm -rf iso_root

info "Build complete → $ISO"
info "Run with:  ./run.sh   or   make run"
