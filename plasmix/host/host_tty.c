/* host_tty.c - run the Plasmix desktop inside a Linux/macOS terminal.
 *
 * Same desktop code as the kernel; only the platform layer differs: the
 * screen is drawn with ANSI escape codes and keys come from the terminal in
 * "raw" mode.  The terminal needs to be at least 80x25.
 *
 * Many terminals and desktops grab Alt+F1, Alt+F2, Alt+Tab and Ctrl+F1..F4
 * for themselves, so there is also a Ctrl+A prefix (like GNU screen):
 *   Ctrl+A k  Kickoff (Alt+F1)       Ctrl+A r  KRunner (Alt+F2)
 *   Ctrl+A w  toolbox (Alt+F12)      Ctrl+A t  next window (Alt+Tab)
 *   Ctrl+A x  close window (Alt+F4)  Ctrl+A m  move window (Alt+F7)
 *   Ctrl+A 1-4  virtual desktop      Ctrl+A d  dashboard (Ctrl+F12)
 *   Ctrl+A s  System Activity        Ctrl+A arrow  Alt+arrow (move widget)
 *   Ctrl+A q  quit
 */
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/select.h>
#include <termios.h>
#include <time.h>
#include <unistd.h>

#include "../src/desktop.h"
#include "cp437.h"

static struct termios saved_termios;
static int quit_requested;

static void restore_terminal(void)
{
    tcsetattr(STDIN_FILENO, TCSAFLUSH, &saved_termios);
    fputs("\033[0m\033[?25h\033[?1049l", stdout); /* reset colour, show cursor, main screen */
    fflush(stdout);
}

static void on_signal(int sig)
{
    (void)sig;
    restore_terminal();
    _exit(1);
}

static void setup_terminal(void)
{
    tcgetattr(STDIN_FILENO, &saved_termios);
    atexit(restore_terminal);
    signal(SIGINT, on_signal);
    signal(SIGTERM, on_signal);
    struct termios raw = saved_termios;
    raw.c_iflag &= ~(unsigned)(BRKINT | ICRNL | INPCK | ISTRIP | IXON);
    raw.c_oflag &= ~(unsigned)OPOST;
    raw.c_lflag &= ~(unsigned)(ECHO | ICANON | IEXTEN | ISIG);
    raw.c_cflag |= CS8;
    raw.c_cc[VMIN] = 0;
    raw.c_cc[VTIME] = 0;
    tcsetattr(STDIN_FILENO, TCSAFLUSH, &raw);
    fputs("\033[?1049h\033[?25l\033[2J", stdout); /* alternate screen, hide cursor */
}

/* ---- platform implementation ---------------------------------------------- */

void plat_present(const uint16_t *cells)
{
    static char buf[SCR_W * SCR_H * 24];
    FILE *f = fmemopen(buf, sizeof buf, "w");
    if (!f)
        return;
    fputs("\033[H", f);
    int last_attr = -1;
    for (int y = 0; y < SCR_H; y++) {
        fprintf(f, "\033[%d;1H", y + 1);
        for (int x = 0; x < SCR_W; x++) {
            uint16_t c = cells[y * SCR_W + x];
            int attr = c >> 8;
            if (attr != last_attr) {
                ansi_attr(f, (uint8_t)attr);
                last_attr = attr;
            }
            cp437_put(f, (uint8_t)c);
        }
    }
    fputs("\033[0m", f);
    long n = ftell(f);
    fclose(f);
    fwrite(buf, 1, (size_t)n, stdout);
    fflush(stdout);
}

void plat_time(struct datetime *t)
{
    time_t now = time(NULL);
    struct tm tm;
    localtime_r(&now, &tm);
    *t = (struct datetime){ tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday,
                            tm.tm_hour, tm.tm_min, tm.tm_sec };
}

uint32_t plat_mem_kib(void) { return 0; }

void plat_reboot(void)
{
    ws_init(); /* "reboot" = start over at the login screen */
}

void plat_poweroff(void) { quit_requested = 1; }

/* ---- keyboard ------------------------------------------------------------- */

static int read_byte(int timeout_ms)
{
    fd_set fds;
    FD_ZERO(&fds);
    FD_SET(STDIN_FILENO, &fds);
    struct timeval tv = { timeout_ms / 1000, (timeout_ms % 1000) * 1000 };
    if (select(STDIN_FILENO + 1, &fds, NULL, NULL, &tv) <= 0)
        return -1;
    unsigned char c;
    return read(STDIN_FILENO, &c, 1) == 1 ? c : -1;
}

/* xterm encodes modifiers as 1 + (shift ? 1 : 0) + (alt ? 2 : 0) + (ctrl ? 4 : 0). */
static int xterm_mods(int m)
{
    m -= 1;
    return (m & 1 ? MOD_SHIFT : 0) | (m & 2 ? MOD_ALT : 0) | (m & 4 ? MOD_CTRL : 0);
}

