/* apps.c - the applications: Skarlet Terminal, Skarlet Files, Skarlet Write,
 * Skarlet Settings and Skarlet Monitor.  Each app is three functions (init,
 * draw, key) plus some state stored inside its window.  They are modelled on
 * the KDE 4 applications Konsole, Dolphin, KWrite, System Settings and System
 * Activity, as tiny re-imaginings, not ports. */
#include "desktop.h"
#include "gfx.h"
#include "lib.h"

const struct app_info g_apps[APP_COUNT] = {
    [APP_TERMINAL] = { "skterm", "Skarlet Terminal", "Terminal", "System", '>' },
    [APP_FILES] = { "skfiles", "Skarlet Files", "File Manager", "System", CH_SQUARE },
    [APP_WRITE] = { "skwrite", "Skarlet Write", "Text Editor", "Utilities", CH_MENU },
    [APP_SETTINGS] = { "sksettings", "Skarlet Settings", "Configure the desktop",
                       "Settings", CH_SUN },
    [APP_MONITOR] = { "skmonitor", "Skarlet Monitor", "Process monitor", "System", CH_UTRI },
};

int svc_app_by_name(const char *name)
{
    for (int i = 0; i < APP_COUNT; i++)
        if (k_strcmp(g_apps[i].id, name) == 0)
            return i;
    return -1;
}

/* ======================================================================== */
/* Skarlet Terminal                                                                    */
/* ======================================================================== */

static void term_newline(struct term_state *k)
{
    if (k->nlines == TERM_LINES) {
        k_memmove(k->lines[0], k->lines[1], sizeof k->lines[0] * (TERM_LINES - 1));
        k->nlines--;
    }
    k->lines[k->nlines][0] = 0;
    k->nlines++;
    k->col = 0;
}

/* The shell writes its output through this function. */
static void term_write(void *ctx, const char *s)
{
    struct term_state *k = ctx;
    for (; *s; s++) {
        if (*s == '\n') {
            term_newline(k);
            continue;
        }
        if (k->col >= k->cols)
            term_newline(k);
        char *line = k->lines[k->nlines - 1];
        line[k->col++] = *s == '\t' ? ' ' : *s;
        line[k->col] = 0;
    }
}

static void term_run(struct window *w, const char *cmd)
{
    struct term_state *k = &w->s.term;
    char prompt[VFS_PATH_MAX + 32];
    shell_prompt(&k->sh, prompt, sizeof prompt);
    term_write(k, prompt);
    term_write(k, cmd);
    term_write(k, "\n");
    k->sh.self_pid = w->pid;
    shell_exec(&k->sh, cmd);
    if (k->sh.want_clear) {
        k->sh.want_clear = 0;
        k->nlines = 1;
        k->lines[0][0] = 0;
        k->col = 0;
    }
    if (k->sh.want_exit) {
        wm_close(w);
        return;
    }
    /* As in KDE's Konsole, the title shows the current directory. */
    k_snprintf(w->title, sizeof w->title, "%s - Skarlet Terminal", vfs_name(k->sh.cwd));
}

static void term_init(struct window *w, const char *arg)
{
    struct term_state *k = &w->s.term;
    k->cols = MIN(w->w - 2, TERM_COLS);
    k->nlines = 1;
    shell_init(&k->sh, term_write, k);
    static char motd[512];
    int n = vfs_lookup(VFS_ROOT, "/etc/motd");
    int len = n < 0 ? 0 : vfs_read(n, motd, sizeof motd - 1);
    motd[len < 0 ? 0 : len] = 0;
    term_write(k, motd);
    k_strlcpy(w->title, "user - Skarlet Terminal", sizeof w->title);
    if (arg)
        k_strlcpy(k->pending, arg, sizeof k->pending);
}

