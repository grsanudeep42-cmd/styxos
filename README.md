# Styx OS

A 64-bit x86-64 microkernel written in C, booted via the
[Limine](https://github.com/limine-bootloader/limine) bootloader.
Tested in QEMU.

---

## Milestone 1 — Boot & framebuffer text

**Goal:** Minimal higher-half kernel that boots and renders text on the
Limine pixel framebuffer.  No memory management, no interrupts, no
userspace — just boot, draw, halt.

**What it does:**
- Clears the framebuffer to black.
- Draws three lines of white text using an embedded 8 × 8 bitmap font.
- Halts cleanly with `cli ; hlt`.

**What it deliberately does NOT do:**
- No IDT / PIC / APIC setup.
- No GDT (Limine provides one).
- No paging code (Limine already set up the higher-half map).
- No FPU / SSE (disabled in compiler flags).
- No userspace or multitasking.

---

## Directory structure

```
styx-os/
├── kernel/
│   ├── main.c      – _start(), Limine requests, framebuffer init, text draw
│   ├── font.c/h    – embedded 8×8 bitmap font + draw_char / draw_string
│   ├── fb.c/h      – framebuffer helpers: init, clear, put_pixel, draw_string
│   └── string.c/h  – freestanding memcpy / memset / memmove / memcmp
├── limine.h        – downloaded automatically (do not edit)
├── linker.ld       – higher-half linker script (virtual base 0xffffffff80000000)
├── limine.cfg      – Limine boot menu config (source file)
├── GNUmakefile     – primary build system
├── build_iso.sh    – standalone build script (same steps as `make all`)
├── run.sh          – `make all` then QEMU BIOS boot
└── README.md       – this file
```

---

## Prerequisites

Install on Ubuntu / Debian:

```bash
sudo apt update
sudo apt install \
    build-essential \   # cc, ld, make
    xorriso         \   # creates the hybrid ISO
    curl            \   # download Limine binaries / limine.h
    git                 # optional, for future source fetching
```

> **QEMU** is needed to actually run the OS:
> ```bash
> sudo apt install qemu-system-x86
> ```

`limine.h` and the Limine binary release (bootloader + host utility) are
downloaded automatically by the Makefile or `build_iso.sh` — **you do not
need to clone Limine yourself**.

---

## Build & run

### Option A — GNU make (recommended)

```bash
cd styx-os
make all        # fetch deps, compile, link, build styx.iso
make run        # build (if needed) then launch QEMU
```

Individual targets:

| Target       | Action                                     |
|--------------|--------------------------------------------|
| `all`        | Build `styx.iso`                           |
| `run`        | Build + launch QEMU (BIOS, serial to tty)  |
| `clean`      | Remove `.o`, `kernel/kernel`, `styx.iso`   |
| `distclean`  | `clean` + delete `limine-binary/`, `limine.h` |

### Option B — shell script

```bash
chmod +x build_iso.sh run.sh
./build_iso.sh   # produces styx.iso
./run.sh         # launches QEMU
```

---

## How the boot flow works

```
QEMU BIOS → Limine bootblock (embedded by bios-install)
          → Limine reads limine.conf from ISO
          → Loads kernel ELF into RAM, maps higher half
          → Jumps to _start() already in 64-bit long mode
          → _start() clears framebuffer, draws text, halts
```

Key design points:

- **No assembly stub.** Limine calls `_start` directly via the SysV
  x86-64 calling convention. We declare it as a plain C function.
- **Pixel framebuffer, not VGA text mode.** Limine provides a
  `limine_framebuffer` struct with `address`, `width`, `height`, and
  `pitch` (bytes per line). We draw glyphs pixel-by-pixel using the
  embedded 8 × 8 bitmap font.
- **Higher-half placement.** The linker script sets `. = 0xffffffff80000000`.
  Limine handles the physical-to-virtual mapping; the kernel never
  touches page tables in Milestone 1.
- **Freestanding.** No libc. `memcpy`, `memset`, `memmove`, `memcmp`
  are implemented in `kernel/string.c`.

---

## Troubleshooting

### `limine.conf` vs `limine.cfg`

Limine v6+ looks for `limine.conf` on the boot partition.  Older v5
binaries looked for `limine.cfg`.  Our build copies `limine.cfg` (the
source file) to the ISO under **both** names, so either binary version
will find it.

### Black screen / no text

- Check QEMU output for `BUG: unhandled fault` – usually means the
  framebuffer pointer math overflowed.  Confirm `fb->pitch` is in
  bytes (it is; we divide by 4 in `fb_init`).
- Run with `-d int,cpu_reset -no-reboot` to catch triple faults:
  ```bash
  qemu-system-x86_64 -M q35 -m 128M -cdrom styx.iso -boot d \
      -serial stdio -no-reboot -d int,cpu_reset 2>&1 | tee qemu.log
  ```

### `ld: unrecognised emulation mode: elf_x86_64`

Your system `ld` might be BFD-only. Try:
```bash
ld --help | grep elf_x86_64
```
If missing, install `binutils` or use `x86_64-linux-gnu-ld`.

### `xorriso: command not found`

```bash
sudo apt install xorriso
```

---

## Milestone roadmap

| Milestone | Goal                                           | Status |
|-----------|------------------------------------------------|--------|
| 1         | Boot, framebuffer text, halt                   | ✅ done |
| 2         | GDT reload + IDT + keyboard interrupt          | ✅ done |
| 3         | Physical memory manager (bitmap allocator)     | ✅ done |
| 4         | Virtual memory / paging (4-level, own tables)  | ✅ done |
| 5         | Capability-based IPC primitives                | 🔵 in progress |
| 6         | First userspace task (ring 3)                  | ⬜ planned |
| 7         | Filesystem + USB storage driver                | ⬜ planned |
| 8         | Pre-boot authentication (FIDO2)                | ⬜ planned |
| 9         | USB encryption + self-destruct                 | ⬜ planned |
| 10        | Session snapshot system                        | ⬜ planned |
| 11        | Network stack + Tor + traffic padding          | ⬜ planned |
| 12        | Usable shell + first real environment          | ⬜ planned |
| 13        | TPM attestation + verified boot chain          | 🔷 research |
