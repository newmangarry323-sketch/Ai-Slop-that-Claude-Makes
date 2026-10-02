/* term.c - Skarlet Terminal on Linux: a real terminal running bash.
 *
 * A terminal is two things.  A "pseudo-terminal" (pty) pair: the shell
 * reads and writes one end as if it were a keyboard and screen, and we hold
 * the other end.  And an emulator that understands the escape codes programs
 * send to move the cursor, change colours, clear the screen and so on; for
 * that we use libvterm (linux/libvterm, MIT licence), the library behind the
 * terminals in Neovim and Vim.  We draw its screen of character cells with
 * SkarletOS's monospaced font.
 *
 * This replaces the kernel's built-in terminal (src/apps.c), whose shell is
 * a toy.  Here "sudo apt install ..." really installs software.
 */
#define _GNU_SOURCE
#include <errno.h>
#include <fcntl.h>
#include <pty.h>
#include <pwd.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <unistd.h>

#include "../src/desktop.h"
#include "../src/gfx.h"
#include "../src/lib.h"
#include "libvterm/include/vterm.h"
#include "linux.h"

#define PAD 10
#define LH 18          /* line height in pixels */
#define SB_LINES 2000  /* scrollback */

struct sb_cell {
    uint32_t ch;
    uint32_t fg, bg;
    uint8_t bold, underline, reverse, def_bg;
};

struct term {
    int used;
    int pid;            /* SkarletOS window */
    VTerm *vt;
    VTermScreen *screen;
    int fd;             /* our end of the pty */
    pid_t child;
    int rows, cols;
    int cursor_visible, altscreen;
    int scroll;         /* lines scrolled back */
    struct sb_cell *sb[SB_LINES];
    int sb_cols[SB_LINES];
    int sb_head, sb_count;
    char title[96];
};

static struct term terms[MAX_WIN];

static int cw(void) { return gfx_glyph_of(&font_mono, 'M')->adv; }

static struct term *term_of(const struct window *w)
{
    for (int i = 0; i < MAX_WIN; i++)
        if (terms[i].used && terms[i].pid == w->pid)
            return &terms[i];
    return 0;
}

/* ---- libvterm callbacks ---------------------------------------------------- */

static void to_pty(const char *s, size_t len, void *user)
{
    struct term *t = user;
    while (len > 0 && t->fd >= 0) {
        ssize_t n = write(t->fd, s, len);
        if (n < 0 && errno == EINTR)
            continue;
        if (n <= 0)
            break;
        s += n;
        len -= (size_t)n;
    }
}

static int on_damage(VTermRect r, void *user)
{
    (void)r, (void)user;
    g_ws.need_redraw = 1;
    return 1;
}

static int on_movecursor(VTermPos pos, VTermPos old, int visible, void *user)
{
    (void)pos, (void)old, (void)visible, (void)user;
    g_ws.need_redraw = 1;
    return 1;
}

static int on_prop(VTermProp prop, VTermValue *val, void *user)
{
    struct term *t = user;
    if (prop == VTERM_PROP_CURSORVISIBLE)
        t->cursor_visible = val->boolean;
    else if (prop == VTERM_PROP_ALTSCREEN)
        t->altscreen = val->boolean;
    else if (prop == VTERM_PROP_TITLE) {
        /* The title may arrive in fragments; we keep the first piece. */
        if (val->string.initial)
            t->title[0] = 0;
        int have = (int)strlen(t->title), n = (int)MIN(val->string.len, sizeof t->title - 1 - have);
        memcpy(t->title + have, val->string.str, (size_t)MAX(n, 0));
        t->title[have + MAX(n, 0)] = 0;
        struct window *w = wm_by_pid(t->pid);
        if (w && val->string.final)
            k_snprintf(w->title, sizeof w->title, "%s", t->title);
    }
    g_ws.need_redraw = 1;
    return 1;
}

static uint32_t rgb_of(struct term *t, VTermColor c)
{
    vterm_screen_convert_color_to_rgb(t->screen, &c);
    return RGB(c.rgb.red, c.rgb.green, c.rgb.blue);
}

