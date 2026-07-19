#!/bin/bash
# 1. Start QEMU in the background and save serial output to qemu.log
export GDK_BACKEND=x11
echo "Starting QEMU..."
rm -f qemu.log
qemu-system-x86_64 \
    -M q35 \
    -m 128M \
    -cdrom styx.iso \
    -boot d \
    -serial file:qemu.log \
    -no-reboot \
    -device qemu-xhci,id=xhci \
    -drive if=none,id=usbdisk,file=usb.img,format=raw -device usb-storage,drive=usbdisk,bus=xhci.0 > qemu_err.log 2>&1 &
QEMU_PID=$!

# Wait for QEMU window to appear
echo "Waiting for QEMU window..."
sleep 5

# Find QEMU window ID
WID=$(xdotool search --name "QEMU" | head -1)
if [ -z "$WID" ]; then
    echo "ERROR: QEMU window not found!"
    kill $QEMU_PID
    exit 1
fi
echo "Found QEMU window ID: $WID"

# Focus QEMU window and type the password "styx" + Enter
echo "Typing password 'styx'..."
xdotool mousemove 100 100 click 1
xdotool key --delay 100 s t y x Return
sleep 2

# Type F2 to activate emulator
echo "Pressing F2 for touch emulator..."
xdotool key F2
sleep 2

# Type Space to simulate physical touch
echo "Pressing Space to authorize..."
xdotool key space
sleep 2

# Type 't' to trigger the M9 Integration Tests and Self-Destruct
echo "Pressing 't' to run M9 verification and self-destruct..."
xdotool key t
sleep 5

echo "--- QEMU SERIAL OUTPUT LOG ---"
cat qemu.log
echo "------------------------------"

# Clean up
kill $QEMU_PID 2>/dev/null
