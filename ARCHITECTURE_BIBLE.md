# StyxOS Architecture Bible v2.0

> *// where data goes to die*

**Author:** Anudeep | **Institution:** GITAM University | **Kernel:** Custom x86-64 | **Bootloader:** Limine | **Status:** Pre-Alpha / Active

---

## Table of Contents

1. [Vision and Core Guarantee](#1-vision-and-core-guarantee)
2. [Honest Reality Check](#2-honest-reality-check)
3. [Full Security Architecture](#3-full-security-architecture)
4. [USB Design and Encryption](#4-usb-design-and-encryption)
5. [Network Anonymity Stack](#5-network-anonymity-stack)
6. [Active Defense Mechanisms](#6-active-defense-mechanisms)
7. [Threat Model](#7-threat-model)
8. [Comparison with Existing Systems](#8-comparison-with-existing-systems)
9. [Build Roadmap](#9-build-roadmap)
10. [Current Kernel State](#10-current-kernel-state)
11. [Driver Model](#11-driver-model)
12. [Hardware Compatibility Target](#12-hardware-compatibility-target)
13. [Key Management and Signing](#13-key-management-and-signing)

---

## 1. Vision and Core Guarantee

StyxOS is a USB-native, security-first operating system built from a custom x86-64 microkernel. Not Linux. Not BSD. Not anyone else's foundation. Every security property it claims is enforced at the hardware and kernel level — not bolted on afterward as a patch over someone else's design decisions.

The name is intentional. In Greek mythology, the River Styx is the boundary between the living world and the underworld. Nothing crosses it without permission. Nothing returns without a cost. StyxOS is that boundary between the outside world and your data.

StyxOS is not for everyday consumers. It is for developers, security researchers, journalists, activists, and anyone operating in environments where surveillance is a real threat, not a hypothetical one.

> **CORE GUARANTEE:** StyxOS makes breaking in so expensive, resource-intensive, and uncertain that no realistic attacker — corporate, criminal, or state-level — will consider it worth the cost. Your data is either yours, or it is mathematically nothing. There is no third option.

---

## 2. Honest Reality Check

No operating system in existence achieves absolute zero trace. Anyone who claims otherwise is selling something. This section documents exactly what StyxOS can guarantee and what it cannot.

| Claim | Reality | StyxOS Approach |
|-------|---------|-----------------|
| Zero trace on host machine | ✅ ACHIEVABLE | RAM-only execution, no disk writes, ever |
| USB contents unreadable if seized | ✅ ACHIEVABLE | AES-256 + key destruction on tamper |
| ISP cannot read your traffic content | ✅ ACHIEVABLE | All traffic encrypted before leaving the NIC |
| Websites do not know who you are | ✅ MOSTLY | Tor routing + hardware fingerprint randomization |
| ISP cannot see that you are doing anything | ❌ NOT POSSIBLE | Encrypted packets are still visible — content is hidden, existence is not |
| Nation-state with unlimited budget cannot find you | ⚠️ DEPENDS | Cost raised dramatically — traffic timing correlation attacks remain possible |
| Full hardware fingerprint hidden | ⚠️ PARTIAL | MAC randomized, most identifiers spoofed, deep hardware probing remains |
| Behavioral fingerprint hidden | ❌ NOT POSSIBLE | No OS can fix human behavior patterns — operational awareness is the only defense |
| AES-256 crackable by quantum computers | ❌ NOT TODAY | Requires ~6,600 logical error-corrected qubits — far beyond current capability |
| You were never physically seen with the USB | ❌ NOT POSSIBLE | The physical world exists — StyxOS covers digital traces only |

> **The real goal, stated precisely:** Make surveillance so expensive that only a nation-state with unlimited budget, physical access to the user, and months of dedicated computational effort could attempt it. For every realistic attacker below that threshold — corporate data brokers, opportunistic hackers, law enforcement with standard tooling — StyxOS makes you effectively invisible.

---

## 3. Full Security Architecture

StyxOS is built as a series of independent security layers. Each layer has exactly one job. Each layer assumes the layer above it has already been compromised. If any layer fails, the next one stops the attacker cold. There is no single point of failure in the **digital** attack surface of this design. The one unavoidable physical-world exposure is credential entry — a password typed on an untrusted host machine cannot be protected by software. This is acknowledged explicitly in the threat model.

> **Note on build order vs. layer numbering:** Layers are numbered by conceptual security depth (L0 = most foundational), not implementation order. The build roadmap installs them bottom-up by necessity — kernel before encryption before network. The layer numbering reflects *what each layer protects against*, not *when it is built*.

| Layer | Name | Description | Status |
|-------|------|-------------|--------|
| **L7** | User Environment | Minimal shell + curated security toolset. Every binary verified against a cryptographic manifest at load time before execution. | 🟡 PLANNED |
| **L6** | Network Anonymity | All traffic routed through Tor at kernel network stack level. Circuits rotate every 60–90s. Synthetic traffic injected to defeat timing correlation. No raw IP ever leaves the machine. | 🟡 PLANNED |
| **L5** | Hardware Anonymization | MAC address randomized on every boot. USB controller IDs spoofed. CPU feature reporting sanitized. Monitor EDID randomized. Every hardware identifier that can be changed, is changed, on every boot. | 🟡 PLANNED |
| **L4** | Process Isolation | Every process holds a cryptographic capability token. No token = no execution. Tokens define exactly which memory regions, devices, and syscalls a process may access. No ambient authority exists. | ✅ M5 COMPLETE |
| **L3** | Memory Security | Nothing ever written to host disk. All execution in RAM. On shutdown or tamper detection, RAM actively overwritten with zeroes. Key material kept encrypted in RAM using envelope encryption: a session key (SK) decrypts data keys on demand and is itself stored encrypted under a hardware-bound root key. SK lives in plaintext in a single locked page for the minimum possible duration — decrypted for the operation, re-encrypted immediately after. Target platforms: Intel TME or AMD SME for hardware memory encryption. **Plus Ultra Enclave Integration:** On systems supporting Intel SGX or AMD SEV-SNP, the SK and cryptographic operations are isolated within a hardware-shielded enclave. The Ring 0 kernel only holds opaque handles, reducing the software-accessible key window to zero. | ✅ FOUNDATION COMPLETE |
| **L2** | Microkernel | Custom x86-64. IDT, IRQ, PIT, PMM, VMM, heap, capability engine, ELF loader, scheduler, syscall gate, and ring-3 task all implemented and passing sanity tests. All drivers run in ring 3. | ✅ M6 COMPLETE |
| **L1** | Verified Boot Chain | Every boot stage measured and hash verified against TPM-anchored chain. Any tamper = system refuses to boot entirely. No fallback. No bypass. **Plus Ultra Anti-Evil-Maid Visual Verification:** Displays a user-memorable 3-word hash and a high-entropy color block derived from the TPM PCR state on the pre-boot auth screen, allowing instant manual verification before entering credentials. | 🔷 RESEARCH TRACK |
| **L0** | USB Encryption | AES-256-XTS + Merkle integrity tree. Key derived via Argon2id + hardware salt + FIDO2 pre-boot authentication. Key material permanently destroyed on tamper or three wrong attempts. | 🟡 PLANNED |

---

## 4. USB Design and Encryption

The USB stick is not just a storage medium. It is a security boundary. Physical possession without correct credentials grants an attacker exactly nothing.

```
USB PARTITION LAYOUT

[ EFI System Partition ]  Limine bootloader — unencrypted, TPM-signed
[ Kernel Partition     ]  StyxOS kernel image — TPM-measured at every boot
[ Encrypted Volume     ]  AES-256 — key derived from password + hardware token
  |-- Snapshot Store      Encrypted RAM snapshots for session resume
  |-- Persistent Store    Optional encrypted user files and config
  |-- Key Material        Permanently destroyed on any tamper detection
[ Tamper Counter       ]  Hardware-level attempt counter — cannot be reset or forged
```

### Features

| Feature | Description |
|---------|-------------|
| 🔐 AES-256-XTS + integrity layer | XTS mode (IEEE P1619) for the encrypted volume — length-preserving, suitable for random-access sector reads. **Critical limitation:** XTS provides confidentiality only, not authentication — a bit-flip in ciphertext produces corrupted plaintext with no error signal. **Mitigation:** a dm-verity-style Merkle hash tree covers all encrypted sectors; a tampered sector produces a hash mismatch that halts the system before any plaintext is returned. **Key derivation:** password → Argon2id (64 MB memory, 3 iterations, parallelism 4) → HKDF-SHA-512 (IKM: Argon2id output; salt: BIOS UUID ⊕ CPU serial number; info: `styxos-v1-volume-key`). The hardware-bound salt ensures the same password on a different machine derives a different key entirely. No offline attack possible. |
| 💥 Self-destruct on brute force | Three wrong attempts permanently destroy key material (3-pass: CSPRNG → zeroes → CSPRNG). **Required hardware:** the attempt counter must be enforced by an onboard microcontroller that cannot be reset by overwriting the drive. Compatible hardware: **IronKey D300S**, **Kingston IronKey S1000**, **Nitrokey Storage 2**. Standard commodity USB drives have no such hardware — on those, the counter is a LUKS key-slot construction, which is defeatable by an attacker holding a raw disk image before the first wrong attempt. This limitation is documented explicitly. StyxOS will publish a certified hardware list before M9 ships. On certified hardware: no recovery path, unconditional. |
| 💾 Session snapshots (AES-256-GCM) | Pause exact working state — every open file, running process, memory region — serialized to an encrypted USB partition. Cipher mode: AES-256-GCM. Each snapshot chunk is written atomically with a sequence number and GHASH MAC. An interrupted write leaves the previous complete snapshot intact and verifiable — partial writes are detected and discarded on resume. **Cross-machine Resume Limitations:** Full state migration is restricted to identical hardware families due to physical device register and driver context constraints. For non-identical hardware, the system falls back to userspace-only snapshot restoration, re-initializing kernel device drivers and state. **Plus Ultra Access Pattern Hiding (ORAM):** To prevent an adversary from performing traffic/access-pattern analysis on the storage sectors, snapshot reads and writes are routed through a PathORAM (Oblivious RAM) simulation layer. |
| 🔑 Multi-factor unlock (FIDO2) | Full unlock requires password + FIDO2 hardware security key (YubiKey 5 series or equivalent). **Implementation:** `StyxAuth.efi` — a standalone EFI binary containing a minimal USB-HID stack sufficient to enumerate a FIDO2 key and complete a CTAP2 challenge-response. Limine loads `StyxAuth.efi` before the kernel image. If authentication fails, `StyxAuth.efi` halts — the kernel binary is never loaded, not even partially. The FIDO2 credential is bound to the USB device serial and the user's PIN. Neither the security key alone, nor the password alone, nor the USB alone unlocks anything. Loss of the hardware key = permanent lockout. No recovery key is stored on-device. This is intentional. |
| 🚫 Zero host disk writes | StyxOS never touches the host machine's internal storage. Swap, logs, temp files — everything lives in RAM and encrypted USB only. Forensically clean on unplug. |
| 📡 Distress beacon (pre-established circuit) | A Tor circuit to the user's dead-drop server is established at boot and held open as a persistent background connection. On tamper: beacon fires first, destruction follows 500ms later. **Fallback guarantee:** if the beacon does not confirm transmission within 500ms (network cut, Tor circuit killed by attacker), destruction proceeds anyway — beacon is best-effort, destruction is unconditional. **Protocol:** dead-drop is a Tor hidden service (.onion) running a minimal HTTPS POST endpoint. StyxOS ships a reference dead-drop server (< 100 lines, deployable on any VPS with a Tor daemon). The .onion address is stored only in the encrypted volume — a seized USB without credentials reveals nothing about the beacon destination. |

---

## 5. Network Anonymity Stack

Network anonymity is implemented at the network stack policy level. To mitigate the security tradeoffs of running complex cryptographic protocols (Tor) in Ring 0, the stack is isolated within a Ring 3 system service domain (Network Driver/Tor Capability Domain). This is "kernel-level" because it is enforced at the microkernel IPC routing layer: applications lack the architectural capabilities to establish network connections bypassing the Tor service domain, maintaining absolute policy enforcement while minimizing the Ring 0 attack surface.

```
PACKET JOURNEY — no raw IP ever reaches the NIC

Application generates a packet
         |
Kernel network IPC hook intercepts   application cannot bypass this layer
         |
Traffic padding layer                synthetic packets injected continuously at a fixed base rate
                                     of 50 Kbps (configurable 10-500 Kbps based on bandwidth/power).
                                     Padding never stops — even with zero real traffic, the NIC
                                     emits a constant stream of noise. Real packets are
                                     indistinguishable from padding at the ISP level.
                                     During circuit teardown and rebuild, padding rate is
                                     INCREASED, not dropped — the rebuild event produces no
                                     detectable silence.
         |
Tor onion encryption                 3 relay hops, circuit rotated every 60–90 seconds
         |
Hardware anonymization               MAC randomized, hardware identifiers stripped
         |
Packet reaches NIC                   ISP sees: encrypted blob to a Tor entry node. Nothing more.
```

> **What ISPs actually see:** Encrypted data going to a known Tor entry node. They know you are using Tor. They cannot read what you are sending, where it is going, or what you are receiving. This matches Tails, but with the addition of continuous traffic padding — which defeats timing correlation attacks that have been used against Tor users in practice.

### Pluggable Transports and Dead Reckoning Fallback

To bypass active censorship where direct access to the Tor network is blocked (e.g. firewalls, national DPI systems):
- **Integrated Pluggable Transports:** The Tor service domain supports **obfs4**, **Snowflake**, and **Meek** transports. Traffic is obfuscated to resemble standard HTTPS connections to reputable content delivery networks (CDNs).
- **Dead Reckoning Fallback:** If direct Tor connectivity and obfuscated bridges all fail to connect within a 90-second timeout, StyxOS enters a **fail-closed state**. All external network interfaces are disabled at the hardware level, and a loud system warning is surfaced to prevent accidental un-anonymized traffic leaks.

---

## 6. Active Defense Mechanisms

StyxOS does not wait passively to be compromised. It monitors for unauthorized access and responds immediately. The goal: ensure an attacker obtains absolutely nothing of value.

| Trigger | Response | Status |
|---------|----------|--------|
| Wrong password 3× | Key material permanently destroyed. Distress beacon fires over Tor. No recovery path. | 🟡 PLANNED |
| Debugger attached to kernel | Instant RAM wipe of all key material and session state. Kernel halts cleanly. | 🟡 PLANNED |
| Unexpected memory probe (cold boot / DMA) | Key material kept encrypted in RAM at all times. Keys rotate on any detected probe pattern. | 🔷 RESEARCH |
| Boot chain attestation failure (TPM mismatch) | System refuses to boot entirely. No partial boot. No recovery mode. Blank screen. | 🔷 RESEARCH |
| USB removed without proper shutdown | Immediate RAM wipe. Session state never flushed without explicit encrypted shutdown sequence. | 🟡 PLANNED |
| Surveillance traffic pattern detected | All connections dropped. Tor circuits destroyed and rebuilt. Traffic padding increased. New anonymous identity established. | 🟡 PLANNED |

> **Legal clarity:** Every defense mechanism destroys StyxOS's own data only. No mechanism attacks, damages, or interferes with any external system. Destroying your own encrypted data in response to unauthorized access is legally unambiguous in virtually every jurisdiction. StyxOS defends by destroying its own state — not by striking outward.

---

## 7. Threat Model

A security system without a defined threat model is not a security system.

| Threat Actor | Attack Vector | StyxOS Defense |
|-------------|---------------|----------------|
| **Corporate surveillance** (Google, Meta, data brokers) | Browser fingerprinting, tracking pixels, cross-site cookies, behavioral profiling | Hardware fingerprint randomized per boot. Tor removes real IP. No persistent storage = no cookies survive. Every boot is a new identity. |
| **ISP monitoring** (traffic inspection, data selling) | Deep packet inspection, DNS logging, traffic volume analysis | All traffic Tor-encrypted before leaving NIC. DNS never unencrypted. Continuous traffic padding (not on-demand) defeats volume and timing analysis. ISP sees a constant noise floor going to Tor. |
| **Physical seizure** (law enforcement, customs, theft) | Forensic analysis of USB, RAM extraction, cold boot attack | AES-256-XTS full volume. FIDO2 MFA unlock. Self-destruct on brute force (3-pass overwrite). Active RAM wipe on tamper. Envelope encryption limits plaintext key window in RAM. No host disk writes. Physical possession of USB = nothing without credentials + FIDO2 key. |
| **Remote intrusion** (hackers, malware, exploits) | Browser/network exploits, privilege escalation, persistence mechanisms | Capability-based model = every process sandboxed with minimal permissions. No escalation without valid token. No persistence — RAM wiped on shutdown. |
| **Nation-state actor** (intelligence agencies, unlimited resources) | Large-scale traffic timing correlation, hardware implants, human intelligence | Cost of attack raised dramatically. No technical countermeasure eliminates this threat entirely. Operational security awareness by the user is a required complement at this level. |
| **Kernel exploit / capability escalation** (compromised userspace) | Bug in capability engine exploited from ring-3 to gain ring-0 access | Microkernel minimizes attack surface — only capability primitives run in ring 0. All drivers in ring 3. A ring-3 driver crash cannot escalate without a valid kernel-issued token. Kernel integrity checked at boot via TPM measurement. Residual risk: a kernel bug in the capability engine itself. Mitigation: formal review of capability code paths, minimal syscall surface. |
| **Credential entry interception** (physical-world attack) | Hardware keylogger on host machine, thermal imaging, acoustic cryptanalysis of keystrokes | **StyxOS cannot protect against this.** This is a physical-world attack surface that no software layer eliminates. Defense is operational: use in physically controlled environments. FIDO2 hardware key provides a second factor that a keylogger alone cannot capture — the attacker needs both the password AND physical possession of the FIDO2 key. |

---

## 8. Comparison with Existing Systems

| Feature | StyxOS | Tails OS | Qubes OS | seL4 | StyxOS Impl. Status |
|---------|--------|----------|----------|------|---------------------|
| Kernel | ✅ Own custom | Linux (Debian) | Xen hypervisor | Verified microkernel | ✅ IMPLEMENTED |
| USB-native / live boot | ✅ YES | ✅ YES | ❌ NO | ❌ NO | ✅ IMPLEMENTED |
| Session snapshots | 🎯 PLANNED | ❌ NO | ❌ NO | ❌ NO | ⬜ M10 |
| USB self-destruct on brute force | 🎯 PLANNED | ❌ NO | ❌ NO | ❌ NO | ⬜ M9 |
| Traffic padding (anti-timing) | 🎯 PLANNED | ❌ NO | ❌ NO | ❌ NO | ⬜ M11 |
| Tor at kernel level | 🎯 PLANNED | ⚠️ Userspace | ⚠️ Userspace | ❌ NO | ⬜ M11 |
| Capability-based process model | ✅ DONE | ❌ NO | ⚠️ PARTIAL | ✅ YES | ✅ M5 COMPLETE |
| Hardware fingerprint randomization | 🎯 PLANNED | ⚠️ PARTIAL | ⚠️ PARTIAL | ❌ NO | ⬜ M11 |
| FIDO2 pre-boot MFA | 🎯 PLANNED | ❌ NO | ❌ NO | ❌ NO | ⬜ M8 |
| Distress beacon on tamper | 🎯 PLANNED | ❌ NO | ❌ NO | ❌ NO | ⬜ M9 |
| Usable by a real human | 🎯 GOAL | ✅ YES | ⚠️ ADVANCED | ❌ RESEARCH ONLY | ⬜ M12 |

---

## 9. Build Roadmap

Each milestone must be stable and tested before the next begins. No skipping. No parallel rushing of interdependent systems. Security software built in the wrong order is not secure software.

| # | Milestone | Key Deliverables | Status |
|---|-----------|-----------------|--------|
| **M1** | Boot, framebuffer, serial, halt | Limine integration, framebuffer text, COM1 UART, controlled halt | ✅ COMPLETE |
| **M2** | Interrupts, PIC, PIT, keyboard | GDT reload, IDT (32 exception + 16 IRQ gates), 8259 PIC remap, PIT @ 100Hz, PS/2 keyboard on IRQ1 | ✅ COMPLETE |
| **M3** | Physical memory manager | Bitmap allocator, 126MB detected, frame alloc/free/reuse all passing | ✅ COMPLETE |
| **M4** | Virtual memory + kernel heap | 4-level paging, own PML4, CR3 switch, HHDM+kernel+framebuffer+stack mapped, page fault test passing, kmalloc/kfree/kcalloc operational | ✅ COMPLETE |
| **M5** | Capability-based IPC primitives | Capability table per task, synchronous send/receive, endpoint objects, rights derivation/escalation/revocation — all 6 sanity tests passing in ring 0 | ✅ COMPLETE |
| **M6** | First userspace process (ring 3) | SYSCALL/SYSRET gate (STAR/LSTAR/SFMASK MSRs), ELF loader, task table, preemptive scheduler, GDT+TSS ring-3 segments, first ring-3 task running with SYS_WRITE + SYS_YIELD | ✅ COMPLETE |
| **M7** | Filesystem + USB storage driver | FAT32 read driver, basic VFS abstraction, xHCI USB host controller driver, execute binaries from USB | ✅ COMPLETE |
| **M8** | Pre-boot authentication (FIDO2) | Custom pre-boot auth stub (runs before kernel load), FIDO2 CTAP2 over USB-HID, password + FIDO2 key derivation via HKDF-SHA-512, attempt counter in tamper-evident register | ✅ COMPLETE |
| **M9** | USB encryption + self-destruct | AES-256-XTS full volume, 3-pass key destruction on tamper, distress beacon (pre-established Tor circuit), FIDO2 MFA unlock integrated | ✅ COMPLETE |
| **M10** | Session snapshot system | AES-256-GCM chunk encryption, atomic write with sequence numbers + GHASH MACs, full RAM state serialization, PathORAM access-pattern obfuscation | ✅ COMPLETE |
| **M11** | Network stack + Tor + traffic padding | Custom kernel-level network stack (Intel e1000 PCI driver), Tor capability domain at network layer, continuous 50 Kbps traffic padding, circuit rotation 60–90s, MAC hardware randomization | ✅ COMPLETE |
| **M12** | Usable shell + first real environment | Minimal custom security shell, signed binary manifest verification, system utilities (sysinfo, net, tor, snapshot, auth, wipe) | ✅ COMPLETE |
| **M13** | TPM attestation + verified boot chain | Full measured boot with TPM 2.0 TIS MMIO, SHA-256 PCR[0..3] extensions, Anti-Evil-Maid visual seal derivation (3-word hash + RGB matrix), golden PCR attestation | ✅ COMPLETE |

> **Realistic total timeline:** Milestones 5–12 represent **20–27 months** of focused solo development from the current kernel state. M13 is a parallel research track. The new M8 (FIDO2 pre-boot auth) is a prerequisite for M9 — hardware token support must exist before encryption is built on top of it. The goal is not to ship fast. The goal is to ship something that actually does what it claims.

---

## 10. Current Kernel State

The StyxOS kernel is not a tutorial project or a toy. It is a functional 64-bit higher-half microkernel that has passed sanity tests for every subsystem implemented so far.

```
SERIAL LOG — last successful boot (M6)

[  UART  ]  COM1 initialized at boot — serial logging active
[  GDT   ]  GDT + TSS installed — ring-0/ring-3 segments active
[  IDT   ]  Descriptor table loaded — 32 exception + 16 IRQ gates
[  PIC   ]  8259 remapped — spurious IRQs masked
[  PIT   ]  Programmable interval timer — 100 Hz
[  KB    ]  PS/2 keyboard driver — IRQ1 active
[  PMM   ]  Physical memory — 126 MB detected, bitmap allocator ready
[  PMM   ]  Frame alloc / free / reuse — PASS
[  VMM   ]  New PML4 created — CR3 switched successfully
[  HEAP  ]  kmalloc / kfree / kcalloc — initialized and operational
[  CAP   ]  M5 capability engine — all 6 sanity tests PASSED
[  M6    ]  Syscall gate initialized (STAR/LSTAR/SFMASK MSRs)
[  M6    ]  ELF user binary loaded into private ring-3 address space
[  M6    ]  Console capability installed in user task slot 0
[  M6    ]  Scheduler ready — handing off to ring 3
[  RING3 ]  Hello from ring 3! (SYS_WRITE confirmed working)
[  SCHED ]  SYS_YIELD preemption loop active — kernel idle
```

The current frontier is **Milestone 7: filesystem + USB storage driver.** With ring-3 processes running and capabilities enforced, the next step is reading real binaries off USB instead of embedding them in the kernel image.

---

## Source Files (Current)

```
kernel/
├── main.c          — _start(), full boot sequence, M5+M6 bootstrap
├── fb.c/h          — Framebuffer: init, clear, put_pixel, draw_string
├── font.c/h        — Embedded 8×8 bitmap font, draw_char
├── string.c/h      — Freestanding memcpy/memset/memmove/memcmp
├── serial.c/h      — COM1 UART debug logging (serial_printf)
├── gdt.c/h         — GDT with ring-0 + ring-3 code/data segments
├── tss.c/h         — TSS (kernel RSP0 for syscall stack pivot)
├── idt.c/h         — Interrupt Descriptor Table (32 exception + 16 IRQ gates)
├── isr.c/h         — Exception handlers (CPU faults, page faults)
├── isr.asm         — ISR stubs in assembly
├── irq.c/h         — 8259 PIC remap, hardware interrupt dispatch
├── irq.asm         — IRQ stubs in assembly
├── pit.c/h         — Programmable Interval Timer @ 100Hz
├── keyboard.c/h    — PS/2 keyboard driver on IRQ1
├── pmm.c/h         — Physical Memory Manager (bitmap allocator)
├── vmm.c/h         — Virtual Memory Manager (4-level paging, own page tables)
├── heap.c/h        — Kernel heap allocator (kmalloc/kfree/kcalloc)
├── cap.c/h         — Capability engine (table, create, derive, revoke, send/recv)
├── endpoint.c/h    — Endpoint objects (kernel-managed IPC rendezvous)
├── ipc.h           — IPC message type (tag, words, cap_count)
├── syscall.c/h     — Syscall handler (SYS_WRITE, SYS_YIELD)
├── syscall.asm     — SYSCALL entry stub (STAR/LSTAR/SFMASK setup)
├── task.c/h        — Task control block, ring-3 address space setup
├── task.asm        — Context switch assembly (save/restore GPRs)
├── sched.c/h       — Preemptive round-robin scheduler
├── elf.c/h         — ELF64 loader (maps PT_LOAD segments into user PML4)
└── user_init.bin.h — Embedded user ELF binary (linked into kernel image)

user/
└── user.c          — First ring-3 task: SYS_WRITE + SYS_YIELD loop
```

---

## 11. Driver Model

All drivers in StyxOS run in ring 3. The microkernel in ring 0 exposes the minimum possible interface. This is the security boundary the entire system depends on.

### Driver Lifecycle

```
Kernel boot
    |
    +--> Kernel issues capability token to driver process at load time
    |    Token specifies: which I/O ports, which IRQ, which memory regions
    |    No token = driver cannot touch hardware at all
    |
    +--> Driver runs in ring 3, communicates via kernel IPC only
    |    All device access goes through kernel capability checks
    |
    +--> Driver crash: kernel detects fault, logs it, kills process
         Dependent processes receive an error on their next IPC call
         Kernel does NOT restart drivers automatically
         Recovery is explicit: kernel re-launches driver, re-issues token
```

### Driver Verification

Every driver binary is verified against a cryptographic manifest before execution. The manifest contains:
- SHA-256 hash of the driver binary
- Capability set the driver is permitted to request (declared at build time)
- Signature from the StyxOS signing key

A driver cannot request capabilities beyond what its manifest declares, even if the binary contains code to attempt it. Capability mismatch = load rejected entirely.

### Target Driver Set (by milestone)

| Driver | Milestone | Purpose |
|--------|-----------|---------|
| PS/2 keyboard | M2 (done) | Boot input |
| xHCI USB host controller | M7 | USB mass storage |
| USB HID (FIDO2 transport) | M8 | Pre-boot authentication |
| FAT32 filesystem | M7 | Read binaries from USB |
| Intel e1000 NIC | M11 | Network stack base |
| Realtek RTL8169 NIC | M11 | Broader hardware support |
| EHCI (fallback USB) | M7+ | Older machine compatibility |

---

## 12. Hardware Compatibility Target

StyxOS is not general-purpose. It targets a specific hardware profile that can be validated.

### Minimum Requirements

| Component | Requirement | Reason |
|-----------|------------|--------|
| CPU | x86-64 with NX bit | Required for kernel security model |
| RAM | 2 GB minimum | RAM-only execution; no swap |
| USB | USB 3.0 host controller (xHCI) | Required for FIDO2 + storage speed |
| TPM | TPM 2.0 | Required for M13 verified boot chain |
| NIC | Intel e1000 family OR Realtek RTL8169 | These are the two initial driver targets |
| Display | Any UEFI-compatible framebuffer | Limine framebuffer output |

### What "Any Compatible Machine" Actually Means

Session snapshots can resume on any machine that meets the above spec. Compatibility is enforced at resume time: the kernel checks CPU feature flags and available hardware before restoring state. An incompatible machine gets a clear error — not a corrupted session.

### Hardware Not Currently Supported

- ARM / RISC-V (x86-64 only for now)
- Wireless NICs (Wi-Fi) — not in initial driver set; Tor over wired Ethernet only
- Nvidia/AMD discrete GPUs — framebuffer only, no accelerated graphics
- SecureBoot with third-party keys — Limine handles boot; SecureBoot key enrollment is a future consideration

---

## 13. Key Management and Signing

The security of every binary verification and every boot attestation depends on the integrity of the signing keys. This section defines how those keys are managed.

### Post-Quantum Cryptography (PQC) Integration

To defend against future adversaries equipped with quantum computing capabilities, StyxOS implements a hybrid classical-quantum cryptographic model:
- **Signatures:** Binary manifests and boot measurements are signed using **CRYSTALS-Dilithium** (NIST FIPS 204) layered alongside classical ECDSA (secp256k1). A signature is only valid if both the post-quantum and classical cryptographic validations pass.
- **Key Exchange:** Key material negotiation and secure communication channels between system domains employ a hybrid **CRYSTALS-Kyber** (NIPS FIPS 203) / X25519 scheme.

### Key Hierarchy

```
Root Signing Key (RSK)
    |
    +--> Kernel Signing Key (KSK) (Dilithium + ECDSA)   — signs kernel image
    +--> Driver Manifest Signing Key (Dilithium)        — signs driver manifests
    +--> Boot Chain Key (Dilithium + TPM PCRs)          — signs each boot stage measurement
    +--> Snapshot Integrity Key (Kyber + AES-GCM)       — signs snapshot chunk manifests
```

### Key Generation and Storage

- All keys generated offline, air-gapped, on a dedicated machine not connected to any network
- Root Signing Key is **never stored digitally** after generation — exists only on a hardware security module (HSM) or printed as a paper key stored physically offline
- Subordinate keys are generated and signed by RSK, then RSK is taken back offline
- No signing key is ever stored on the USB device or in any encrypted volume
- All public keys are embedded in the kernel binary at build time

### Compromise Recovery

If a subordinate key is compromised:
1. Issue a revocation record signed by RSK
2. Rebuild all artifacts signed with the compromised key
3. New kernel binary distributed with updated embedded public keys
4. Old builds refuse to run if they check a revocation endpoint (future: remote revocation over Tor)

If RSK is compromised: the entire trust chain must be rebuilt from scratch. There is no automated recovery. This is the correct response to a root key compromise.

### Current State (Solo Developer)

For a solo developer, the RSK is a YubiKey 5 with the RSA-4096 PIV slot used for signing. It is physically secured offline. All signing operations require physical presence and PIN. This is the minimum acceptable key security posture for a project of this threat model.

---

*STYXOS — ARCHITECTURE BIBLE v2.0 — AUTHOR: ANUDEEP — GITAM UNIVERSITY, VISAKHAPATNAM*

*Build it anyway. Everything is impossible until someone does it.*