static void cell_colours(struct term *t, const VTermScreenCell *c, uint32_t *fg, uint32_t *bg,
                         int *def_bg)
{
    *def_bg = VTERM_COLOR_IS_DEFAULT_BG(&c->bg);
    *fg = VTERM_COLOR_IS_DEFAULT_FG(&c->fg) ? g_theme->term_fg : rgb_of(t, c->fg);
    *bg = *def_bg ? g_theme->term_bg : rgb_of(t, c->bg);
}

/* A line scrolled off the top: keep it for scrolling back. */
static int on_pushline(int cols, const VTermScreenCell *cells, void *user)
{
    struct term *t = user;
    int slot = t->sb_head;
    free(t->sb[slot]);
    t->sb[slot] = malloc(sizeof(struct sb_cell) * (size_t)cols);
    if (!t->sb[slot])
        return 0;
    for (int i = 0; i < cols; i++) {
        struct sb_cell *s = &t->sb[slot][i];
        int def_bg;
        cell_colours(t, &cells[i], &s->fg, &s->bg, &def_bg);
        s->ch = cells[i].chars[0];
        s->bold = cells[i].attrs.bold;
        s->underline = cells[i].attrs.underline != 0;
        s->reverse = cells[i].attrs.reverse;
        s->def_bg = (uint8_t)def_bg;
    }
    t->sb_cols[slot] = cols;
    t->sb_head = (t->sb_head + 1) % SB_LINES;
    if (t->sb_count < SB_LINES)
        t->sb_count++;
    return 1;
}

static const VTermScreenCallbacks callbacks = {
    .damage = on_damage,
    .movecursor = on_movecursor,
    .settermprop = on_prop,
    .sb_pushline = on_pushline,
};

/* ---- the window -------------------------------------------------------------- */

static void size_for(int w, int h, int *rows, int *cols)
{
    *cols = MAX(10, (w - 2 * PAD) / cw());
    *rows = MAX(3, (h - 2 * PAD) / LH);
}

static void start_shell(struct term *t, const char *cmd)
{
    struct winsize ws = { (unsigned short)t->rows, (unsigned short)t->cols, 0, 0 };
    pid_t pid = forkpty(&t->fd, 0, 0, &ws);
    if (pid < 0) {
        t->fd = -1;
        return;
    }
    if (pid == 0) {
        setenv("TERM", "xterm-256color", 1);
        setenv("COLORTERM", "truecolor", 1);
        const char *home = getenv("HOME");
        if (home && chdir(home) != 0) {
            /* stay where we are */
        }
        signal(SIGCHLD, SIG_DFL);
        signal(SIGPIPE, SIG_DFL);
        const char *shell = access("/bin/bash", X_OK) == 0 ? "/bin/bash" : "/bin/sh";
        if (cmd && *cmd) {
            /* Run the command, then stay open with a shell, as KDE's
             * Konsole does for "run in terminal". */
            char script[1024];
            snprintf(script, sizeof script, "%s; exec %s -l", cmd, shell);
            execl(shell, shell, "-c", script, (char *)0);
        } else {
            execl(shell, shell, "-l", (char *)0);
        }
        _exit(127);
    }
    t->child = pid;
    fcntl(t->fd, F_SETFL, fcntl(t->fd, F_GETFL) | O_NONBLOCK);
}

static void lt_init(struct window *w, const char *arg)
{
    struct term *t = 0;
    for (int i = 0; i < MAX_WIN; i++)
        if (!terms[i].used) {
            t = &terms[i];
            break;
        }
    k_strlcpy(w->title, "Skarlet Terminal", sizeof w->title);
    if (!t)
        return;
    memset(t, 0, sizeof *t);
    t->used = 1;
    t->pid = w->pid;
    t->fd = -1;
    t->cursor_visible = 1;
    size_for(w->w - 2, w->h - TITLE_H - 2, &t->rows, &t->cols);
    t->vt = vterm_new(t->rows, t->cols);
    vterm_set_utf8(t->vt, 1);
    vterm_output_set_callback(t->vt, to_pty, t);
    t->screen = vterm_obtain_screen(t->vt);
    vterm_screen_set_callbacks(t->screen, &callbacks, t);
    vterm_screen_enable_altscreen(t->screen, 1);
    vterm_screen_reset(t->screen, 1);
    /* The 16 basic colours, chosen to read well on the dark background
     * (the standard xterm blue, for one, is too dark there). */
    static const uint32_t palette[16] = {
        0x232627, 0xed1515, 0x11d116, 0xf67400, 0x1d99f3, 0x9b59b6, 0x1abc9c, 0xfcfcfc,
        0x7f8c8d, 0xc0392b, 0x1cdc9a, 0xfdbc4b, 0x3daee9, 0x8e44ad, 0x16a085, 0xffffff,
    };
    VTermState *state = vterm_obtain_state(t->vt);
    for (int i = 0; i < 16; i++) {
        VTermColor col;
        vterm_color_rgb(&col, (uint8_t)(palette[i] >> 16), (uint8_t)(palette[i] >> 8),
                        (uint8_t)palette[i]);
        vterm_state_set_palette_color(state, i, &col);
    }
    start_shell(t, arg);
    if (t->fd < 0)
        svc_notify("Skarlet Terminal", "Could not start a shell.");
}