static void term_idle(struct window *w)
{
    struct term_state *k = &w->s.term;
    if (k->pending[0]) {
        char cmd[SH_LINE];
        k_strlcpy(cmd, k->pending, sizeof cmd);
        k->pending[0] = 0;
        term_run(w, cmd);
        g_ws.need_redraw = 1;
    }
}

static void term_draw(struct window *w, int x, int y, int cw, int ch, int focused)
{
    struct term_state *k = &w->s.term;
    const struct theme *t = g_theme;
    gfx_fill(x, y, cw, ch, ' ', t->term);

    char prompt[VFS_PATH_MAX + 32];
    shell_prompt(&k->sh, prompt, sizeof prompt);

    /* Row list = output lines (dropping an empty last line) + prompt line. */
    int out_rows = k->nlines;
    if (out_rows > 0 && k->col == 0 && k->lines[k->nlines - 1][0] == 0)
        out_rows--;
    int total = out_rows + 1;
    int first = total - ch - k->scroll;
    if (first < 0)
        first = 0;

    for (int row = 0; row < ch && first + row < total; row++) {
        int idx = first + row;
        if (idx < out_rows) {
            gfx_textw(x, y + row, k->lines[idx], cw, t->term);
            continue;
        }
        /* The prompt: green "user@skarlet:dir$", then the typed text.
         * If it is too long for the window, show the end of it. */
        int plen = k_strlen(prompt);
        int over = plen + k->len + 1 - cw;
        int px = x;
        for (int i = MAX(over, 0); i < plen; i++)
            gfx_put(px++, y + row, (unsigned char)prompt[i], t->term_bold);
        for (int i = MAX(over - plen, 0); i < k->len; i++)
            gfx_put(px++, y + row, (unsigned char)k->input[i], t->term);
        if (focused && k->scroll == 0)
            gfx_put(px, y + row, CH_FULL, t->term);
    }
    if (k->scroll)
        gfx_text(x + cw - 12, y, "[scrollback]", ATTR(BLACK, YELLOW));
}

static void term_key(struct window *w, struct key key)
{
    struct term_state *k = &w->s.term;
    int c = key.code;

    if ((key.mods & MOD_SHIFT) && (c == K_PGUP || c == K_PGDN)) {
        int page = w->h - 3;
        k->scroll += c == K_PGUP ? page : -page;
        k->scroll = MAX(0, MIN(k->scroll, k->nlines));
        return;
    }
    k->scroll = 0;

    if ((key.mods & MOD_CTRL) && c == 'l') {
        k->nlines = 1;
        k->lines[0][0] = 0;
        k->col = 0;
        return;
    }
    if ((key.mods & MOD_CTRL) && c == 'c') {
        char prompt[VFS_PATH_MAX + 32];
        shell_prompt(&k->sh, prompt, sizeof prompt);
        term_write(k, prompt);
        term_write(k, k->input);
        term_write(k, "^C\n");
        k->len = 0;
        k->input[0] = 0;
        return;
    }
    if (c == K_ENTER) {
        char cmd[SH_LINE];
        k_strlcpy(cmd, k->input, sizeof cmd);
        k->len = 0;
        k->input[0] = 0;
        k->hist_pos = k->sh.nhist + 1;
        if (cmd[0] == 0) {
            char prompt[VFS_PATH_MAX + 32];
            shell_prompt(&k->sh, prompt, sizeof prompt);
            term_write(k, prompt);
            term_write(k, "\n");
            return;
        }
        term_run(w, cmd);
        k->hist_pos = k->sh.nhist;
        return;
    }
    if (c == K_BACKSPACE) {
        if (k->len > 0)
            k->input[--k->len] = 0;
        return;
    }
    if (c == K_UP || c == K_DOWN) {
        /* Walk through the command history. */
        int n = k->sh.nhist;
        if (k->hist_pos > n)
            k->hist_pos = n;
        k->hist_pos += c == K_UP ? -1 : 1;
        if (k->hist_pos < 0)
            k->hist_pos = 0;
        if (k->hist_pos >= n) {
            k->hist_pos = n;
            k->input[0] = 0;
        } else {
            k_strlcpy(k->input, k->sh.hist[k->hist_pos], sizeof k->input);
        }
        k->len = k_strlen(k->input);
        return;
    }
    if (c >= 32 && c < 127 && !(key.mods & (MOD_CTRL | MOD_ALT)) && k->len < SH_LINE - 1) {
        k->input[k->len++] = (char)c;
        k->input[k->len] = 0;
    }
}

