/* platform.h - the boundary between the portable desktop and the hardware.
 *
 * Everything in src/ is plain C that does not touch hardware.  It only talks
 * to the machine through the functions below.  There are three
 * implementations:
 *   kernel/arch_x86_64.c  - real hardware (framebuffer, PS/2, CMOS clock)
 *   host/test_ui.c        - scripted keys + screen assertions, for tests
 *   host/sh_repl.c        - stubs, for running just the shell on Linux
 * Keeping this boundary small is what makes the system testable.
 */
#ifndef SKARLET_PLATFORM_H
#define SKARLET_PLATFORM_H

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

/* Returns 1 and fills *k if a key is waiting, 0 otherwise. Never blocks. */
int  plat_key_poll(struct key *k);
/* The screen size the platform set up (the desktop adapts to it). */
void plat_display_size(int *w, int *h);
/* Show a finished frame: w x h pixels, 0x00RRGGBB each, row after row. */
void plat_present(const uint32_t *pixels, int w, int h);
void plat_time(struct datetime *t);
/* Total RAM in KiB as reported by the boot loader (0 if unknown). */
uint32_t plat_mem_kib(void);
void plat_reboot(void);
void plat_poweroff(void); /* may return if the machine cannot power off */

/* Mouse events, for platforms that have a pointer (the X11 session); they
 * call ws_mouse() with these.  Buttons: 1 left, 2 middle, 3 right; the wheel
 * arrives as MOUSE_WHEEL with button 4 (up) or 5 (down). */
enum { MOUSE_DOWN, MOUSE_UP, MOUSE_MOVE, MOUSE_WHEEL };
struct mouse {
    int type;
    int x, y;   /* screen coordinates */
    int button;
    int clicks; /* 2 for the second press of a double click */
};

/* Optional hooks for platforms that show other programs' windows (see
 * wm_open_external() in desktop.h).  The core provides empty defaults, so
 * platforms without such windows need not define them. */
void plat_window_close(long ext); /* ask the program to close the window */
/* Check the login password (default: anything is accepted), and the hint
 * shown under the password field. */
int plat_login(const char *password);
const char *plat_login_hint(void);

#endif