static void lt_close(struct window *w)
{
    struct term *t = term_of(w);
    if (!t)
        return;
    if (t->fd >= 0)
        close(t->fd); /* the shell gets SIGHUP and ends */
    if (t->child > 0)
        kill(t->child, SIGHUP);
    vterm_free(t->vt);
    for (int i = 0; i < SB_LINES; i++)
        free(t->sb[i]);
    t->used = 0;
}

/* Box drawing and blocks are drawn as shapes, so that lines join up from
 * one cell to the next whatever the font does. */
static int draw_special(unsigned ch, int x, int y, int w, int h, uint32_t c)
{
    int mx = x + w / 2, my = y + h / 2;
    if (ch >= 0x2580 && ch <= 0x259f) {
        switch (ch) {
        case 0x2580: gfx_rect(x, y, w, h / 2, c, 255); return 1;      /* upper half */
        case 0x2584: gfx_rect(x, my, w, h - h / 2, c, 255); return 1; /* lower half */
        case 0x2588: gfx_rect(x, y, w, h, c, 255); return 1;          /* full */
        case 0x258c: gfx_rect(x, y, w / 2, h, c, 255); return 1;      /* left half */
        case 0x2590: gfx_rect(mx, y, w - w / 2, h, c, 255); return 1; /* right half */
        case 0x2591: gfx_rect(x, y, w, h, c, 64); return 1;           /* shades */
        case 0x2592: gfx_rect(x, y, w, h, c, 128); return 1;
        case 0x2593: gfx_rect(x, y, w, h, c, 192); return 1;
        }
        return 0;
    }
    if (ch < 0x2500 || ch > 0x257f)
        return 0;
    /* Which arms of the cross the character has: left, right, up, down. */
    static const struct { unsigned short ch; unsigned char arms; } box[] = {
        { 0x2500, 3 }, { 0x2501, 3 }, { 0x2502, 12 }, { 0x2503, 12 }, { 0x250c, 10 }, { 0x250f, 10 },
        { 0x2510, 9 }, { 0x2513, 9 }, { 0x2514, 6 }, { 0x2517, 6 }, { 0x2518, 5 }, { 0x251b, 5 },
        { 0x251c, 14 }, { 0x2523, 14 }, { 0x2524, 13 }, { 0x252b, 13 }, { 0x252c, 11 },
        { 0x2533, 11 }, { 0x2534, 7 }, { 0x253b, 7 }, { 0x253c, 15 }, { 0x254b, 15 },
        { 0x2550, 3 }, { 0x2551, 12 }, { 0x2554, 10 }, { 0x2557, 9 }, { 0x255a, 6 }, { 0x255d, 5 },
        { 0x2560, 14 }, { 0x2563, 13 }, { 0x2566, 11 }, { 0x2569, 7 }, { 0x256c, 15 },
        { 0x256d, 10 }, { 0x256e, 9 }, { 0x256f, 5 }, { 0x2570, 6 }, { 0x2574, 1 }, { 0x2575, 4 },
        { 0x2576, 2 }, { 0x2577, 8 },
    };
    for (int i = 0; i < ARRAY_LEN(box); i++) {
        if (box[i].ch != ch)
            continue;
        int a = box[i].arms;
        if (a & 1) gfx_rect(x, my, mx - x + 1, 1, c, 255);
        if (a & 2) gfx_rect(mx, my, x + w - mx, 1, c, 255);
        if (a & 4) gfx_rect(mx, y, 1, my - y + 1, c, 255);
        if (a & 8) gfx_rect(mx, my, 1, y + h - my, c, 255);
        return 1;
    }
    return 0;
}

