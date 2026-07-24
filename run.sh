#!/usr/bin/env bash
# run.sh – Launch Styx OS in QEMU with swtpm TPM 2.0 emulation and e1000 networking.

set -euo pipefail
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
cd "$SCRIPT_DIR"

ISO="styx.iso"

if [[ ! -f "$ISO" ]]; then
    echo "[run.sh] $ISO not found – building first..."
    make all
fi

# Start swtpm TPM 2.0 emulator background daemon if available
TPM_SOCK="/tmp/swtpm-sock"
TPM_DIR="/tmp/swtpm-state"

if command -v swtpm &>/dev/null; then
    mkdir -p "$TPM_DIR"
    if ! pgrep -f "swtpm socket.*$TPM_SOCK" &>/dev/null; then
        echo "[run.sh] Starting swtpm TPM 2.0 daemon..."
        swtpm socket --tpmstate dir="$TPM_DIR" \
                     --ctrl type=unixio,path="$TPM_SOCK" \
                     --tpm2 \
                     --flags not-sessions-taxed &
        sleep 1
    fi
    TPM_FLAGS="-chardev socket,id=chrtpm,path=$TPM_SOCK -tpmdev emulator,id=tpm0,chardev=chrtpm -device tpm-tis,tpmdev=tpm0"
else
    echo "[run.sh] swtpm not found — running without TPM emulation"
    TPM_FLAGS=""
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
    -device e1000,netdev=net0 \
    -netdev user,id=net0 \
    $TPM_FLAGS
