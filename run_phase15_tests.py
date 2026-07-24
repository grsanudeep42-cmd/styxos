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

    def send_keys(keys_list):
        for k in keys_list:
            send_cmd(f"sendkey {k}")
            time.sleep(0.15)

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

    print("Waiting for boot menu / Ring-3 init process...")
    time.sleep(3.0)

    mon.close()
    proc.kill()
    cleanup()

    print("\n=========================================")
    print(" PHASE 15 QEMU VERIFICATION LOG")
    print("=========================================")
    with open(LOG_FILE, "r") as f:
        log_content = f.read()
        print(log_content)
    print("=========================================")

    pqc_passed = "All Post-Quantum Cryptography Self-Tests PASSED." in log_content
    init_started = "[INIT-RING3] StyxOS Ring-3 Init process started." in log_content
    kyber_ring3 = "[INIT-RING3] PQC Kyber-1024 Shared Secret derived successfully!" in log_content
    tcp_ring3 = "[INIT-RING3] TCP Connection established to 192.168.1.1:443" in log_content

    print("\nPhase 15 Verification Metrics:")
    print(f"  - Kernel PQC Self-Tests: {'PASSED ✓' if pqc_passed else 'FAILED ✗'}")
    print(f"  - Ring-3 Init Execution: {'PASSED ✓' if init_started else 'FAILED ✗'}")
    print(f"  - Ring-3 Kyber-1024 KEM: {'PASSED ✓' if kyber_ring3 else 'FAILED ✗'}")
    print(f"  - Ring-3 TCP Handshake:  {'PASSED ✓' if tcp_ring3 else 'FAILED ✗'}")

    if pqc_passed and init_started and kyber_ring3 and tcp_ring3:
        print("\nRESULT: ALL PHASE 15 TESTS PASSED PERFECTLY ✓")
        sys.exit(0)
    else:
        print("\nRESULT: SOME PHASE 15 CHECKS WERE NOT MET ✗")
        sys.exit(1)

if __name__ == "__main__":
    main()
