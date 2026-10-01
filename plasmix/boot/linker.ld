/* linker.ld - where each part of the kernel goes in memory.
 * The kernel is loaded at 1 MiB, the traditional spot above the BIOS area.
 * The Multiboot header must be in the first 8 KiB of the file, so its
 * section comes first. */
OUTPUT_FORMAT(elf64-x86-64)
ENTRY(_start)

/* Two program segments: code + read-only data (read/execute), and
 * data + bss (read/write). */
PHDRS
{
    text PT_LOAD FLAGS(5);
    data PT_LOAD FLAGS(6);
}

SECTIONS
{
    . = 1M;
    _load_start = .;

    .text : {
        *(.multiboot)
        *(.text .text.*)
    } :text
    .rodata : ALIGN(16) { *(.rodata .rodata.*) } :text
    .data : ALIGN(4096) { *(.data .data.*) } :data
    _load_end = .;

    .bss : ALIGN(4096) {
        *(COMMON)
        *(.bss .bss.*)
    } :data
    _bss_end = .;

    /DISCARD/ : { *(.comment) *(.note*) *(.eh_frame*) }
}