static int tilde_key(int n)
{
    switch (n) {
    case 1: case 7: return K_HOME;
    case 2: return K_INSERT;
    case 3: return K_DELETE;
    case 4: case 8: return K_END;
    case 5: return K_PGUP;
    case 6: return K_PGDN;
    case 11: return K_F1;
    case 12: return K_F2;
    case 13: return K_F3;
    case 14: return K_F4;
    case 15: return K_F5;
    case 17: return K_F6;
    case 18: return K_F7;
    case 19: return K_F8;
    case 20: return K_F9;
    case 21: return K_F10;
    case 23: return K_F11;
    case 24: return K_F12;
    }
    return 0;
}

static int final_key(int c)
{
    switch (c) {
    case 'A': return K_UP;
    case 'B': return K_DOWN;
    case 'C': return K_RIGHT;
    case 'D': return K_LEFT;
    case 'H': return K_HOME;
    case 'F': return K_END;
    case 'P': return K_F1;
    case 'Q': return K_F2;
    case 'R': return K_F3;
    case 'S': return K_F4;
    }
    return 0;
}

/* Parse what follows ESC. Returns 1 if *k was filled. */
static int parse_escape(struct key *k)
{
    int c = read_byte(30);
    if (c < 0) {
        *k = (struct key){ K_ESC, 0 };
        return 1;
    }
    if (c == 'O') { /* SS3: F1-F4, sometimes arrows */
        int f = read_byte(30);
        k->code = final_key(f);
        k->mods = 0;
        return k->code != 0;
    }
    if (c != '[') { /* ESC + key = Alt + key */
        if (c == 27) {
            *k = (struct key){ K_ESC, MOD_ALT };
            return 1;
        }
        *k = (struct key){ c == '\t' ? K_TAB : c == 127 ? K_BACKSPACE : c, MOD_ALT };
        if (c == '\r')
            k->code = K_ENTER;
        return 1;
    }
    int params[4] = { 0 }, np = 0, f;
    while ((f = read_byte(30)) >= 0) {
        if (f >= '0' && f <= '9') {
            params[np] = params[np] * 10 + (f - '0');
        } else if (f == ';') {
            if (np < 3)
                np++;
        } else {
            break;
        }
    }
    if (f == 'Z') {
        *k = (struct key){ K_TAB, MOD_SHIFT };
        return 1;
    }
    int mods = np >= 1 ? xterm_mods(params[1]) : 0;
    k->code = f == '~' ? tilde_key(params[0]) : final_key(f);
    k->mods = mods;
    return k->code != 0;
}

static int prefix_key(int c, struct key *k)
{
    switch (c) {
    case 'k': *k = (struct key){ K_F1, MOD_ALT }; return 1;
    case 'r': *k = (struct key){ K_F2, MOD_ALT }; return 1;
    case 'w': *k = (struct key){ K_F12, MOD_ALT }; return 1;
    case 't': *k = (struct key){ K_TAB, MOD_ALT }; return 1;
    case 'x': *k = (struct key){ K_F4, MOD_ALT }; return 1;
    case 'm': *k = (struct key){ K_F7, MOD_ALT }; return 1;
    case 'd': *k = (struct key){ K_F12, MOD_CTRL }; return 1;
    case 's': *k = (struct key){ K_ESC, MOD_CTRL }; return 1;
    case 'q': quit_requested = 1; return 0;
    case 27: {
        struct key arrow;
        if (parse_escape(&arrow) && arrow.code >= K_UP && arrow.code <= K_RIGHT) {
            *k = (struct key){ arrow.code, MOD_ALT };
            return 1;
        }
        return 0;
    }
    }
    if (c >= '1' && c <= '4') {
        *k = (struct key){ K_F1 + (c - '1'), MOD_CTRL };
        return 1;
    }
    return 0;
}

int plat_key_poll(struct key *k)
{
    int c = read_byte(0);
    if (c < 0)
        return 0;
    if (c == 27)
        return parse_escape(k);
    if (c == 1) /* Ctrl+A prefix */
        return prefix_key(read_byte(2000), k);
    k->mods = 0;
    if (c == '\r' || c == '\n')
        k->code = K_ENTER;
    else if (c == 127 || c == 8)
        k->code = K_BACKSPACE;
    else if (c == '\t')
        k->code = K_TAB;
    else if (c >= 1 && c <= 26) { /* Ctrl+letter */
        k->code = 'a' + c - 1;
        k->mods = MOD_CTRL;
    } else
        k->code = c;
    return 1;
}

int main(void)
{
    if (!isatty(STDIN_FILENO)) {
        fprintf(stderr, "plasmix-tty must be run in an interactive terminal.\n");
        return 1;
    }
    setup_terminal();
    ws_init();
    while (!quit_requested) {
        ws_tick();
        /* Sleep until a key arrives or 100 ms pass (for the clock). */
        fd_set fds;
        FD_ZERO(&fds);
        FD_SET(STDIN_FILENO, &fds);
        struct timeval tv = { 0, 100000 };
        select(STDIN_FILENO + 1, &fds, NULL, NULL, &tv);
    }
    if (g_ws.phase == PHASE_OFF)
        usleep(800000); /* let the "shut down" screen be seen */
    return 0;
}
