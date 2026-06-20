/* linker.ld – Styx OS higher-half kernel linker script for x86-64 + Limine.
 *
 * The kernel is linked as a static PIE at virtual base 0xffffffff80000000.
 * Limine loads the physical segments into low RAM and installs the
 * higher-half mapping before handing control to _start.
 *
 * Section layout follows the Limine Bare Bones specification exactly:
 *   .limine_requests_start  – start sentinel
 *   .limine_requests        – Limine protocol request structs
 *   .limine_requests_end    – end sentinel
 *   .text                   – executable code (PT_LOAD, R|X)
 *   .rodata                 – read-only data (PT_LOAD, R)
 *   .data                   – read-write data + .bss (PT_LOAD, R|W)
 */

OUTPUT_FORMAT("elf64-x86-64")
OUTPUT_ARCH(i386:x86-64)

ENTRY(_start)

PHDRS {
    limine_requests PT_LOAD FLAGS((1 << 2) | (1 << 0));   /* R + X */
    text            PT_LOAD FLAGS((1 << 2) | (1 << 0));   /* R + X */
    rodata          PT_LOAD FLAGS((1 << 2));               /* R     */
    data            PT_LOAD FLAGS((1 << 2) | (1 << 1));   /* R + W */
}

SECTIONS {
    . = 0xffffffff80000000;

    .limine_requests : {
        KEEP(*(.limine_requests_start))
        KEEP(*(.limine_requests))
        KEEP(*(.limine_requests_end))
    } :limine_requests

    . = ALIGN(CONSTANT(MAXPAGESIZE));

    .text : {
        *(.text .text.*)
    } :text

    . = ALIGN(CONSTANT(MAXPAGESIZE));

    .rodata : {
        *(.rodata .rodata.*)
    } :rodata

    . = ALIGN(CONSTANT(MAXPAGESIZE));

    .data : {
        *(.data .data.*)
    } :data

    .bss : {
        *(.bss .bss.*)
        *(COMMON)
    } :data

    /* Discard DWARF unwinding tables and debug info. */
    /DISCARD/ : {
        *(.eh_frame*)
        *(.note .note.*)
    }
}
