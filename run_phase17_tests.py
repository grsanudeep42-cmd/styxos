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

SCANCODES = {
    'a': '0x1e', 'b': '0x30', 'c': '0x2e', 'd': '0x20', 'e': '0x12',
    'f': '0x21', 'g': '0x22', 'h': '0x23', 'i': '0x17', 'j': '0x24',
    'k': '0x25', 'l': '0x26', 'm': '0x32', 'n': '0x31', 'o': '0x18',
    'p': '0x19', 'q': '0x10', 'r': '0x13', 's': '0x1f', 't': '0x14',
    'u': '0x16', 'v': '0x2f', 'w': '0x11', 'x': '0x2d', 'y': '0x15',
    'z': '0x2c', ' ': '0x39', '\n': '0x1c', 'f2': '0x3c'
}

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
        "-netdev", "user,id=net0",
        "-device", "e1000,netdev=net0",
        "-monitor", f"unix:{MON_SOCK},server,nowait"
    ]

    err_fd = open(ERR_FILE, "w")
    proc = subprocess.Popen(cmd, cwd=PROJECT_DIR, env=env, stdout=err_fd, stderr=err_fd)
    print(f"Started QEMU PID: {proc.pid}")

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

    def type_str(s):
        for ch in s:
            sc = SCANCODES.get(ch, None)
            if sc:
                send_cmd(f"sendkey {sc}")
            time.sleep(0.15)
        send_cmd("sendkey 0x1c")
        time.sleep(1.0)

    print("Waiting for password prompt...")
    while True:
        if os.path.exists(LOG_FILE):
            with open(LOG_FILE, "r") as f:
                content = f.read()
                if "All cryptographic self-tests PASSED." in content:
                    break
        time.sleep(0.2)

    print("Password prompt ready. Sending 'styx' + Enter...")
    type_str("styx")

    print("Waiting for FIDO2 probe...")
    while True:
        if os.path.exists(LOG_FILE):
            with open(LOG_FILE, "r") as f:
                content = f.read()
                if "Probing for physical USB FIDO2 tokens" in content:
                    break
        time.sleep(0.2)

    print("FIDO2 active. Sending F2 (emulator mode)...")
    send_cmd("sendkey 0x3c")
    time.sleep(1.0)

    print("Sending Space (authorize touch)...")
    send_cmd("sendkey 0x39")

    print("Waiting for Ring-3 shell launch...")
    while True:
        if os.path.exists(LOG_FILE):
            with open(LOG_FILE, "r") as f:
                content = f.read()
                if "Launching security shell" in content:
                    break
        time.sleep(0.3)

    time.sleep(2.0)
    print("Interactive shell active! Sending commands...")

    print("Sending 'help'...")
    type_str("help")

    print("Sending 'sysinfo'...")
    type_str("sysinfo")

    print("Sending 'caps'...")
    type_str("caps")

    print("Sending 'pqc'...")
    type_str("pqc")

    print("Sending 'tor'...")
    type_str("tor")

    print("Waiting 3 seconds for output logging...")
    time.sleep(3.0)

    mon.close()
    proc.terminate()
    try:
        proc.wait(timeout=3)
    except subprocess.TimeoutExpired:
        proc.kill()
    cleanup()

    print("\n=========================================")
    print(" PHASE 17 QEMU VERIFICATION LOG")
    print("=========================================")
    with open(LOG_FILE, "r") as f:
        log_content = f.read()
        print(log_content)
    print("=========================================")

    prompt_active = "styxos-ring3>" in log_content
    help_passed = "StyxOS Ring-3 Capability Shell Commands:" in log_content
    sysinfo_passed = "System Status: FREE_MEM=" in log_content
    caps_passed = "Slot 0: CONSOLE_CAP" in log_content
    pqc_passed = "[PQC-USER] Shared secret derived:" in log_content
    tor_passed = "[SHELL-RING3] Transmitted 512-byte Tor onion cell" in log_content

    print("\nPhase 17 Verification Metrics:")
    print(f"  - Ring-3 Shell REPL Prompt: {'PASSED ✓' if prompt_active else 'FAILED 成果' if not prompt_active else 'PASSED ✓'}")
    print(f"  - 'help' Command Exec:     {'PASSED ✓' if help_passed else 'FAILED ✗'}")
    print(f"  - 'sysinfo' Command Exec:  {'PASSED ✓' if sysinfo_passed else 'FAILED ✗'}")
    print(f"  - 'caps' Capability Exec:  {'PASSED ✓' if caps_passed else 'FAILED ✗'}")
    print(f"  - 'pqc' KEM Shared Secret: {'PASSED ✓' if pqc_passed else 'FAILED ✗'}")
    print(f"  - 'tor' Cell Transmission: {'PASSED ✓' if tor_passed else 'FAILED ✗'}")

    if prompt_active and help_passed and sysinfo_passed and caps_passed and pqc_passed and tor_passed:
        print("\nRESULT: ALL PHASE 17 TESTS PASSED PERFECTLY ✓")
        sys.exit(0)
    else:
        print("\nRESULT: SOME PHASE 17 CHECKS WERE NOT MET ✗")
        sys.exit(1)

if __name__ == "__main__":
    main()
