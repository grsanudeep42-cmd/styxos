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

    print("Waiting for Ring-3 boot / TPM Quote verification...")
    time.sleep(3.0)

    mon.close()
    proc.kill()
    cleanup()

    print("\n=========================================")
    print(" PHASE 16 QEMU VERIFICATION LOG")
    print("=========================================")
    with open(LOG_FILE, "r") as f:
        log_content = f.read()
        print(log_content)
    print("=========================================")

    tpm_init_passed = "[TPM2] Measured boot chain initialized" in log_content
    tpm_quote_passed = "[TIS] TPM2_Quote(pcr_mask=0x7)" in log_content
    policy_passed = "[TPM2] Hardware attestation quote" in log_content and "validated against golden PCR policy" in log_content
    ring3_started = "[INIT-RING3] StyxOS Ring-3 Init process started." in log_content

    print("\nPhase 16 Verification Metrics:")
    print(f"  - TPM2 Measured Boot Init: {'PASSED ✓' if tpm_init_passed else 'FAILED ✗'}")
    print(f"  - TPM2 Quote Generation:   {'PASSED ✓' if tpm_quote_passed else 'FAILED ✗'}")
    print(f"  - Golden PCR Policy Check: {'PASSED ✓' if policy_passed else 'FAILED ✗'}")
    print(f"  - Microkernel Boot Gating: {'PASSED ✓' if ring3_started else 'FAILED ✗'}")

    if tpm_init_passed and tpm_quote_passed and policy_passed and ring3_started:
        print("\nRESULT: ALL PHASE 16 TESTS PASSED PERFECTLY ✓")
        sys.exit(0)
    else:
        print("\nRESULT: SOME PHASE 16 CHECKS WERE NOT MET ✗")
        sys.exit(1)

if __name__ == "__main__":
    main()
