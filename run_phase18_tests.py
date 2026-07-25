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
    print("Interactive shell active! Triggering Emergency 'wipe' Command...")
    type_str("wipe")

    print("Waiting 4 seconds for emergency wipe completion...")
    time.sleep(4.0)

    mon.close()
    proc.terminate()
    try:
        proc.wait(timeout=3)
    except subprocess.TimeoutExpired:
        proc.kill()
    cleanup()

    print("\n=========================================")
    print(" PHASE 18 QEMU VERIFICATION LOG")
    print("=========================================")
    with open(LOG_FILE, "r") as f:
        log_content = f.read()
        print(log_content)
    print("=========================================")

    manifest_verified = "[MANIFEST] PQC Dilithium3 / ECDSA Hybrid Signature: VERIFIED ✓" in log_content
    syscall_triggered = "[SYSCALL] Emergency Wipe triggered by Ring-3 Process" in log_content
    pass1_ok = "[DESTRUCT] Pass 1: CSPRNG noise" in log_content
    pass2_ok = "[DESTRUCT] Pass 2: Zeroes" in log_content
    pass3_ok = "[DESTRUCT] Pass 3: CSPRNG noise" in log_content
    halted_ok = "[DESTRUCT] All key material wiped. System halted." in log_content

    print("\nPhase 18 Verification Metrics:")
    print(f"  - PQC Dilithium Manifest Verification: {'PASSED ✓' if manifest_verified else 'FAILED ✗'}")
    print(f"  - Ring-3 SYS_EMERGENCY_WIPE Syscall:   {'PASSED ✓' if syscall_triggered else 'FAILED ✗'}")
    print(f"  - Pass 1 CSPRNG Noise Erasure:         {'PASSED ✓' if pass1_ok else 'FAILED ✗'}")
    print(f"  - Pass 2 Zero Pattern Erasure:          {'PASSED ✓' if pass2_ok else 'FAILED ✗'}")
    print(f"  - Pass 3 CSPRNG Noise Erasure:         {'PASSED ✓' if pass3_ok else 'FAILED ✗'}")
    print(f"  - Key Material Wiped & System Halted:  {'PASSED ✓' if halted_ok else 'FAILED ✗'}")

    if manifest_verified and syscall_triggered and pass1_ok and pass2_ok and pass3_ok and halted_ok:
        print("\nRESULT: ALL PHASE 18 TESTS PASSED PERFECTLY ✓")
        sys.exit(0)
    else:
        print("\nRESULT: SOME PHASE 18 CHECKS WERE NOT MET ✗")
        sys.exit(1)

if __name__ == "__main__":
    main()