static const struct app terminal_app = { 64, 16, term_init, term_draw, term_key, term_idle };

/* ======================================================================== */
/* Skarlet Files                                                                    */
/* ======================================================================== */

static const char *const places[][2] = {
    { "Home", "/home/user" },
    { "Desktop", "/home/user/Desktop" },
    { "Documents", "/home/user/Documents" },
    { "Music", "/home/user/Music" },
    { "Root", "/" },
    { "Temp", "/tmp" },
};

static void files_set_dir(struct window *w, int dir)
{
    struct files_state *d = &w->s.files;
    d->dir = dir;
    d->sel = 0;
    d->top = 0;
    k_snprintf(w->title, sizeof w->title, "%s - Skarlet Files", dir == VFS_ROOT ? "/" : vfs_name(dir));
}

static void files_init(struct window *w, const char *arg)
{
    int dir = vfs_lookup(VFS_ROOT, arg ? arg : "/home/user");
    if (dir < 0 || !vfs_is_dir(dir))
        dir = vfs_lookup(VFS_ROOT, "/home/user");
    w->s.files.pane = 1;
    files_set_dir(w, dir);
}

static void files_draw(struct window *w, int x, int y, int cw, int ch, int focused)
{
    struct files_state *d = &w->s.files;
    const struct theme *t = g_theme;
    int places_w = 13;

    /* Toolbar with the "breadcrumb" location bar. */
    char path[VFS_PATH_MAX];
    vfs_path(d->dir, path, sizeof path);
    gfx_fill(x, y, cw, 1, ' ', t->win);
    gfx_put(x + 1, y, CH_LTRI, t->win_dim);
    gfx_put(x + 3, y, CH_UTRI, t->win);
    gfx_textw(x + 5, y, path, cw - 6, t->input);

    /* Places panel. */
    gfx_text(x + 1, y + 1, "Places", t->menu_head);
    for (int i = 0; i < ARRAY_LEN(places) && i + 2 < ch - 1; i++) {
        uint8_t a = (d->pane == 0 && d->place == i && focused) ? t->sel : t->win;
        gfx_put(x, y + 2 + i, ' ', a);
        gfx_put(x + 1, y + 2 + i, i == 4 ? CH_SQUARE : CH_HOUSE, a);
        gfx_textw(x + 2, y + 2 + i, places[i][0], places_w - 3, a);
    }
    gfx_fill(x + places_w, y + 1, 1, ch - 2, CH_V, t->win_border_off);

    /* File list. */
    int kids[VFS_MAX_NODES];
    int n = vfs_list(d->dir, kids, VFS_MAX_NODES);
    int lx = x + places_w + 1, lw = cw - places_w - 1, rows = ch - 2;
    if (d->sel >= n)
        d->sel = MAX(n - 1, 0);
    if (d->sel < d->top)
        d->top = d->sel;
    if (d->sel >= d->top + rows)
        d->top = d->sel - rows + 1;
    if (n == 0)
        gfx_text(lx + 2, y + 2, "Folder is empty", t->win_dim);
    for (int r = 0; r < rows && d->top + r < n; r++) {
        int node = kids[d->top + r];
        int is_sel = d->top + r == d->sel;
        uint8_t a = is_sel ? (d->pane == 1 && focused ? t->sel : t->title_off) : t->win;
        gfx_fill(lx, y + 1 + r, lw, 1, ' ', a);
        gfx_put(lx + 1, y + 1 + r, vfs_is_dir(node) ? CH_SQUARE : CH_MENU, a);
        gfx_textw(lx + 3, y + 1 + r, vfs_name(node), lw - 12, a);
        char size[16];
        if (vfs_is_dir(node))
            k_strlcpy(size, "folder", sizeof size);
        else
            k_snprintf(size, sizeof size, "%d B", vfs_size(node));
        gfx_text(lx + lw - 1 - k_strlen(size), y + 1 + r, size, a);
    }

    /* Status bar. */
    int dirs = 0;
    for (int i = 0; i < n; i++)
        dirs += vfs_is_dir(kids[i]);
    char status[64];
    k_snprintf(status, sizeof status, " %d folders, %d files", dirs, n - dirs);
    gfx_textw(x, y + ch - 1, status, cw, t->win_dim);
}

