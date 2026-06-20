#!/usr/bin/env bash
# run.sh – Launch Styx OS in QEMU.
#
# Boots styx.iso via the BIOS path (no EFI firmware needed).
# -serial stdio  → kernel serial output (future debug prints) appears here.
# -no-reboot     → stops QEMU on triple fault rather than looping.
#
# Usage:
#   chmod +x run.sh
#   ./run.sh
#
# To boot with UEFI instead, install ovmf and add:
#   -drive if=pflash,unit=0,format=raw,file=/usr/share/OVMF/OVMF_CODE.fd,readonly=on

set -euo pipefail
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
cd "$SCRIPT_DIR"

ISO="styx.iso"

if [[ ! -f "$ISO" ]]; then
    echo "[run.sh] $ISO not found – building first..."
    make all
fi

echo "[run.sh] Launching QEMU..."
exec qemu-system-x86_64 \
    -M q35              \
    -m 128M             \
    -cdrom "$ISO"       \
    -boot d             \
    -display none       \
    -serial stdio       \
    -no-reboot          \
    -d int,cpu_reset    \
    -D qemu_debug.log
