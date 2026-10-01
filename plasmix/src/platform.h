/* platform.h - the boundary between the portable desktop and the hardware.
 *
 * Everything in src/ is plain C that does not touch hardware.  It only talks
 * to the machine through the functions below.  There are three
 * implementations:
 *   kernel/arch_x86_64.c  - real hardware (VGA text memory, PS/2, CMOS clock)
 *   host/host_tty.c       - runs inside a Linux terminal, for trying it out
 *   host/test_ui.c        - scripted keys + screen assertions, for tests
 * Keeping this boundary small is what makes the system testable.
 */
#ifndef PLASMIX_PLATFORM_H
#define PLASMIX_PLATFORM_H

#include <stdint.h>

/* Key codes: printable keys use their ASCII value, everything else is >= 256. */
enum {
    K_NONE = 0,
    K_TAB = '\t',
    K_ENTER = '\n',
    K_BACKSPACE = 8,
    K_ESC = 27,
    K_UP = 256, K_DOWN, K_LEFT, K_RIGHT,
    K_HOME, K_END, K_PGUP, K_PGDN, K_INSERT, K_DELETE,
    K_F1, K_F2, K_F3, K_F4, K_F5, K_F6, K_F7, K_F8, K_F9, K_F10, K_F11, K_F12,
    K_META,
};

enum { MOD_SHIFT = 1, MOD_CTRL = 2, MOD_ALT = 4 };

struct key {
    int code; /* K_* or ASCII; letters arrive lower-case when Ctrl/Alt held */
    int mods; /* MOD_* bit mask */
};

struct datetime {
    int year, month, day;
    int hour, minute, second;
};

#define SCR_W 80
#define SCR_H 25

/* Returns 1 and fills *k if a key is waiting, 0 otherwise. Never blocks. */
int  plat_key_poll(struct key *k);
/* Copy the 80x25 back buffer (VGA format: char | attribute << 8) to the screen. */
void plat_present(const uint16_t *cells);
void plat_time(struct datetime *t);
/* Total RAM in KiB as reported by the boot loader (0 if unknown). */
uint32_t plat_mem_kib(void);
void plat_reboot(void);
void plat_poweroff(void); /* may return if the machine cannot power off */

#endif
