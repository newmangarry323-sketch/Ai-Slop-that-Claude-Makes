/* boot.S - from the boot loader to 64-bit C code.
 *
 * A Multiboot boot loader (GRUB, or QEMU's -kernel option) starts us in
 * 32-bit protected mode with paging off.  x86-64 "long mode" can only be
 * entered with paging on, so before we can run 64-bit code we must:
 *   1. check the CPU supports long mode (CPUID),
 *   2. build page tables (we identity-map the first 4 GiB with 2 MiB pages, so
 *      that graphics card memory, usually just below 4 GiB, is reachable),
 *   3. enable PAE, set the LME bit in the EFER register, enable paging,
 *   4. load a GDT with a 64-bit code segment and jump into it.
 * Then we set up a stack and call kmain() in C.
 */

.set MB_MAGIC,    0x1BADB002
/* bit 0: align modules, bit 1: give us a memory map, bit 2: please set a
 * graphics mode (the video fields below), bit 16: the address fields below
 * are valid.  Bit 16 (the "a.out kludge") lets the loader load this image
 * without understanding 64-bit ELF files. */
.set MB_FLAGS,    (1 << 0) | (1 << 1) | (1 << 2) | (1 << 16)
.set MB_CHECKSUM, -(MB_MAGIC + MB_FLAGS)

.section .multiboot, "a"
.align 4
multiboot_header:
    .long MB_MAGIC
    .long MB_FLAGS
    .long MB_CHECKSUM
    .long multiboot_header   /* header_addr   */
    .long _load_start        /* load_addr     */
    .long _load_end          /* load_end_addr */
    .long _bss_end           /* bss_end_addr  */
    .long _start             /* entry_addr    */
    /* Preferred video mode: linear framebuffer (0), 1920 x 1080, 32 bits per
     * pixel.  Firmware rarely offers SkarletOS's 1918 x 1075, so we ask for
     * the nearest common mode; the kernel then sets 1918 x 1075 exactly on
     * graphics cards it can program itself, or centres the desktop. */
    .long 0
    .long 1920
    .long 1080
    .long 32

.section .bss
.align 4096
pml4:   .skip 4096           /* page map level 4 */
pdpt:   .skip 4096           /* page directory pointer table */
pd:     .skip 4 * 4096       /* 4 page directories: 4 x 512 x 2 MiB = 4 GiB */
.align 16
stack_bottom:
    .skip 65536
stack_top:

.section .data
.align 4
mb_magic: .long 0
mb_info:  .long 0

.section .rodata
.align 16
gdt64:
    .quad 0                      /* null descriptor */
    .quad 0x00AF9A000000FFFF     /* 0x08: 64-bit code, ring 0 (L bit set) */
    .quad 0x00CF92000000FFFF     /* 0x10: data, ring 0 */
gdt64_end:
gdt64_ptr:
    .word gdt64_end - gdt64 - 1
    .quad gdt64

no_long_mode_msg:
    .asciz "SkarletOS needs a 64-bit (x86-64) CPU."

.section .text
.code32
.global _start
_start:
    cli
    cld                          /* string instructions count upwards */
    mov $stack_top, %esp
    mov %eax, mb_magic
    mov %ebx, mb_info

    /* 1. Is extended CPUID available, and does it report long mode? */
    mov $0x80000000, %eax
    cpuid
    cmp $0x80000001, %eax
    jb no_long_mode
    mov $0x80000001, %eax
    cpuid
    test $(1 << 29), %edx        /* LM bit */
    jz no_long_mode

    /* 2. Page tables. Clear them first (the loader should zero .bss, but
     * stale entries here would be fatal, so we make sure).
     * Entry flags: Present|Writable = 0x3, plus PageSize (0x80) in the PD. */
    mov $pml4, %edi
    xor %eax, %eax
    mov $(6 * 4096 / 4), %ecx
    rep stosl
    mov $pdpt, %eax
    or $0x3, %eax
    mov %eax, pml4
    /* pdpt[i] -> the i-th page directory, for i = 0..3 (1 GiB each). */
    mov $pd, %eax
    or $0x3, %eax
    xor %ecx, %ecx
4:  mov %eax, pdpt(, %ecx, 8)
    add $4096, %eax
    inc %ecx
    cmp $4, %ecx
    jne 4b
    /* The 2048 directory entries are contiguous, so one loop fills them:
     * entry n maps physical address n * 2 MiB.  The high half of each
     * 8-byte entry stays zero (addresses are below 4 GiB). */
    xor %ecx, %ecx
1:  mov $0x200000, %eax
    mul %ecx                     /* eax = ecx * 2 MiB (wraps only past 4 GiB) */
    or $0x83, %eax
    mov %eax, pd(, %ecx, 8)
    inc %ecx
    cmp $2048, %ecx
    jne 1b

    /* 3. CR3 = top table, CR4.PAE = 1, EFER.LME = 1, CR0.PG = 1. */
    mov $pml4, %eax
    mov %eax, %cr3
    mov %cr4, %eax
    or $(1 << 5), %eax
    mov %eax, %cr4
    mov $0xC0000080, %ecx
    rdmsr
    or $(1 << 8), %eax
    wrmsr
    mov %cr0, %eax
    or $0x80000001, %eax
    mov %eax, %cr0

    /* 4. Load the 64-bit GDT and far-jump to reload CS with the 64-bit segment. */
    lgdt gdt64_ptr
    ljmp $0x08, $long_mode_start

no_long_mode:
    /* Print the message straight into VGA text memory, white on red. */
    mov $no_long_mode_msg, %esi
    mov $0xB8000, %edi
2:  lodsb
    test %al, %al
    jz 3f
    mov $0x4F, %ah
    stosw
    jmp 2b
3:  hlt
    jmp 3b

.code64
long_mode_start:
    mov $0x10, %ax
    mov %ax, %ds
    mov %ax, %es
    mov %ax, %ss
    xor %ax, %ax
    mov %ax, %fs
    mov %ax, %gs
    mov $stack_top, %rsp
    xor %rbp, %rbp

    mov mb_magic, %edi           /* kmain(magic, multiboot_info) */
    mov mb_info, %esi
    call kmain

halt:
    cli
    hlt
    jmp halt
