/* arch_x86_64.c - the only file that touches real PC hardware.
 *
 * SkarletOS deliberately uses no interrupts: the keyboard and clock are
 * "polled" (we keep asking them whether anything happened).  That costs CPU
 * time but means we need no interrupt descriptor table, no PIC setup and no
 * interrupt handlers, which keeps the kernel small enough to read in one go.
 * Adding interrupts is a good next step if you want to learn more; see the
 * OSDev wiki pages listed in the README.
 */
#include <stddef.h>
#include <stdint.h>
#include "../src/desktop.h"
#include "../src/platform.h"

/* ---- port I/O --------------------------------------------------------------
 * x86 has a separate 64 KiB "I/O port" address space used by older devices
 * (keyboard controller, CMOS clock, VGA registers).  The in/out instructions
 * read and write it. */

static inline void outb(uint16_t port, uint8_t v) { __asm__ volatile("outb %0, %1" ::"a"(v), "Nd"(port)); }
static inline void outw(uint16_t port, uint16_t v) { __asm__ volatile("outw %0, %1" ::"a"(v), "Nd"(port)); }
static inline uint8_t inb(uint16_t port)
{
    uint8_t v;
    __asm__ volatile("inb %1, %0" : "=a"(v) : "Nd"(port));
    return v;
}
static inline void cpu_relax(void) { __asm__ volatile("pause"); }

/* GCC may emit calls to these four even in freestanding code (for struct
 * copies and large initialisers), so a kernel has to provide them. */
void *memset(void *d, int v, size_t n)
{
    unsigned char *p = d;
    while (n--)
        *p++ = (unsigned char)v;
    return d;
}
void *memcpy(void *d, const void *s, size_t n)
{
    unsigned char *a = d;
    const unsigned char *b = s;
    while (n--)
        *a++ = *b++;
    return d;
}
void *memmove(void *d, const void *s, size_t n)
{
    unsigned char *a = d;
    const unsigned char *b = s;
    if (a < b)
        while (n--)
            *a++ = *b++;
    else
        while (n--)
            a[n] = b[n];
    return d;
}
int memcmp(const void *x, const void *y, size_t n)
{
    const unsigned char *a = x, *b = y;
    for (; n; n--, a++, b++)
        if (*a != *b)
            return *a - *b;
    return 0;
}

/* ---- VGA text mode ------------------------------------------------------- */

#define VGA_MEM ((volatile uint16_t *)0xB8000)

static void vga_init(void)
{
    /* Hide the blinking hardware cursor (CRTC register 0x0A, bit 5). */
    outb(0x3D4, 0x0A);
    outb(0x3D5, 0x20);
    /* Turn attribute bit 7 into "bright background" instead of "blink", so
     * we get 16 background colours.  Reading 0x3DA resets the attribute
     * controller's index/data flip-flop; index 0x10 is the mode control
     * register (0x20 keeps the display enabled); bit 3 is blink enable. */
    (void)inb(0x3DA);
    outb(0x3C0, 0x30);
    uint8_t mode = inb(0x3C1);
    outb(0x3C0, mode & ~0x08);
    /* SkarletOS's accent: turn colour 4 (normally red) into maroon.  In text
     * mode colour 4 is looked up in palette register 4, which selects entry 4
     * of the DAC, the chip that turns colour numbers into analogue red, green
     * and blue.  The DAC takes 6-bit levels (0-63): write the entry number to
     * 0x3C8, then red, green and blue to 0x3C9.  32,0,0 is #820000, the closest
     * a VGA can get to maroon (#800000). */
    outb(0x3C8, 4);
    outb(0x3C9, 32);
    outb(0x3C9, 0);
    outb(0x3C9, 0);
}

void plat_present(const uint16_t *cells)
{
    for (int i = 0; i < SCR_W * SCR_H; i++)
        VGA_MEM[i] = cells[i];
}

