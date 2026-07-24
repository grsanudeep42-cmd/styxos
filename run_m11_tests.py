#!/usr/bin/env python3
import os
import sys
import time
import socket
import subprocess

PROJECT_DIR = "/home/anudeep/projects/styxos"
LOG_FILE = os.path.join(PROJECT_DIR, "qemu.log")
ERR_FILE = os.path.join(PROJECT_DIR, "qemu_err.log")
MON_SOCK = "/tmp/qemu-monitor.sock"

def cleanup():
    subprocess.run(["killall", "-9", "qemu-system-x86_64"], stderr=subprocess.DEVNULL)
    if os.path.exists(MON_SOCK):
        try:
            os.remove(MON_SOCK)
        except OSError:
            pass

def main():
    cleanup()
    time.sleep(1)

    if os.path.exists(LOG_FILE):
        os.remove(LOG_FILE)
    open(LOG_FILE, "w").close()

    env = os.environ.copy()
    env["GDK_BACKEND"] = "x11"

    cmd = [
        "qemu-system-x86_64",
        "-M", "q35",
        "-m", "128M",
        "-cdrom", "styx.iso",
        "-boot", "d",
        "-serial", f"file:{LOG_FILE}",
        "-no-reboot",
        "-device", "qemu-xhci,id=xhci",
        "-drive", "if=none,id=usbdisk,file=usb.img,format=raw",
        "-device", "usb-storage,drive=usbdisk,bus=xhci.0",
        "-netdev", "user,id=net0",
        "-device", "e1000,netdev=net0",
        "-monitor", f"unix:{MON_SOCK},server,nowait"
    ]

    err_fd = open(ERR_FILE, "w")
    proc = subprocess.Popen(cmd, cwd=PROJECT_DIR, env=env, stdout=err_fd, stderr=err_fd)
    print(f"Started QEMU PID: {proc.pid}")

    # Wait for monitor socket
    for _ in range(30):
        if os.path.exists(MON_SOCK):
            break
        time.sleep(0.5)

    if not os.path.exists(MON_SOCK):
        print("ERROR: Monitor socket timed out")
        proc.kill()
        sys.exit(1)

    mon = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
    mon.connect(MON_SOCK)
    time.sleep(0.2)
    try:
        mon.recv(1024)
    except Exception:
        pass

    def send_cmd(cmd_str):
        mon.sendall((cmd_str + "\n").encode())
        time.sleep(0.2)

    def send_keys(keys_list):
        for k in keys_list:
            send_cmd(f"sendkey {k}")
            time.sleep(0.15)

    # 1. Wait for crypto self-tests complete
    print("Waiting for password prompt...")
    while True:
        if os.path.exists(LOG_FILE):
            with open(LOG_FILE, "r") as f:
                content = f.read()
                if "All cryptographic self-tests PASSED." in content:
                    break
        time.sleep(0.2)

    print("Password prompt ready. Sending 'styx' + Enter...")
    send_keys(['s', 't', 'y', 'x', 'ret'])

    # 2. Wait for FIDO2 probe
    print("Waiting for FIDO2 probe...")
    while True:
        if os.path.exists(LOG_FILE):
            with open(LOG_FILE, "r") as f:
                content = f.read()
                if "Probing for physical USB FIDO2 tokens" in content:
                    break
        time.sleep(0.2)

    print("FIDO2 active. Sending F2 (emulator mode)...")
    send_cmd("sendkey f2")
    time.sleep(1.0)

    print("Sending Space (authorize touch)...")
    send_cmd("sendkey spc")

    # 3. Wait for boot menu
    print("Waiting for boot menu...")
    while True:
        if os.path.exists(LOG_FILE):
            with open(LOG_FILE, "r") as f:
                content = f.read()
                if "PRESS 't' FOR M9" in content or "[BOOT]" in content:
                    break
        time.sleep(0.2)

    print("Boot menu active. Sending 'n' for M11 Network Tests...")
    send_cmd("sendkey n")
    time.sleep(0.1)
    send_cmd("sendkey n")

    # 4. Wait for test suite completion
    print("Waiting for M11 tests completion...")
    start_t = time.time()
    success = False
    failed = False

    while time.time() - start_t < 45:
        if os.path.exists(LOG_FILE):
            with open(LOG_FILE, "r") as f:
                content = f.read()
                if "M11 INTEGRATION TESTS COMPLETED SUCCESSFULLY" in content:
                    success = True
                    break
                if "[TEST] ERROR" in content:
                    failed = True
                    break
        time.sleep(0.5)

    mon.close()
    proc.kill()
    cleanup()

    print("\n=========================================")
    print(" M11 NETWORK ANONYMITY TEST LOG")
    print("=========================================")
    with open(LOG_FILE, "r") as f:
        print(f.read())
    print("=========================================")

    if success:
        print("\nRESULT: ALL M11 TESTS PASSED ✓")
        sys.exit(0)
    elif failed:
        print("\nRESULT: M11 TESTS FAILED ✗")
        sys.exit(1)
    else:
        print("\nRESULT: TIMEOUT / UNFINISHED")
        sys.exit(2)

if __name__ == "__main__":
    main()