static void files_key(struct window *w, struct key k)
{
    struct files_state *d = &w->s.files;
    int kids[VFS_MAX_NODES];
    int n = vfs_list(d->dir, kids, VFS_MAX_NODES);

    if (k.code == K_TAB || (d->pane == 1 && k.code == K_LEFT) || (d->pane == 0 && k.code == K_RIGHT)) {
        d->pane ^= 1;
        return;
    }
    if (k.code == K_BACKSPACE || (k.mods & MOD_ALT && k.code == K_UP)) {
        if (d->dir != VFS_ROOT) {
            int child = d->dir;
            files_set_dir(w, vfs_parent(d->dir));
            /* Select the folder we came out of. */
            n = vfs_list(d->dir, kids, VFS_MAX_NODES);
            for (int i = 0; i < n; i++)
                if (kids[i] == child)
                    d->sel = i;
        }
        return;
    }
    if (d->pane == 0) {
        if (k.code == K_UP && d->place > 0)
            d->place--;
        else if (k.code == K_DOWN && d->place < ARRAY_LEN(places) - 1)
            d->place++;
        else if (k.code == K_ENTER) {
            int dir = vfs_lookup(VFS_ROOT, places[d->place][1]);
            if (dir >= 0)
                files_set_dir(w, dir);
            d->pane = 1;
        }
        return;
    }
    if (k.code == K_UP && d->sel > 0)
        d->sel--;
    else if (k.code == K_DOWN && d->sel < n - 1)
        d->sel++;
    else if (k.code == K_HOME)
        d->sel = 0;
    else if (k.code == K_END)
        d->sel = MAX(n - 1, 0);
    else if (k.code == K_ENTER && n > 0) {
        int node = kids[d->sel];
        if (vfs_is_dir(node)) {
            files_set_dir(w, node);
        } else {
            char path[VFS_PATH_MAX];
            vfs_path(node, path, sizeof path);
            svc_launch(APP_WRITE, path);
        }
    } else if (k.code == K_DELETE && n > 0) {
        char msg[64];
        int err = vfs_remove(kids[d->sel]);
        if (err < 0) {
            k_snprintf(msg, sizeof msg, "Could not delete: %s", vfs_strerror(err));
            svc_notify("Skarlet Files", msg);
        }
    }
}

static const struct app files_app = { 58, 15, files_init, files_draw, files_key, 0 };

/* ======================================================================== */
/* Skarlet Write                                                                     */
/* ======================================================================== */

static void ed_title(struct window *w)
{
    struct edit_state *e = &w->s.ed;
    const char *name = e->path;
    for (const char *p = e->path; *p; p++)
        if (*p == '/')
            name = p + 1;
    k_snprintf(w->title, sizeof w->title, "%s%s - Skarlet Write", name[0] ? name : "Untitled",
               e->dirty ? " [modified]" : "");
}