/* ---- PS/2 keyboard --------------------------------------------------------
 * The keyboard controller (an "8042") has a status port 0x64 and a data port
 * 0x60.  Status bit 0 means a byte is waiting; bit 5 means it came from the
 * mouse, which we ignore.  Each key press sends a "scan code" (set 1, as
 * translated by the controller); releasing it sends the same code + 0x80.
 * Some keys (arrows, right Ctrl/Alt...) are prefixed by 0xE0. */

static const char keymap[128] = {
    0, 27, '1', '2', '3', '4', '5', '6', '7', '8', '9', '0', '-', '=', 8, '\t',
    'q', 'w', 'e', 'r', 't', 'y', 'u', 'i', 'o', 'p', '[', ']', '\n', 0, 'a', 's',
    'd', 'f', 'g', 'h', 'j', 'k', 'l', ';', '\'', '`', 0, '\\', 'z', 'x', 'c', 'v',
    'b', 'n', 'm', ',', '.', '/', 0, '*', 0, ' ',
};
static const char keymap_shift[128] = {
    0, 27, '!', '@', '#', '$', '%', '^', '&', '*', '(', ')', '_', '+', 8, '\t',
    'Q', 'W', 'E', 'R', 'T', 'Y', 'U', 'I', 'O', 'P', '{', '}', '\n', 0, 'A', 'S',
    'D', 'F', 'G', 'H', 'J', 'K', 'L', ':', '"', '~', 0, '|', 'Z', 'X', 'C', 'V',
    'B', 'N', 'M', '<', '>', '?', 0, '*', 0, ' ',
};

static int kb_shift, kb_ctrl, kb_alt, kb_caps, kb_e0;

static int nav_key(uint8_t sc)
{
    switch (sc) {
    case 0x47: return K_HOME;
    case 0x48: return K_UP;
    case 0x49: return K_PGUP;
    case 0x4B: return K_LEFT;
    case 0x4D: return K_RIGHT;
    case 0x4F: return K_END;
    case 0x50: return K_DOWN;
    case 0x51: return K_PGDN;
    case 0x52: return K_INSERT;
    case 0x53: return K_DELETE;
    }
    return 0;
}

int plat_key_poll(struct key *k)
{
    while (inb(0x64) & 1) {
        uint8_t status = inb(0x64);
        uint8_t sc = inb(0x60);
        if (status & 0x20)
            continue; /* mouse byte */
        if (sc == 0xE0) {
            kb_e0 = 1;
            continue;
        }
        int e0 = kb_e0;
        kb_e0 = 0;
        int release = sc & 0x80;
        sc &= 0x7F;

        /* Modifier keys only change state. */
        if (sc == 0x2A || sc == 0x36) {
            if (!e0) /* E0 2A is a fake shift sent with Print Screen */
                kb_shift = !release;
            continue;
        }
        if (sc == 0x1D) {
            kb_ctrl = !release;
            continue;
        }
        if (sc == 0x38) {
            kb_alt = !release;
            continue;
        }
        if (release)
            continue;
        if (sc == 0x3A) {
            kb_caps ^= 1;
            continue;
        }

        int code = 0;
        if (e0 && (sc == 0x5B || sc == 0x5C))
            code = K_META;
        else if (nav_key(sc))
            code = nav_key(sc);
        else if (sc >= 0x3B && sc <= 0x44)
            code = K_F1 + (sc - 0x3B);
        else if (sc == 0x57)
            code = K_F11;
        else if (sc == 0x58)
            code = K_F12;
        else if (sc < 128 && keymap[sc]) {
            int shifted = kb_shift;
            char c = keymap[sc];
            if (c >= 'a' && c <= 'z')
                shifted ^= kb_caps;
            code = shifted ? keymap_shift[sc] : c;
            /* With Ctrl or Alt held, report the plain lower-case key. */
            if ((kb_ctrl || kb_alt) && c >= 'a' && c <= 'z')
                code = c;
        }
        if (!code)
            continue;
        k->code = code;
        k->mods = (kb_shift ? MOD_SHIFT : 0) | (kb_ctrl ? MOD_CTRL : 0) | (kb_alt ? MOD_ALT : 0);
        return 1;
    }
    return 0;
}

