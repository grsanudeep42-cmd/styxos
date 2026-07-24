# GNUmakefile – Styx OS Milestone 1 build system.
#
# Targets:
#   all         – download deps, compile kernel, build styx.iso
#   run         – build then launch QEMU (BIOS boot, serial to stdio)
#   clean       – remove build artefacts (keep limine-binary/)
#   distclean   – remove everything including downloaded limine-binary/
#
# Requirements: cc ld xorriso git (see README §Prerequisites).

.SUFFIXES:

# ── Toolchain ──────────────────────────────────────────────────────────────
CC  := cc
LD  := ld
comma := ,

# ── Kernel compile flags (per Milestone 1 spec) ────────────────────────────
# Note: -fno-PIC + -mcmodel=kernel is the correct combo for a higher-half
# kernel at 0xffffffff80000000. -mcmodel=kernel makes GCC use RIP-relative
# (64-bit) addressing instead of 32-bit absolutes which would truncate.
CFLAGS := \
    -std=gnu11              \
    -ffreestanding          \
    -fno-stack-protector    \
    -fno-stack-check        \
    -fno-lto                \
    -fno-PIC                \
    -m64                    \
    -march=x86-64           \
    -mcmodel=kernel         \
    -mno-80387              \
    -mno-mmx                \
    -mno-sse                \
    -mno-sse2               \
    -mno-red-zone           \
    -Wall                   \
    -Wextra                 \
    -I.

# ── Linker flags ───────────────────────────────────────────────────────────
LDFLAGS := \
    -m elf_x86_64           \
    -nostdlib               \
    --no-dynamic-linker     \
    -z max-page-size=0x1000 \
    -T linker.ld

# ── Sources & outputs ──────────────────────────────────────────────────────
SRCS := kernel/main.c kernel/font.c kernel/fb.c kernel/string.c \
        kernel/idt.c kernel/isr.c kernel/irq.c kernel/pit.c \
        kernel/keyboard.c kernel/serial.c kernel/pmm.c kernel/vmm.c \
        kernel/heap.c kernel/endpoint.c kernel/cap.c \
        kernel/gdt.c \
        kernel/tss.c \
        kernel/task.c \
        kernel/sched.c \
        kernel/syscall.c \
        kernel/elf.c \
        kernel/pci.c \
        kernel/xhci.c \
        kernel/usb_msc.c \
        kernel/fat32.c \
        kernel/vfs.c \
        kernel/crypto.c \
        kernel/usb_hid.c \
        kernel/fido2.c \
        kernel/auth.c \
        kernel/aes.c \
        kernel/xts.c \
        kernel/integrity.c \
        kernel/destruct.c \
        kernel/gcm.c \
        kernel/oram.c \
        kernel/snapshot.c

ASMS := kernel/isr.asm kernel/irq.asm kernel/task.asm kernel/syscall.asm
OBJS := $(SRCS:.c=.o) $(ASMS:.asm=.asm.o)
KERNEL := kernel/kernel
ISO    := styx.iso



# ── Phony targets ──────────────────────────────────────────────────────────
.PHONY: all run usb clean distclean

all: $(ISO)

# ── Fetch limine.h (header only, not the binary release) ──────────────────
# We grab it from the limine-binary repo after it has been cloned.
# If you already ran build_iso.sh this will already exist.
limine.h:
	@echo "[DEPS] Fetching limine.h..."
	curl -fsSL \
	  "https://raw.githubusercontent.com/limine-bootloader/limine/v8.x-binary/limine.h" \
	  -o limine.h

# ── Limine binary release (the 'limine' host utility + boot files) ─────────
limine-binary/limine:
	@echo "[DEPS] Downloading Limine binary release..."
	rm -rf limine-binary
	curl -fsSL \
	  "https://github.com/limine-bootloader/limine/releases/latest/download/limine-binary.tar.gz" \
	  | tar -xzf -
	$(MAKE) -C limine-binary CC="$(CC)"

# ── Compile each kernel C source to an object file ────────────────────────
%.o: %.c limine.h
	@echo "[CC] $<"
	$(CC) $(CFLAGS) -c $< -o $@

# ── Assemble each assembly source to an object file ───────────────────────
%.asm.o: %.asm
	@echo "[AS] $<"
	$(CC) $(CFLAGS) -x assembler-with-cpp -c $< -o $@

# ── Link the kernel ELF ───────────────────────────────────────────────────
$(KERNEL): $(OBJS)
	@echo "[LD] $(KERNEL)"
	$(LD) $(LDFLAGS) $(OBJS) -o $(KERNEL)

# ── Build the bootable ISO ─────────────────────────────────────────────────
$(ISO): $(KERNEL) limine-binary/limine
	@echo "[ISO] Building $(ISO)..."
	rm -rf iso_root
	mkdir -p iso_root/boot/limine iso_root/EFI/BOOT
	cp $(KERNEL)                              iso_root/boot/kernel
	cp limine.cfg                             iso_root/boot/limine/limine.conf
	cp limine.cfg                             iso_root/boot/limine/limine.cfg
	cp limine-binary/limine-bios.sys          iso_root/boot/limine/
	cp limine-binary/limine-bios-cd.bin       iso_root/boot/limine/
	cp limine-binary/limine-uefi-cd.bin       iso_root/boot/limine/
	cp limine-binary/BOOTX64.EFI              iso_root/EFI/BOOT/
	cp limine-binary/BOOTIA32.EFI             iso_root/EFI/BOOT/
	xorriso -as mkisofs -R -r -J \
	    -b boot/limine/limine-bios-cd.bin     \
	    -no-emul-boot -boot-load-size 4       \
	    -boot-info-table -hfsplus             \
	    -apm-block-size 2048                  \
	    --efi-boot boot/limine/limine-uefi-cd.bin \
	    -efi-boot-part --efi-boot-image       \
	    --protective-msdos-label              \
	    iso_root -o $(ISO)
	./limine-binary/limine bios-install $(ISO)
	rm -rf iso_root
	@echo "[ISO] Done: $(ISO)"

# ── Run in QEMU (BIOS) with xHCI USB disk ─────────────────────────────────
run: $(ISO)
	qemu-system-x86_64        \
	    -M q35                \
	    -m 128M               \
	    -cdrom $(ISO)         \
	    -boot d               \
	    -serial stdio         \
	    -no-reboot            \
	    -device qemu-xhci,id=xhci \
	    $(if $(wildcard usb.img),-drive if=none$(comma)id=usbdisk$(comma)file=usb.img$(comma)format=raw -device usb-storage$(comma)drive=usbdisk$(comma)bus=xhci.0,)

# ── Build FAT32 USB test image ─────────────────────────────────────────────
usb:
	bash make_usb_img.sh

# ── Clean ──────────────────────────────────────────────────────────────────
clean:
	rm -f $(OBJS) $(KERNEL) $(ISO)
	rm -rf iso_root

distclean: clean
	rm -f limine.h
	rm -rf limine-binary