static void ed_init(struct window *w, const char *arg)
{
    struct edit_state *e = &w->s.ed;
    if (arg) {
        k_strlcpy(e->path, arg, sizeof e->path);
        int n = vfs_lookup(VFS_ROOT, arg);
        if (n >= 0 && !vfs_is_dir(n))
            e->len = vfs_read(n, e->buf, sizeof e->buf);
    } else {
        k_strlcpy(e->path, "/home/user/Documents/untitled.txt", sizeof e->path);
    }
    ed_title(w);
}

/* Position helpers: find where the line containing pos starts. */
static int ed_line_start(struct edit_state *e, int pos)
{
    while (pos > 0 && e->buf[pos - 1] != '\n')
        pos--;
    return pos;
}

static int ed_line_end(struct edit_state *e, int pos)
{
    while (pos < e->len && e->buf[pos] != '\n')
        pos++;
    return pos;
}

static void ed_draw(struct window *w, int x, int y, int cw, int ch, int focused)
{
    struct edit_state *e = &w->s.ed;
    const struct theme *t = g_theme;
    uint8_t text = ATTR(BLACK, WHITE), gutter = ATTR(DGRAY, LGRAY);
    int rows = ch - 1;

    /* Which line is the cursor on? Scroll so it stays visible. */
    int cur_line = 0, cur_col = 0;
    for (int i = 0; i < e->cur; i++) {
        if (e->buf[i] == '\n') {
            cur_line++;
            cur_col = 0;
        } else {
            cur_col++;
        }
    }
    if (cur_line < e->top)
        e->top = cur_line;
    if (cur_line >= e->top + rows)
        e->top = cur_line - rows + 1;

    gfx_fill(x, y, cw, rows, ' ', text);
    int line = 0, col = 0;
    for (int i = 0; i <= e->len; i++) {
        int r = line - e->top;
        if (col == 0 && r >= 0 && r < rows) {
            char num[8];
            k_snprintf(num, sizeof num, "%3d ", line + 1);
            gfx_text(x, y + r, num, gutter);
        }
        if (r >= 0 && r < rows && i == e->cur && focused)
            gfx_put(x + 4 + col, y + r, i < e->len && e->buf[i] != '\n' ? e->buf[i] : ' ',
                    g_theme->sel);
        else if (r >= 0 && r < rows && i < e->len && e->buf[i] != '\n')
            gfx_put(x + 4 + col, y + r, (unsigned char)e->buf[i], text);
        if (i < e->len && e->buf[i] == '\n') {
            line++;
            col = 0;
        } else {
            col++;
        }
    }
    char status[80];
    k_snprintf(status, sizeof status, " Line: %d  Col: %d   %d bytes   Ctrl+S save", cur_line + 1,
               cur_col + 1, e->len);
    gfx_textw(x, y + ch - 1, status, cw, t->win_dim);
}

static void ed_insert(struct edit_state *e, char c)
{
    if (e->len >= VFS_FILE_MAX)
        return;
    k_memmove(e->buf + e->cur + 1, e->buf + e->cur, e->len - e->cur);
    e->buf[e->cur++] = c;
    e->len++;
    e->dirty = 1;
}

