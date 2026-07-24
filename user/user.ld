/*
 * user.ld — Linker script for StyxOS Ring-3 Executables
 * Base virtual address: 0x0000000000400000
 */

ENTRY(_start)

SECTIONS
{
    . = 0x400000;

    .text : ALIGN(4096) {
        *(.text._start)
        *(.text*)
    }

    .rodata : ALIGN(4096) {
        *(.rodata*)
    }

    .data : ALIGN(4096) {
        *(.data*)
    }

    .bss : ALIGN(4096) {
        *(COMMON)
        *(.bss*)
    }

    /DISCARD/ : {
        *(.comment)
        *(.note*)
        *(.eh_frame*)
    }
}
