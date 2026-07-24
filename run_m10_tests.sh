#!/bin/bash
set -e

# Clean up any stale QEMU instances first
echo "Cleaning up old QEMU instances..."
killall -9 qemu-system-x86_64 2>/dev/null || true
sleep 2

# Remove stale monitor socket
MONITOR_SOCK=/tmp/qemu-monitor.sock
rm -f "$MONITOR_SOCK"

# 1. Start QEMU with a unix monitor socket and serial output to qemu.log
export GDK_BACKEND=x11
echo "Starting QEMU..."
rm -f qemu.log
touch qemu.log
qemu-system-x86_64 \
    -M q35 \
    -m 128M \
    -cdrom styx.iso \
    -boot d \
    -serial file:qemu.log \
    -no-reboot \
    -device qemu-xhci,id=xhci \
    -drive if=none,id=usbdisk,file=usb.img,format=raw \
    -device usb-storage,drive=usbdisk,bus=xhci.0 \
    -monitor unix:"$MONITOR_SOCK",server,nowait \
    > qemu_err.log 2>&1 &
QEMU_PID=$!

echo "QEMU PID: $QEMU_PID"

# Wait for the monitor socket to become available
echo "Waiting for QEMU monitor socket..."
for i in $(seq 1 30); do
    [ -S "$MONITOR_SOCK" ] && break
    sleep 0.5
done
if [ ! -S "$MONITOR_SOCK" ]; then
    echo "ERROR: Monitor socket not ready"
    kill -9 $QEMU_PID 2>/dev/null || true
    exit 1
fi
echo "Monitor socket ready: $MONITOR_SOCK"

# Helper: send a key sequence via QEMU monitor using python
# Usage: qemu_sendkey <qcode> [<qcode> ...]
qemu_sendkey() {
    local keys=""
    for k in "$@"; do
        [ -n "$keys" ] && keys="$keys-"
        keys="${keys}${k}"
    done
    python3 -c "
import socket
s = socket.socket(socket.AF_UNIX)
s.connect('$MONITOR_SOCK')
s.sendall(b'sendkey $keys\n')
s.close()
"
    sleep 0.15
}

# Type a string by sending one key at a time via QEMU monitor
qemu_type_string() {
    local str="$1"
    for (( i=0; i<${#str}; i++ )); do
        local ch="${str:$i:1}"
        # Map char to QEMU qcode
        case "$ch" in
            a) qemu_sendkey a ;;
            b) qemu_sendkey b ;;
            c) qemu_sendkey c ;;
            d) qemu_sendkey d ;;
            e) qemu_sendkey e ;;
            f) qemu_sendkey f ;;
            g) qemu_sendkey g ;;
            h) qemu_sendkey h ;;
            i) qemu_sendkey i ;;
            j) qemu_sendkey j ;;
            k) qemu_sendkey k ;;
            l) qemu_sendkey l ;;
            m) qemu_sendkey m ;;
            n) qemu_sendkey n ;;
            o) qemu_sendkey o ;;
            p) qemu_sendkey p ;;
            q) qemu_sendkey q ;;
            r) qemu_sendkey r ;;
            s) qemu_sendkey s ;;
            t) qemu_sendkey t ;;
            u) qemu_sendkey u ;;
            v) qemu_sendkey v ;;
            w) qemu_sendkey w ;;
            x) qemu_sendkey x ;;
            y) qemu_sendkey y ;;
            z) qemu_sendkey z ;;
            ' ') qemu_sendkey spc ;;
        esac
        sleep 0.1
    done
}

# 2. Wait for password prompt to be ready (crypto self-tests done)
echo "Waiting for password prompt to be ready..."
until grep -q "All cryptographic self-tests PASSED." qemu.log 2>/dev/null; do
    sleep 0.2
done
sleep 0.5

echo "Password prompt active! Sending 'styx' via QEMU monitor..."
qemu_type_string "styx"
sleep 0.3
qemu_sendkey ret   # Enter key
echo "Password sent."

# 3. Wait for FIDO2 probe phase
echo "Waiting for FIDO2 probe..."
until grep -q "Probing for physical USB FIDO2 tokens" qemu.log 2>/dev/null; do
    sleep 0.2
done
sleep 0.5

echo "FIDO2 active! Pressing F2 for Emulator Mode..."
qemu_sendkey f2
sleep 1.0

echo "Pressing Space to authorize Touch..."
qemu_sendkey spc

# 4. Wait for the boot menu prompt
echo "Waiting for OS boot menu..."
until grep -q "PRESS 't' FOR M9" qemu.log 2>/dev/null; do
    sleep 0.2
done
sleep 0.5

echo "Boot menu! Pressing 's' for M10 Snapshot Verification Tests..."
qemu_sendkey s

# 5. Wait for the integration tests to complete
echo "Waiting for snapshot verification tests to finish..."
TIMEOUT=180
ELAPSED=0
while ! grep -q "M10 INTEGRATION TESTS COMPLETED SUCCESSFULLY" qemu.log 2>/dev/null && \
      ! grep -q "\[TEST\] ERROR" qemu.log 2>/dev/null; do
    sleep 0.5
    ELAPSED=$((ELAPSED + 1))
    if [ $ELAPSED -ge $((TIMEOUT * 2)) ]; then
        echo "TIMEOUT: Tests did not complete within ${TIMEOUT}s"
        break
    fi
done

echo ""
echo "========================================="
echo " M10 SNAPSHOT TEST RESULTS"
echo "========================================="
cat qemu.log
echo "========================================="

# Check pass/fail
if grep -q "M10 INTEGRATION TESTS COMPLETED SUCCESSFULLY" qemu.log; then
    echo "RESULT: ALL M10 TESTS PASSED ✓"
    EXIT_CODE=0
elif grep -q "\[TEST\] ERROR" qemu.log; then
    echo "RESULT: M10 TESTS FAILED ✗"
    EXIT_CODE=1
else
    echo "RESULT: M10 TESTS TIMED OUT / INCOMPLETE"
    EXIT_CODE=2
fi

# Clean up
kill -9 $QEMU_PID 2>/dev/null || true
rm -f "$MONITOR_SOCK"
exit $EXIT_CODE