static void draw_cell(int x, int y, unsigned ch, uint32_t fg, uint32_t bg, int def_bg, int bold,
                      int underline, int reverse, int W)
{
    if (reverse) {
        uint32_t tmp = fg;
        fg = bg, bg = tmp;
        def_bg = 0;
    }
    if (!def_bg)
        gfx_rect(x, y, W, LH, bg, 255);
    if (ch > ' ' && !draw_special(ch, x, y, W, LH, fg)) {
        gfx_glyph(&font_mono, x, y, ch, fg, 255);
        if (bold) /* no bold font: draw it twice, a pixel apart */
            gfx_glyph(&font_mono, x + 1, y, ch, fg, 255);
    }
    if (underline)
        gfx_rect(x, y + LH - 3, W, 1, fg, 255);
}

static void lt_draw(struct window *w, int x, int y, int wcw, int wch, int focused)
{
    const struct theme *th = g_theme;
    gfx_rect(x, y, wcw, wch, th->term_bg, 255);
    struct term *t = term_of(w);
    if (!t)
        return;
    int rows, cols;
    size_for(wcw, wch, &rows, &cols);
    if (rows != t->rows || cols != t->cols) {
        /* The window was resized: tell libvterm and the shell. */
        t->rows = rows, t->cols = cols;
        vterm_set_size(t->vt, rows, cols);
        struct winsize ws = { (unsigned short)rows, (unsigned short)cols, 0, 0 };
        if (t->fd >= 0)
            ioctl(t->fd, TIOCSWINSZ, &ws);
    }
    int W = cw(), x0 = x + PAD, y0 = y + PAD;
    int back = MIN(t->scroll, t->sb_count);
    /* Scrolled back: the newest "back" scrollback lines, then the screen. */
    for (int r = 0; r < back && r < rows; r++) {
        int slot = (t->sb_head - back + r + SB_LINES) % SB_LINES;
        for (int c = 0; c < t->sb_cols[slot] && c < cols; c++) {
            struct sb_cell *s = &t->sb[slot][c];
            draw_cell(x0 + c * W, y0 + r * LH, s->ch, s->fg, s->bg, s->def_bg, s->bold,
                      s->underline, s->reverse, W);
        }
    }
    for (int r = back; r < rows; r++) {
        int row = r - back;
        for (int c = 0; c < cols; c++) {
            VTermScreenCell cell;
            if (!vterm_screen_get_cell(t->screen, (VTermPos){ row, c }, &cell))
                continue;
            uint32_t fg, bg;
            int def_bg;
            cell_colours(t, &cell, &fg, &bg, &def_bg);
            draw_cell(x0 + c * W, y0 + r * LH, cell.chars[0], fg, bg, def_bg, cell.attrs.bold,
                      cell.attrs.underline != 0, cell.attrs.reverse, W);
            if (cell.width > 1)
                c += cell.width - 1;
        }
    }
    if (t->cursor_visible && back == 0) {
        VTermPos pos;
        vterm_state_get_cursorpos(vterm_obtain_state(t->vt), &pos);
        int cx = x0 + pos.col * W, cy = y0 + pos.row * LH;
        if (focused)
            gfx_rect(cx, cy, W, LH, th->term_fg, 140);
        else
            gfx_rrect_line(cx, cy, W, LH, 0, th->term_fg, 200);
    }
    if (back) {
        char label[32];
        k_snprintf(label, sizeof label, "scrollback  -%d", back);
        int pw = gfx_text_width(&font_small, label) + 20;
        gfx_rrect(x + wcw - pw - 12, y + 8, pw, 22, 11, th->accent, 255);
        gfx_text(&font_small, x + wcw - pw - 2, y + 12, label, WHITE, 255);
    }
}