static void ed_key(struct window *w, struct key k)
{
    struct edit_state *e = &w->s.ed;
    int c = k.code;
    if ((k.mods & MOD_CTRL) && c == 's') {
        int n = vfs_lookup(VFS_ROOT, e->path);
        if (n < 0)
            n = vfs_create(VFS_ROOT, e->path, 0);
        int err = n < 0 ? n : vfs_write(n, e->buf, e->len, 0);
        char msg[VFS_PATH_MAX + 40];
        if (err < 0) {
            k_snprintf(msg, sizeof msg, "Could not save: %s", vfs_strerror(err));
        } else {
            e->dirty = 0;
            k_snprintf(msg, sizeof msg, "Saved %s", e->path);
        }
        svc_notify("Skarlet Write", msg);
    } else if (c == K_LEFT && e->cur > 0) {
        e->cur--;
    } else if (c == K_RIGHT && e->cur < e->len) {
        e->cur++;
    } else if (c == K_HOME) {
        e->cur = ed_line_start(e, e->cur);
    } else if (c == K_END) {
        e->cur = ed_line_end(e, e->cur);
    } else if (c == K_UP || c == K_DOWN) {
        int start = ed_line_start(e, e->cur), col = e->cur - start;
        int target;
        if (c == K_UP) {
            if (start == 0)
                return;
            target = ed_line_start(e, start - 1);
        } else {
            int end = ed_line_end(e, e->cur);
            if (end >= e->len)
                return;
            target = end + 1;
        }
        e->cur = MIN(target + col, ed_line_end(e, target));
    } else if (c == K_BACKSPACE && e->cur > 0) {
        k_memmove(e->buf + e->cur - 1, e->buf + e->cur, e->len - e->cur);
        e->cur--;
        e->len--;
        e->dirty = 1;
    } else if (c == K_DELETE && e->cur < e->len) {
        k_memmove(e->buf + e->cur, e->buf + e->cur + 1, e->len - e->cur - 1);
        e->len--;
        e->dirty = 1;
    } else if (c == K_ENTER) {
        ed_insert(e, '\n');
    } else if (c == K_TAB) {
        for (int i = 0; i < 4; i++)
            ed_insert(e, ' ');
    } else if (c >= 32 && c < 127 && !(k.mods & (MOD_CTRL | MOD_ALT))) {
        ed_insert(e, (char)c);
    }
    ed_title(w);
}

static const struct app write_app = { 60, 16, ed_init, ed_draw, ed_key, 0 };

/* ======================================================================== */
/* Skarlet Settings                                                            */
/* ======================================================================== */

static const char *const setting_names[] = {
    "Desktop theme", "Accent colour", "Wallpaper", "Clock format", "Clock seconds", "Widgets",
};
static const char *const wallpapers[] = { "Horizon", "Dots", "Plain" };

static void set_value(int i, char *out, int size)
{
    switch (i) {
    case 0: k_strlcpy(out, g_theme->name, size); break;
    case 1: k_strlcpy(out, g_accents[g_accent_index].name, size); break;
    case 2: k_strlcpy(out, wallpapers[g_ws.wallpaper], size); break;
    case 3: k_strlcpy(out, g_ws.clock24 ? "24-hour" : "12-hour", size); break;
    case 4: k_strlcpy(out, g_ws.clock_seconds ? "Shown" : "Hidden", size); break;
    case 5: k_strlcpy(out, g_ws.locked ? "Locked" : "Unlocked", size); break;
    }
}

static void set_init(struct window *w, const char *arg)
{
    (void)arg;
    k_strlcpy(w->title, "Skarlet Settings", sizeof w->title);
}

static void set_draw(struct window *w, int x, int y, int cw, int ch, int focused)
{
    const struct theme *t = g_theme;
    gfx_text(x + 2, y + 1, "Workspace Appearance and Behavior", t->menu_head);
    for (int i = 0; i < ARRAY_LEN(setting_names); i++) {
        int sel = i == w->s.list.sel;
        uint8_t a = sel && focused ? t->sel : t->win;
        char val[24];
        set_value(i, val, sizeof val);
        gfx_fill(x + 1, y + 3 + i, cw - 2, 1, ' ', a);
        gfx_text(x + 3, y + 3 + i, setting_names[i], a);
        gfx_put(x + 20, y + 3 + i, CH_LTRI, a);
        gfx_text(x + 22, y + 3 + i, val, a);
        gfx_put(x + 37, y + 3 + i, CH_RTRI, a);
        /* A swatch shows the accent colour itself. */
        if (i == 1)
            gfx_text(x + 39, y + 3 + i, "  ", g_theme->accent);
    }
    gfx_text(x + 2, y + ch - 2, "Up/Down: choose   Left/Right/Enter: change", t->win_dim);
}

