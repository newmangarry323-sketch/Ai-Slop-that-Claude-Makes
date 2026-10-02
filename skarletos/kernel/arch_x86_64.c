/* arch_x86_64.c - the only file that touches real PC hardware.
 *
 * SkarletOS deliberately uses no interrupts: the keyboard and clock are
 * "polled" (we keep asking them whether anything happened).  That costs CPU
 * time but means we need no interrupt descriptor table, no PIC setup and no
 * interrupt handlers, which keeps the kernel small enough to read in one go.
 * Adding interrupts is a good next step if you want to learn more; see the
 * OSDev wiki pages listed in the README.
 *
 * The screen is a "linear framebuffer": a block of graphics card memory
 * where each pixel is a 32-bit number.  We find one in this order:
 *   1. a VMware SVGA II card (VMware, also VirtualBox's "VMSVGA" and QEMU's
 *      -vga vmware), which we program ourselves to exactly 1918 x 1075;
 *   2. a Bochs/QEMU "DISPI" card (QEMU -vga std, Bochs, VirtualBox VBoxVGA),
 *      also programmed directly (its width must be a multiple of 8);
 *   3. whatever mode the boot loader set up (any other machine).
 */
#include <stdarg.h>
#include <stddef.h>
#include <stdint.h>
#include "../src/desktop.h"
#include "../src/lib.h"
#include "../src/platform.h"

/* The size SkarletOS is designed to run at. */
#define WANT_W 1918
#define WANT_H 1075

/* ---- port I/O --------------------------------------------------------------
 * x86 has a separate 64 KiB "I/O port" address space used by older devices
 * (keyboard controller, CMOS clock, PCI configuration, VGA registers).  The
 * in/out instructions read and write it, 8, 16 or 32 bits at a time. */

static inline void outb(uint16_t port, uint8_t v) { __asm__ volatile("outb %0, %1" ::"a"(v), "Nd"(port)); }
static inline void outw(uint16_t port, uint16_t v) { __asm__ volatile("outw %0, %1" ::"a"(v), "Nd"(port)); }
static inline void outl(uint16_t port, uint32_t v) { __asm__ volatile("outl %0, %1" ::"a"(v), "Nd"(port)); }
static inline uint8_t inb(uint16_t port)
{
    uint8_t v;
    __asm__ volatile("inb %1, %0" : "=a"(v) : "Nd"(port));
    return v;
}
static inline uint16_t inw(uint16_t port)
{
    uint16_t v;
    __asm__ volatile("inw %1, %0" : "=a"(v) : "Nd"(port));
    return v;
}
static inline uint32_t inl(uint16_t port)
{
    uint32_t v;
    __asm__ volatile("inl %1, %0" : "=a"(v) : "Nd"(port));
    return v;
}
static inline void cpu_relax(void) { __asm__ volatile("pause"); }

static inline uint64_t rdmsr(uint32_t msr)
{
    uint32_t lo, hi;
    __asm__ volatile("rdmsr" : "=a"(lo), "=d"(hi) : "c"(msr));
    return ((uint64_t)hi << 32) | lo;
}
static inline void wrmsr(uint32_t msr, uint64_t v)
{
    __asm__ volatile("wrmsr" ::"a"((uint32_t)v), "d"((uint32_t)(v >> 32)), "c"(msr));
}

/* GCC may emit calls to these four even in freestanding code (for struct
 * copies and large initialisers), so a kernel has to provide them.  memcpy
 * and memset use the "rep movsb"/"rep stosb" string instructions, which
 * modern CPUs run many bytes at a time; the desktop copies whole frames
 * (8 MB) with them. */