/* ---- CMOS real-time clock -------------------------------------------------
 * The battery-backed clock is read through ports 0x70 (register number) and
 * 0x71 (value).  Values are usually stored as BCD (0x59 means 59). */

static uint8_t cmos(uint8_t reg)
{
    outb(0x70, reg);
    return inb(0x71);
}

static int bcd(int v) { return (v & 0x0F) + (v >> 4) * 10; }

static void rtc_read_raw(uint8_t r[6])
{
    while (cmos(0x0A) & 0x80) /* wait while an update is in progress */
        cpu_relax();
    static const uint8_t regs[6] = { 0x00, 0x02, 0x04, 0x07, 0x08, 0x09 };
    for (int i = 0; i < 6; i++)
        r[i] = cmos(regs[i]);
}

void plat_time(struct datetime *t)
{
    uint8_t a[6], b[6];
    /* Read until two reads agree, so we never see a half-updated time. */
    rtc_read_raw(a);
    for (int tries = 0; tries < 5; tries++) {
        rtc_read_raw(b);
        int same = 1;
        for (int i = 0; i < 6; i++)
            same &= a[i] == b[i];
        if (same)
            break;
        for (int i = 0; i < 6; i++)
            a[i] = b[i];
    }
    uint8_t status_b = cmos(0x0B);
    int pm = a[2] & 0x80;
    int v[6];
    for (int i = 0; i < 6; i++)
        v[i] = (i == 2) ? (a[i] & 0x7F) : a[i];
    if (!(status_b & 0x04)) /* bit 2 clear: values are BCD */
        for (int i = 0; i < 6; i++)
            v[i] = bcd(v[i]);
    if (!(status_b & 0x02) && pm) /* bit 1 clear: 12-hour clock */
        v[2] = (v[2] + 12) % 24;
    t->second = v[0];
    t->minute = v[1];
    t->hour = v[2];
    t->day = v[3];
    t->month = v[4];
    t->year = 2000 + v[5];
}

/* ---- memory size and power ---------------------------------------------- */

static uint32_t mem_kib;

uint32_t plat_mem_kib(void) { return mem_kib; }

void plat_reboot(void)
{
    /* Ask the keyboard controller to pulse the CPU reset line. */
    for (int i = 0; i < 100000 && (inb(0x64) & 0x02); i++)
        cpu_relax();
    outb(0x64, 0xFE);
    /* If that did not work, load an empty IDT and trap: the CPU then
     * "triple faults", which also resets it. */
    struct __attribute__((packed)) { uint16_t limit; uint64_t base; } idt = { 0, 0 };
    __asm__ volatile("lidt %0; int3" ::"m"(idt));
}

void plat_poweroff(void)
{
    /* Emulator-specific ACPI shortcuts documented on the OSDev wiki. Real
     * hardware needs a full ACPI implementation, so there we just stop. */
    outw(0x604, 0x2000);  /* QEMU (newer versions) */
    outw(0xB004, 0x2000); /* Bochs and older QEMU */
    outw(0x4004, 0x3400); /* VirtualBox */
}

/* ---- entry point ------------------------------------------------------------ */

struct multiboot_info {
    uint32_t flags;
    uint32_t mem_lower; /* KiB below 1 MiB */
    uint32_t mem_upper; /* KiB above 1 MiB */
};

void kmain(uint32_t magic, uint32_t info_addr)
{
    if (magic == 0x2BADB002 && info_addr) {
        const struct multiboot_info *mb = (const struct multiboot_info *)(uintptr_t)info_addr;
        if (mb->flags & 1)
            mem_kib = mb->mem_upper + 1024;
    }
    vga_init();
    while (inb(0x64) & 1) /* throw away keys pressed during boot */
        (void)inb(0x60);

    ws_init();
    for (;;) {
        ws_tick();
        for (int i = 0; i < 20000; i++)
            cpu_relax();
    }
}