static void lt_key(struct window *w, struct key k)
{
    struct term *t = term_of(w);
    if (!t || t->fd < 0)
        return;
    if ((k.mods & MOD_SHIFT) && (k.code == K_PGUP || k.code == K_PGDN)) {
        int page = MAX(t->rows - 1, 1);
        t->scroll = MAX(0, MIN(t->scroll + (k.code == K_PGUP ? page : -page), t->sb_count));
        return;
    }
    t->scroll = 0;
    VTermModifier mod = (k.mods & MOD_SHIFT ? VTERM_MOD_SHIFT : 0) |
                        (k.mods & MOD_CTRL ? VTERM_MOD_CTRL : 0) |
                        (k.mods & MOD_ALT ? VTERM_MOD_ALT : 0);
    VTermKey key = VTERM_KEY_NONE;
    switch (k.code) {
    case K_ENTER: key = VTERM_KEY_ENTER; break;
    case K_TAB: key = VTERM_KEY_TAB; break;
    case K_BACKSPACE: key = VTERM_KEY_BACKSPACE; break;
    case K_ESC: key = VTERM_KEY_ESCAPE; break;
    case K_UP: key = VTERM_KEY_UP; break;
    case K_DOWN: key = VTERM_KEY_DOWN; break;
    case K_LEFT: key = VTERM_KEY_LEFT; break;
    case K_RIGHT: key = VTERM_KEY_RIGHT; break;
    case K_HOME: key = VTERM_KEY_HOME; break;
    case K_END: key = VTERM_KEY_END; break;
    case K_PGUP: key = VTERM_KEY_PAGEUP; break;
    case K_PGDN: key = VTERM_KEY_PAGEDOWN; break;
    case K_INSERT: key = VTERM_KEY_INS; break;
    case K_DELETE: key = VTERM_KEY_DEL; break;
    default:
        if (k.code >= K_F1 && k.code <= K_F12)
            key = VTERM_KEY_FUNCTION(k.code - K_F1 + 1);
    }
    if (key != VTERM_KEY_NONE) {
        vterm_keyboard_key(t->vt, key, mod);
    } else if (k.code >= 32 && k.code < 0x110000 && k.code != K_META) {
        /* Shift is already part of the character ("A", "!"). */
        vterm_keyboard_unichar(t->vt, (uint32_t)k.code, mod & ~VTERM_MOD_SHIFT);
    }
}

/* The wheel scrolls back through the output; in full-screen programs (less,
 * nano, htop) it sends arrow keys instead, as most terminals do. */
static void lt_mouse(struct window *w, struct mouse m, int wcw, int wch)
{
    (void)wcw, (void)wch;
    struct term *t = term_of(w);
    if (!t || m.type != MOUSE_WHEEL)
        return;
    int up = m.button == 4;
    if (t->altscreen) {
        for (int i = 0; i < 3; i++)
            vterm_keyboard_key(t->vt, up ? VTERM_KEY_UP : VTERM_KEY_DOWN, VTERM_MOD_NONE);
        return;
    }
    t->scroll = MAX(0, MIN(t->scroll + (up ? 3 : -3), t->sb_count));
}

const struct app linux_terminal_app = {
    .w = 860, .h = 540, .init = lt_init, .draw = lt_draw, .key = lt_key, .mouse = lt_mouse,
    .close = lt_close,
};

/* ---- the main loop's part: read what the programs print ------------------------- */

int term_add_fds(fd_set *fds, int maxfd)
{
    for (int i = 0; i < MAX_WIN; i++)
        if (terms[i].used && terms[i].fd >= 0) {
            FD_SET(terms[i].fd, fds);
            maxfd = MAX(maxfd, terms[i].fd);
        }
    return maxfd;
}

void term_read_fds(fd_set *fds)
{
    char buf[65536];
    for (int i = 0; i < MAX_WIN; i++) {
        struct term *t = &terms[i];
        if (!t->used || t->fd < 0 || !FD_ISSET(t->fd, fds))
            continue;
        ssize_t n = read(t->fd, buf, sizeof buf);
        if (n > 0) {
            vterm_input_write(t->vt, buf, (size_t)n);
            g_ws.need_redraw = 1;
        } else if (n == 0 || (errno != EAGAIN && errno != EINTR)) {
            /* The shell has exited: close the window, as Konsole does. */
            struct window *w = wm_by_pid(t->pid);
            if (w)
                wm_remove(w); /* calls lt_close */
            else
                lt_close(&(struct window){ .pid = t->pid });
        }
    }
}