void *memset(void *d, int v, size_t n)
{
    void *r = d;
    __asm__ volatile("rep stosb" : "+D"(d), "+c"(n) : "a"(v) : "memory");
    return r;
}
void *memcpy(void *d, const void *s, size_t n)
{
    void *r = d;
    __asm__ volatile("rep movsb" : "+D"(d), "+S"(s), "+c"(n) : : "memory");
    return r;
}
void *memmove(void *d, const void *s, size_t n)
{
    unsigned char *a = d;
    const unsigned char *b = s;
    if (a <= b || a >= b + n)
        return memcpy(d, s, n);
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

/* ---- serial port log --------------------------------------------------------
 * COM1 (port 0x3F8) is the simplest output device a PC has.  We write a few
 * lines about the boot to it; QEMU shows them with "-serial stdio", and
 * VMware can save a virtual serial port to a file. */

static void serial_init(void)
{
    outb(0x3F8 + 1, 0x00); /* no interrupts */
    outb(0x3F8 + 3, 0x80); /* set the speed: divisor 1 = 115200 baud */
    outb(0x3F8 + 0, 0x01);
    outb(0x3F8 + 1, 0x00);
    outb(0x3F8 + 3, 0x03); /* 8 data bits, no parity, 1 stop bit */
}

static void klog(const char *fmt, ...)
{
    char buf[160];
    va_list ap;
    va_start(ap, fmt);
    k_vsnprintf(buf, sizeof buf, fmt, ap);
    va_end(ap);
    for (const char *p = buf; *p; p++) {
        for (int i = 0; i < 10000 && !(inb(0x3F8 + 5) & 0x20); i++) /* wait for room */
            cpu_relax();
        outb(0x3F8, (uint8_t)*p);
    }
}

/* ---- VGA text mode, only for error messages ----------------------------- */

static void text_panic(const char *msg)
{
    volatile uint16_t *vga = (volatile uint16_t *)0xB8000;
    for (int i = 0; i < 80 * 25; i++)
        vga[i] = 0x4F20; /* white on red spaces */
    for (int i = 0; msg[i] && i < 80 * 25; i++)
        vga[i] = 0x4F00 | (uint8_t)msg[i];
    for (;;)
        __asm__ volatile("cli; hlt");
}

/* ---- page tables -------------------------------------------------------------
 * boot.S identity-maps the first 4 GiB (virtual address = physical address)
 * with 2 MiB pages.  Here we can map more (a graphics card placed above
 * 4 GiB) and choose how each region is cached:
 *   write-back     normal RAM
 *   write-combining  framebuffers: the CPU batches writes into bursts, which
 *                  makes copying a frame many times faster than "uncached"
 *   uncached       device registers, where every access must happen in order
 * The cache type is picked with three page-table bits (PWT, PCD and PAT)
 * that index an 8-entry table in the PAT model-specific register. */

#define PG_PRESENT 0x1
#define PG_WRITE 0x2
#define PG_PWT 0x8
#define PG_PCD 0x10
#define PG_HUGE 0x80
#define PG_PAT_HUGE 0x1000 /* the PAT bit, in a 2 MiB page entry */
#define PG_ADDR 0x000FFFFFFFFFF000ull

enum cache { CACHE_WB, CACHE_WC, CACHE_UC };

static uint64_t table_pool[16][512] __attribute__((aligned(4096)));
static int table_used;
static int have_wc;

static uint64_t *next_table(uint64_t *entry)
{
    if (!(*entry & PG_PRESENT)) {
        if (table_used == 16)
            text_panic("SkarletOS: out of page tables while mapping the screen.");
        uint64_t *t = table_pool[table_used++];
        for (int i = 0; i < 512; i++)
            t[i] = 0;
        *entry = (uint64_t)(uintptr_t)t | PG_PRESENT | PG_WRITE;
    }
    return (uint64_t *)(uintptr_t)(*entry & PG_ADDR);
}

static void map_region(uint64_t phys, uint64_t len, enum cache type)
{
    uint64_t cr3;
    __asm__ volatile("mov %%cr3, %0" : "=r"(cr3));
    uint64_t *pml4 = (uint64_t *)(uintptr_t)(cr3 & PG_ADDR);
    uint64_t flags = PG_PRESENT | PG_WRITE | PG_HUGE;
    if (type == CACHE_WC && have_wc)
        flags |= PG_PAT_HUGE; /* PAT entry 4, which pat_init() made WC */
    else if (type != CACHE_WB)
        flags |= PG_PCD | PG_PWT; /* PAT entry 3: uncached */
    for (uint64_t a = phys & ~0x1FFFFFull; a < phys + len; a += 0x200000) {
        uint64_t *pdpt = next_table(&pml4[(a >> 39) & 511]);
        uint64_t *pd = next_table(&pdpt[(a >> 30) & 511]);
        pd[(a >> 21) & 511] = a | flags;
    }
    /* Reloading CR3 flushes the CPU's cached translations (the TLB). */
    __asm__ volatile("mov %0, %%cr3" ::"r"(cr3) : "memory");
}

static void pat_init(void)
{
    uint32_t a, b, c, d;
    __asm__ volatile("cpuid" : "=a"(a), "=b"(b), "=c"(c), "=d"(d) : "a"(1), "c"(0));
    if (!(d & (1u << 16))) /* no PAT: framebuffers stay uncached (slower) */
        return;
    /* Entry 4 (bits 32-39) is write-back after reset; make it
     * write-combining (type 1).  Entries 0-3 keep their reset values. */
    uint64_t pat = rdmsr(0x277);
    pat = (pat & ~(0xFFull << 32)) | (1ull << 32);
    wrmsr(0x277, pat);
    have_wc = 1;
}

/* ---- PCI ---------------------------------------------------------------------
 * PCI devices are found by asking every bus/slot/function for its vendor and
 * device ID through the configuration ports 0xCF8 (address) and 0xCFC
 * (data).  "BARs" (base address registers) say where a device's memory or
 * I/O ports are. */

static uint32_t pci_read(int bus, int slot, int fn, int off)
{
    outl(0xCF8, 0x80000000u | (bus << 16) | (slot << 11) | (fn << 8) | (off & 0xFC));
    return inl(0xCFC);
}

static void pci_write(int bus, int slot, int fn, int off, uint32_t v)
{
    outl(0xCF8, 0x80000000u | (bus << 16) | (slot << 11) | (fn << 8) | (off & 0xFC));
    outl(0xCFC, v);
}

struct pci_dev {
    int bus, slot, fn;
};

static int pci_find(uint16_t vendor, uint16_t device, struct pci_dev *out)
{
    for (int bus = 0; bus < 256; bus++)
        for (int slot = 0; slot < 32; slot++)
            for (int fn = 0; fn < 8; fn++) {
                uint32_t id = pci_read(bus, slot, fn, 0);
                if ((id & 0xFFFF) == 0xFFFF) {
                    if (fn == 0)
                        break; /* no device in this slot */
                    continue;
                }
                if ((id & 0xFFFF) == vendor && (id >> 16) == device) {
                    out->bus = bus;
                    out->slot = slot;
                    out->fn = fn;
                    /* Make sure it answers I/O and memory accesses. */
                    uint32_t cmd = pci_read(bus, slot, fn, 4);
                    pci_write(bus, slot, fn, 4, (cmd & 0xFFFF) | 0x3);
                    return 1;
                }
                if (fn == 0 && !(pci_read(bus, slot, 0, 12) & 0x00800000))
                    break; /* not a multi-function device */
            }
    return 0;
}

/* Base address register n: an I/O port number or a (32/64-bit) memory address. */
static uint64_t pci_bar(const struct pci_dev *d, int n)
{
    uint32_t lo = pci_read(d->bus, d->slot, d->fn, 0x10 + 4 * n);
    if (lo & 1)
        return lo & ~3u;
    uint64_t addr = lo & ~0xFu;
    if (((lo >> 1) & 3) == 2) /* 64-bit BAR: the next one holds the top half */
        addr |= (uint64_t)pci_read(d->bus, d->slot, d->fn, 0x14 + 4 * n) << 32;
    return addr;
}

/* ---- the screen ------------------------------------------------------------ */

static struct {
    volatile uint8_t *fb;  /* framebuffer memory */
    int w, h, pitch, bpp;  /* mode: size, bytes per row, bits per pixel */
    int rpos, gpos, bpos;  /* where red/green/blue sit in a pixel */
    int ox, oy;            /* where our picture starts (centred if smaller) */
    int dw, dh;            /* the desktop's size */
    const char *driver;
} scr;

/* What is currently on the screen.  plat_present() only copies the rows
 * that changed, because graphics memory is much slower to write than RAM. */
static uint32_t shown[GFX_MAX_W * GFX_MAX_H];

/* VMware SVGA II, as documented in VMware's open-source "svga_reg.h" (see
 * the README).  Registers are reached through two 32-bit ports: write a
 * register number to the index port, then read/write the value port.
 * Drawing goes straight into the framebuffer, but the card only refreshes
 * the parts it is told about with UPDATE commands placed in a command
 * queue (the "FIFO") that it shares with us in memory. */
enum {
    SVGA_REG_ID = 0, SVGA_REG_ENABLE = 1, SVGA_REG_WIDTH = 2, SVGA_REG_HEIGHT = 3,
    SVGA_REG_MAX_WIDTH = 4, SVGA_REG_MAX_HEIGHT = 5, SVGA_REG_BITS_PER_PIXEL = 7,
    SVGA_REG_BYTES_PER_LINE = 12, SVGA_REG_FB_OFFSET = 14, SVGA_REG_VRAM_SIZE = 15,
    SVGA_REG_MEM_SIZE = 19, SVGA_REG_CONFIG_DONE = 20, SVGA_REG_SYNC = 21, SVGA_REG_BUSY = 22,
};
#define SVGA_ID_2 0x90000002u
#define SVGA_FIFO_MIN 0
#define SVGA_FIFO_MAX 1
#define SVGA_FIFO_NEXT_CMD 2
#define SVGA_FIFO_STOP 3
/* The FIFO memory starts with a few hundred 32-bit registers; we start the
 * command queue after the first 4 KiB, safely past all of them. */
#define SVGA_FIFO_CMDS 4096
#define SVGA_CMD_UPDATE 1

static uint16_t svga_port;
static volatile uint32_t *svga_fifo;

static void svga_out(int reg, uint32_t v)
{
    outl(svga_port, reg);
    outl(svga_port + 1, v);
}

static uint32_t svga_in(int reg)
{
    outl(svga_port, reg);
    return inl(svga_port + 1);
}

static void svga_sync(void)
{
    svga_out(SVGA_REG_SYNC, 1);
    while (svga_in(SVGA_REG_BUSY))
        cpu_relax();
}

static void svga_update(int x, int y, int w, int h)
{
    uint32_t cmd[5] = { SVGA_CMD_UPDATE, (uint32_t)x, (uint32_t)y, (uint32_t)w, (uint32_t)h };
    uint32_t min = svga_fifo[SVGA_FIFO_MIN], max = svga_fifo[SVGA_FIFO_MAX];
    uint32_t next = svga_fifo[SVGA_FIFO_NEXT_CMD];
    for (int i = 0; i < 5; i++) {
        svga_fifo[next / 4] = cmd[i];
        next += 4;
        if (next >= max)
            next = min;
    }
    svga_fifo[SVGA_FIFO_NEXT_CMD] = next;
    /* Wait until the card has handled it, so the queue never fills up. */
    svga_sync();
}

static int svga_init(void)
{
    struct pci_dev d;
    if (!pci_find(0x15AD, 0x0405, &d))
        return 0;
    svga_port = (uint16_t)pci_bar(&d, 0);
    uint64_t fb = pci_bar(&d, 1), fifo = pci_bar(&d, 2);
    svga_out(SVGA_REG_ID, SVGA_ID_2); /* "we speak version 2" */
    if (svga_in(SVGA_REG_ID) != SVGA_ID_2)
        return 0;
    int w = WANT_W, h = WANT_H;
    if ((int)svga_in(SVGA_REG_MAX_WIDTH) < w || (int)svga_in(SVGA_REG_MAX_HEIGHT) < h)
        return 0;
    uint32_t fifo_size = svga_in(SVGA_REG_MEM_SIZE);
    map_region(fifo, fifo_size, CACHE_UC);
    map_region(fb, svga_in(SVGA_REG_VRAM_SIZE), CACHE_WC);
    svga_fifo = (volatile uint32_t *)(uintptr_t)fifo;

    svga_out(SVGA_REG_WIDTH, w);
    svga_out(SVGA_REG_HEIGHT, h);
    svga_out(SVGA_REG_BITS_PER_PIXEL, 32);
    svga_out(SVGA_REG_ENABLE, 1);
    svga_fifo[SVGA_FIFO_MIN] = SVGA_FIFO_CMDS;
    svga_fifo[SVGA_FIFO_MAX] = fifo_size;
    svga_fifo[SVGA_FIFO_NEXT_CMD] = SVGA_FIFO_CMDS;
    svga_fifo[SVGA_FIFO_STOP] = SVGA_FIFO_CMDS;
    svga_out(SVGA_REG_CONFIG_DONE, 1);

    scr.fb = (volatile uint8_t *)(uintptr_t)(fb + svga_in(SVGA_REG_FB_OFFSET));
    scr.w = (int)svga_in(SVGA_REG_WIDTH);
    scr.h = (int)svga_in(SVGA_REG_HEIGHT);
    scr.pitch = (int)svga_in(SVGA_REG_BYTES_PER_LINE);
    scr.bpp = 32;
    scr.rpos = 16;
    scr.gpos = 8;
    scr.bpos = 0;
    scr.driver = "VMware SVGA II";
    return 1;
}

/* Bochs "DISPI" graphics (QEMU's standard VGA, Bochs, VirtualBox's older
 * adapter): registers at ports 0x1CE (index) and 0x1CF (data), the
 * framebuffer in PCI BAR 0. */
enum {
    DISPI_ID = 0, DISPI_XRES = 1, DISPI_YRES = 2, DISPI_BPP = 3, DISPI_ENABLE = 4,
    DISPI_VIRT_WIDTH = 6,
};

static void dispi_out(int reg, uint16_t v)
{
    outw(0x1CE, reg);
    outw(0x1CF, v);
}

static uint16_t dispi_in(int reg)
{
    outw(0x1CE, reg);
    return inw(0x1CF);
}

static int dispi_set(int w, int h)
{
    dispi_out(DISPI_ENABLE, 0);
    dispi_out(DISPI_XRES, w);
    dispi_out(DISPI_YRES, h);
    dispi_out(DISPI_BPP, 32);
    dispi_out(DISPI_ENABLE, 0x41); /* enabled, linear framebuffer */
    return dispi_in(DISPI_XRES) == w && dispi_in(DISPI_YRES) == h;
}

static int dispi_init(void)
{
    struct pci_dev d;
    if (!pci_find(0x1234, 0x1111, &d) && !pci_find(0x80EE, 0xBEEF, &d))
        return 0;
    uint16_t id = dispi_in(DISPI_ID);
    if (id < 0xB0C0 || id > 0xB0CF)
        return 0;
    /* Try the exact size, then the nearest width that is a multiple of 8. */
    if (!dispi_set(WANT_W, WANT_H) && !dispi_set(WANT_W & ~7, WANT_H))
        return 0;
    uint64_t fb = pci_bar(&d, 0);
    scr.w = dispi_in(DISPI_XRES);
    scr.h = dispi_in(DISPI_YRES);
    scr.pitch = dispi_in(DISPI_VIRT_WIDTH) * 4;
    map_region(fb, (uint64_t)scr.pitch * scr.h, CACHE_WC);
    scr.fb = (volatile uint8_t *)(uintptr_t)fb;
    scr.bpp = 32;
    scr.rpos = 16;
    scr.gpos = 8;
    scr.bpos = 0;
    scr.driver = "Bochs DISPI";
    return 1;
}

/* The boot loader's framebuffer, described in the Multiboot information
 * structure (flags bit 12, fields from byte 88 on).  We only handle 24 and
 * 32 bits per pixel, which is what VESA BIOSes and UEFI GOP offer today. */
static int bootfb_init(const uint8_t *mb)
{
    if (!mb || !(*(const uint32_t *)mb & (1u << 12)))
        return 0;
    uint64_t addr = *(const uint64_t *)(mb + 88);
    scr.pitch = *(const uint32_t *)(mb + 96);
    scr.w = *(const uint32_t *)(mb + 100);
    scr.h = *(const uint32_t *)(mb + 104);
    scr.bpp = mb[108];
    if (mb[109] != 1 || (scr.bpp != 32 && scr.bpp != 24))
        return 0; /* text mode or a palette mode: not usable */
    /* The colour layout: bit positions of red, green and blue, each followed
     * by a mask size.  The Multiboot spec's table puts it at byte 110, but
     * boot loaders (Limine's multiboot1.h, GRUB's multiboot.h) align it to
     * byte 112, so that is where it really is. */
    scr.rpos = mb[112];
    scr.gpos = mb[114];
    scr.bpos = mb[116];
    map_region(addr, (uint64_t)scr.pitch * scr.h, CACHE_WC);
    scr.fb = (volatile uint8_t *)(uintptr_t)addr;
    scr.driver = "boot loader framebuffer";
    return 1;
}

static void screen_init(const uint8_t *mb)
{
    pat_init();
    if (!svga_init() && !dispi_init() && !bootfb_init(mb))
        text_panic("SkarletOS needs a graphics framebuffer (VESA/GOP, VMware SVGA or QEMU VGA).");
    klog("screen: %s, %dx%d, %d bits, %d bytes per row, at 0x%x\n", scr.driver, scr.w,
         scr.h, scr.bpp, scr.pitch, (unsigned)(uintptr_t)scr.fb);
    scr.dw = MIN(MIN(scr.w, WANT_W), GFX_MAX_W);
    scr.dh = MIN(MIN(scr.h, WANT_H), GFX_MAX_H);
    scr.ox = (scr.w - scr.dw) / 2;
    scr.oy = (scr.h - scr.dh) / 2;
    /* Black out the whole mode (the border, if we are centred). */
    for (int y = 0; y < scr.h; y++)
        memset((void *)(scr.fb + (size_t)y * scr.pitch), 0, (size_t)scr.w * (scr.bpp / 8));
    for (int i = 0; i < GFX_MAX_W * GFX_MAX_H; i++)
        shown[i] = 1; /* not a valid 0x00RRGGBB colour: forces a full copy */
    if (svga_fifo)
        svga_update(0, 0, scr.w, scr.h);
}

void plat_display_size(int *w, int *h)
{
    *w = scr.dw;
    *h = scr.dh;
}

/* Copy one row of pixels to the screen, converting the format if needed. */
static void put_row(int y, const uint32_t *src, int w)
{
    volatile uint8_t *dst = scr.fb + (size_t)(scr.oy + y) * scr.pitch + (size_t)scr.ox * (scr.bpp / 8);
    if (scr.bpp == 32 && scr.rpos == 16 && scr.gpos == 8 && scr.bpos == 0) {
        memcpy((void *)dst, src, (size_t)w * 4);
        return;
    }
    for (int x = 0; x < w; x++) {
        uint32_t c = src[x];
        uint32_t v = ((c >> 16) & 255) << scr.rpos | ((c >> 8) & 255) << scr.gpos |
                     (c & 255) << scr.bpos;
        if (scr.bpp == 32) {
            ((volatile uint32_t *)dst)[x] = v;
        } else {
            dst[x * 3] = (uint8_t)v;
            dst[x * 3 + 1] = (uint8_t)(v >> 8);
            dst[x * 3 + 2] = (uint8_t)(v >> 16);
        }
    }
}

static int row_same(const uint32_t *a, const uint32_t *b, int n)
{
    for (int i = 0; i < n; i++)
        if (a[i] != b[i])
            return 0;
    return 1;
}

void plat_present(const uint32_t *px, int w, int h)
{
    int first = -1, last = -1;
    for (int y = 0; y < h; y++) {
        const uint32_t *src = px + (size_t)y * w;
        uint32_t *old = shown + (size_t)y * w;
        if (row_same(src, old, w))
            continue;
        put_row(y, src, w);
        memcpy(old, src, (size_t)w * 4);
        if (first < 0)
            first = y;
        last = y;
    }
    if (svga_fifo && first >= 0)
        svga_update(scr.ox, scr.oy + first, w, last - first + 1);
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

/* So a debugger (or the emulator tests) can see which screen driver won. */
const char *volatile g_screen_driver;

void kmain(uint32_t magic, uint32_t info_addr)
{
    const uint8_t *mb = 0;
    if (magic == 0x2BADB002 && info_addr) {
        mb = (const uint8_t *)(uintptr_t)info_addr;
        uint32_t flags = *(const uint32_t *)mb;
        if (flags & (1u << 6)) {
            /* The memory map: entries of {size, base, length, type}; type 1
             * is usable RAM.  "size" does not count its own 4 bytes. */
            uint32_t len = *(const uint32_t *)(mb + 44), addr = *(const uint32_t *)(mb + 48);
            uint64_t total = 0;
            for (uint32_t off = 0; off < len;) {
                const uint8_t *e = (const uint8_t *)(uintptr_t)(addr + off);
                if (*(const uint32_t *)(e + 20) == 1)
                    total += *(const uint64_t *)(e + 12);
                off += *(const uint32_t *)e + 4;
            }
            mem_kib = (uint32_t)(total / 1024);
        } else if (flags & 1) { /* mem_upper: KiB above 1 MiB, up to the first hole */
            mem_kib = *(const uint32_t *)(mb + 8) + 1024;
        }
    }
    serial_init();
    klog("SkarletOS starting, %u KiB of memory\n", mem_kib);
    screen_init(mb);
    g_screen_driver = scr.driver;
    while (inb(0x64) & 1) /* throw away keys pressed during boot */
        (void)inb(0x60);

    ws_init();
    for (;;) {
        ws_tick();
        for (int i = 0; i < 20000; i++)
            cpu_relax();
    }
}