static void set_change(int i, int dir)
{
    switch (i) {
    case 0: theme_apply(g_theme_index + dir, g_accent_index); break;
    case 1: theme_apply(g_theme_index, g_accent_index + dir); break;
    case 2: g_ws.wallpaper = (g_ws.wallpaper + dir + ARRAY_LEN(wallpapers)) % ARRAY_LEN(wallpapers); break;
    case 3: g_ws.clock24 ^= 1; break;
    case 4: g_ws.clock_seconds ^= 1; break;
    case 5: g_ws.locked ^= 1; break;
    }
}

static void set_key(struct window *w, struct key k)
{
    int *sel = &w->s.list.sel;
    if (k.code == K_UP && *sel > 0)
        (*sel)--;
    else if (k.code == K_DOWN && *sel < ARRAY_LEN(setting_names) - 1)
        (*sel)++;
    else if (k.code == K_RIGHT || k.code == K_ENTER)
        set_change(*sel, 1);
    else if (k.code == K_LEFT)
        set_change(*sel, -1);
}

static const struct app settings_app = { 50, 13, set_init, set_draw, set_key, 0 };

/* ======================================================================== */
/* Skarlet Monitor (process list)                                            */
/* ======================================================================== */

struct monitor_ctx {
    int x, y, cw, row, sel, focused, count;
    int pids[MAX_WIN];
};

static void monitor_row(void *vctx, int pid, const char *title, int app, int desk)
{
    struct monitor_ctx *c = vctx;
    const struct theme *t = g_theme;
    if (c->count < MAX_WIN)
        c->pids[c->count] = pid;
    uint8_t a = (c->count == c->sel && c->focused) ? t->sel : t->win;
    char line[96];
    k_snprintf(line, sizeof line, " %5d  %-15s %4d  %s", pid, g_apps[app].id, desk + 1, title);
    gfx_textw(c->x, c->y + c->row, line, c->cw, a);
    c->row++;
    c->count++;
}

static struct monitor_ctx monitor_last;

static void mon_init(struct window *w, const char *arg)
{
    (void)arg;
    k_strlcpy(w->title, "Skarlet Monitor", sizeof w->title);
}

static void mon_draw(struct window *w, int x, int y, int cw, int ch, int focused)
{
    const struct theme *t = g_theme;
    char line[80];
    uint32_t up = svc_uptime();
    k_snprintf(line, sizeof line, " Uptime %u:%02u   Files %d/%d   Data %d KiB", up / 60,
               up % 60, vfs_used(), VFS_MAX_NODES, vfs_bytes_used() / 1024);
    gfx_textw(x, y, line, cw, t->win_dim);
    gfx_textw(x, y + 1, "   PID  Name            Desk  Window", cw, t->menu_head);
    struct monitor_ctx c = { x, y + 2, cw, 0, w->s.list.sel, focused, 0, { 0 } };
    svc_each_window(monitor_row, &c);
    if (w->s.list.sel >= c.count)
        w->s.list.sel = MAX(c.count - 1, 0);
    monitor_last = c;
    gfx_textw(x, y + ch - 1, " Delete: end the selected process", cw, t->win_dim);
}

static void mon_key(struct window *w, struct key k)
{
    int *sel = &w->s.list.sel;
    if (k.code == K_UP && *sel > 0)
        (*sel)--;
    else if (k.code == K_DOWN && *sel < monitor_last.count - 1)
        (*sel)++;
    else if (k.code == K_DELETE && *sel < monitor_last.count)
        svc_kill(monitor_last.pids[*sel]);
}

static const struct app monitor_app = { 58, 13, mon_init, mon_draw, mon_key, 0 };

const struct app *const g_app_impl[APP_COUNT] = {
    [APP_TERMINAL] = &terminal_app,
    [APP_FILES] = &files_app,
    [APP_WRITE] = &write_app,
    [APP_SETTINGS] = &settings_app,
    [APP_MONITOR] = &monitor_app,
};
