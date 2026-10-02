/* apps.c - the applications: Skarlet Terminal, Skarlet Files, Skarlet Write,
 * Skarlet Settings and Skarlet Monitor.  Each app is three functions (init,
 * draw, key) plus some state stored inside its window.  They are modelled on
 * KDE's Konsole, Dolphin, KWrite, System Settings and System Monitor, as tiny
 * re-imaginings, not ports. */
#include "desktop.h"
#include "gfx.h"
#include "lib.h"

const struct app_info g_apps[APP_SLOTS] = {
    [APP_TERMINAL] = { "skterm", "Skarlet Terminal", "Terminal", "System", IC_TERMINAL,
                       RGB(0x3a, 0x34, 0x40) },
    [APP_FILES] = { "skfiles", "Skarlet Files", "File Manager", "System", IC_FOLDER,
                    RGB(0xd0, 0x8a, 0x22) },
    [APP_WRITE] = { "skwrite", "Skarlet Write", "Text Editor", "Utilities", IC_FILE,
                    RGB(0x34, 0x74, 0xd0) },
    [APP_SETTINGS] = { "sksettings", "Skarlet Settings", "Configure the desktop", "Settings",
                       IC_SETTINGS, RGB(0x63, 0x67, 0x72) },
    [APP_MONITOR] = { "skmonitor", "Skarlet Monitor", "Process monitor", "System", IC_MONITOR,
                      RGB(0x22, 0x8f, 0x62) },
    /* Another program's window; its title comes from the program. */
    [APP_EXTERNAL] = { "app", "Application", "Application", "", IC_GRID,
                       RGB(0x5d, 0x57, 0x64) },
};

int svc_app_by_name(const char *name)
{
    for (int i = 0; i < APP_COUNT; i++)
        if (k_strcmp(g_apps[i].id, name) == 0)
            return i;
    return -1;
}

/* A selection highlight: an accent pill when the list has focus, a soft grey
 * one when it does not. */
static void sel_pill(int x, int y, int w, int h, int focused)
{
    if (focused)
        gfx_rrect(x, y, w, h, 7, g_theme->accent, 255);
    else
        gfx_rrect(x, y, w, h, 7, g_theme->hover, 255);
}

static uint32_t sel_text(int selected, int focused)
{
    return selected && focused ? g_theme->text_on_accent : g_theme->text;
}

/* ======================================================================== */
/* Skarlet Terminal                                                          */
/* ======================================================================== */

#define TERM_PAD 12
#define TERM_LH 18 /* line height in pixels */

static int term_cw(void) { return font_mono.glyphs['M' - 32].adv; }

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
    k->cols = MIN((w->w - 2 - 2 * TERM_PAD) / term_cw(), TERM_COLS);
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
    int CW = term_cw();
    gfx_rect(x, y, cw, ch, t->term_bg, 255);

    char prompt[VFS_PATH_MAX + 32];
    shell_prompt(&k->sh, prompt, sizeof prompt);

    int rows = (ch - 2 * TERM_PAD + 4) / TERM_LH;
    k->rows = rows;
    /* Row list = output lines (dropping an empty last line) + prompt line. */
    int out_rows = k->nlines;
    if (out_rows > 0 && k->col == 0 && k->lines[k->nlines - 1][0] == 0)
        out_rows--;
    int total = out_rows + 1;
    int first = total - rows - k->scroll;
    if (first < 0)
        first = 0;

    int maxc = (cw - 2 * TERM_PAD) / CW;
    for (int row = 0; row < rows && first + row < total; row++) {
        int idx = first + row, ly = y + TERM_PAD + row * TERM_LH;
        if (idx < out_rows) {
            gfx_text(&font_mono, x + TERM_PAD, ly, k->lines[idx], t->term_fg, 255);
            continue;
        }
        /* The prompt in the accent tint, then the typed text.  If it is too
         * long for the window, show the end of it. */
        int plen = k_strlen(prompt);
        int over = plen + k->len + 1 - maxc;
        int px = x + TERM_PAD;
        if (over < plen)
            px += gfx_text(&font_mono, px, ly, prompt + MAX(over, 0), t->term_prompt, 255);
        px += gfx_text(&font_mono, px, ly, k->input + MAX(over - plen, 0), t->term_fg, 255);
        if (focused && k->scroll == 0)
            gfx_rect(px + 1, ly + 1, CW - 1, TERM_LH - 2, t->term_fg, 200);
    }
    if (k->scroll) {
        int pw = gfx_text_width(&font_small, "scrollback") + 20;
        gfx_rrect(x + cw - pw - 12, y + 8, pw, 22, 11, t->accent, 255);
        gfx_text(&font_small, x + cw - pw + -2, y + 12, "scrollback", WHITE, 255);
    }
}

static void term_key(struct window *w, struct key key)
{
    struct term_state *k = &w->s.term;
    int c = key.code;

    if ((key.mods & MOD_SHIFT) && (c == K_PGUP || c == K_PGDN)) {
        int page = MAX(k->rows - 1, 4);
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

/* The wheel scrolls back through the output. */
static void term_mouse(struct window *w, struct mouse m, int cw, int ch)
{
    (void)cw, (void)ch;
    struct term_state *k = &w->s.term;
    if (m.type == MOUSE_WHEEL)
        k->scroll = MAX(0, MIN(k->scroll + (m.button == 4 ? 3 : -3), k->nlines));
}

static const struct app terminal_app = {
    .w = 780, .h = 480, .init = term_init, .draw = term_draw, .key = term_key,
    .idle = term_idle, .mouse = term_mouse,
};

/* ======================================================================== */
/* Skarlet Files                                                             */
/* ======================================================================== */

static const struct { const char *name, *path; int icon; } places[] = {
    { "Home", "/home/user", IC_HOME },
    { "Desktop", "/home/user/Desktop", IC_DESKTOP },
    { "Documents", "/home/user/Documents", IC_FILE },
    { "Music", "/home/user/Music", IC_MUSIC },
    { "Root", "/", IC_DRIVE },
    { "Temp", "/tmp", IC_RECENT },
};

#define FILES_SIDEBAR 190
#define FILES_ROW 34

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

    /* Sidebar with the places. */
    gfx_rect(x, y, FILES_SIDEBAR, ch, t->titlebar, 255);
    gfx_text(&font_small, x + 18, y + 14, "PLACES", t->text_dim, 255);
    for (int i = 0; i < ARRAY_LEN(places); i++) {
        int ry = y + 36 + i * 36;
        int selected = d->pane == 0 && d->place == i;
        if (selected)
            sel_pill(x + 8, ry, FILES_SIDEBAR - 16, 30, focused);
        uint32_t tc = sel_text(selected, focused);
        gfx_icon(places[i].icon, x + 18, ry + 6, 18, selected && focused ? WHITE : t->accent_hi,
                 255);
        gfx_text(&font_ui, x + 46, ry + 6, places[i].name, tc, 255);
    }

    /* Toolbar: back/up buttons and the location as a "breadcrumb" field. */
    int lx = x + FILES_SIDEBAR, lw = cw - FILES_SIDEBAR;
    char path[VFS_PATH_MAX];
    vfs_path(d->dir, path, sizeof path);
    gfx_icon(IC_CHEVRON_L, lx + 14, y + 13, 18, t->text_dim, 255);
    gfx_icon(IC_CHEVRON_R, lx + 40, y + 13, 18, t->text_dim, 160);
    gfx_rrect(lx + 70, y + 8, lw - 84, 30, 15, t->input, 255);
    gfx_icon(IC_FOLDER, lx + 82, y + 14, 18, t->accent_hi, 255);
    gfx_text_fit(&font_ui, lx + 108, y + 14, lw - 130, path, t->text, 255);

    /* The file list. */
    int kids[VFS_MAX_NODES];
    int n = vfs_list(d->dir, kids, VFS_MAX_NODES);
    int ly = y + 50, rows = (ch - 50 - 34) / FILES_ROW;
    if (d->sel >= n)
        d->sel = MAX(n - 1, 0);
    if (d->sel < d->top)
        d->top = d->sel;
    if (d->sel >= d->top + rows)
        d->top = d->sel - rows + 1;
    if (n == 0)
        gfx_text(&font_ui, lx + 24, ly + 12, "Folder is empty", t->text_dim, 255);
    for (int r = 0; r < rows && d->top + r < n; r++) {
        int node = kids[d->top + r];
        int selected = d->top + r == d->sel;
        int ry = ly + r * FILES_ROW;
        if (selected)
            sel_pill(lx + 10, ry, lw - 20, FILES_ROW - 4, d->pane == 1 && focused);
        uint32_t tc = sel_text(selected, d->pane == 1 && focused);
        int dir = vfs_is_dir(node);
        gfx_icon(dir ? IC_FOLDER : IC_FILE, lx + 22, ry + 6, 18,
                 selected && d->pane == 1 && focused ? WHITE
                 : dir ? RGB(0xd8, 0x96, 0x2e) : t->text_dim,
                 255);
        gfx_text_fit(&font_ui, lx + 50, ry + 6, lw - 170, vfs_name(node), tc, 255);
        char size[16];
        if (dir)
            k_strlcpy(size, "Folder", sizeof size);
        else
            k_snprintf(size, sizeof size, "%d B", vfs_size(node));
        gfx_text_right(&font_ui, lx + lw - 26, ry + 6, size,
                       selected && d->pane == 1 && focused ? WHITE : t->text_dim, 255);
    }

    /* Status bar. */
    int dirs = 0;
    for (int i = 0; i < n; i++)
        dirs += vfs_is_dir(kids[i]);
    char status[64];
    k_snprintf(status, sizeof status, "%d folders, %d files", dirs, n - dirs);
    gfx_rect(lx, y + ch - 30, lw, 1, t->divider, 255);
    gfx_text(&font_small, lx + 20, y + ch - 22, status, t->text_dim, 255);
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
            int dir = vfs_lookup(VFS_ROOT, places[d->place].path);
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

/* Click a place or a file to select it; double-click a file or folder to
 * open it; the back arrow goes up a folder; the wheel scrolls. */
static void files_mouse(struct window *w, struct mouse m, int cw, int ch)
{
    struct files_state *d = &w->s.files;
    if (m.type == MOUSE_WHEEL) {
        for (int i = 0; i < 3; i++)
            files_key(w, (struct key){ m.button == 4 ? K_UP : K_DOWN, 0 });
        return;
    }
    if (m.type != MOUSE_DOWN || m.button != 1)
        return;
    if (m.x < FILES_SIDEBAR) {
        int i = (m.y - 36) / 36;
        if (m.y >= 36 && i < ARRAY_LEN(places)) {
            d->pane = 0;
            d->place = i;
            files_key(w, (struct key){ K_ENTER, 0 });
        }
        return;
    }
    int lx = m.x - FILES_SIDEBAR;
    if (m.y < 46) {
        if (lx < 36)
            files_key(w, (struct key){ K_BACKSPACE, 0 });
        return;
    }
    int r = (m.y - 50) / FILES_ROW, rows = (ch - 50 - 34) / FILES_ROW;
    int kids[VFS_MAX_NODES];
    int n = vfs_list(d->dir, kids, VFS_MAX_NODES);
    (void)cw;
    if (m.y < 50 || r >= rows || d->top + r >= n)
        return;
    d->pane = 1;
    if (m.clicks >= 2 && d->sel == d->top + r)
        files_key(w, (struct key){ K_ENTER, 0 });
    else
        d->sel = d->top + r;
}

static const struct app files_app = {
    .w = 780, .h = 480, .init = files_init, .draw = files_draw, .key = files_key,
    .mouse = files_mouse,
};

/* ======================================================================== */
/* Skarlet Write                                                             */
/* ======================================================================== */

#define ED_GUTTER 54
#define ED_LH 19

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
    int CW = term_cw(), rows = (ch - 30 - 16) / ED_LH;

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

    gfx_rect(x, y, ED_GUTTER, ch - 30, t->titlebar, 255);
    /* Draw line by line: each line of the buffer becomes one text run. */
    char line_buf[256];
    int line = 0, start = 0;
    for (int i = 0; i <= e->len; i++) {
        if (i < e->len && e->buf[i] != '\n')
            continue;
        int r = line - e->top;
        if (r >= 0 && r < rows) {
            int ly = y + 8 + r * ED_LH;
            char num[8];
            k_snprintf(num, sizeof num, "%d", line + 1);
            gfx_text_right(&font_mono, x + ED_GUTTER - 12, ly, num,
                           line == cur_line ? t->text : t->text_dim, 255);
            int n = MIN(i - start, (int)sizeof line_buf - 1);
            k_memcpy(line_buf, e->buf + start, n);
            line_buf[n] = 0;
            if (line == cur_line && focused)
                gfx_rect(x + ED_GUTTER, ly - 1, cw - ED_GUTTER, ED_LH, t->hover, 255);
            gfx_text(&font_mono, x + ED_GUTTER + 10, ly, line_buf, t->text, 255);
            if (line == cur_line && focused)
                gfx_rect(x + ED_GUTTER + 10 + cur_col * CW, ly - 1, 2, ED_LH, t->accent_hi, 255);
        }
        line++;
        start = i + 1;
    }
    char status[96];
    k_snprintf(status, sizeof status, "Line %d, Column %d    %d bytes    Ctrl+S to save",
               cur_line + 1, cur_col + 1, e->len);
    gfx_rect(x, y + ch - 30, cw, 1, t->divider, 255);
    gfx_text(&font_small, x + 16, y + ch - 22, status, t->text_dim, 255);
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

/* A click puts the cursor where you clicked; the wheel moves it by lines. */
static void ed_mouse(struct window *w, struct mouse m, int cw, int ch)
{
    struct edit_state *e = &w->s.ed;
    (void)cw, (void)ch;
    if (m.type == MOUSE_WHEEL) {
        for (int i = 0; i < 3; i++)
            ed_key(w, (struct key){ m.button == 4 ? K_UP : K_DOWN, 0 });
        return;
    }
    if (m.type != MOUSE_DOWN || m.button != 1 || m.y < 8)
        return;
    int line = e->top + (m.y - 8) / ED_LH;
    int col = MAX(0, (m.x - ED_GUTTER - 10 + term_cw() / 2) / term_cw());
    int pos = 0;
    for (int l = 0; l < line && pos < e->len; pos++)
        if (e->buf[pos] == '\n')
            l++;
    if (pos > e->len)
        pos = e->len;
    e->cur = MIN(pos + col, ed_line_end(e, pos));
}

static const struct app write_app = {
    .w = 760, .h = 500, .init = ed_init, .draw = ed_draw, .key = ed_key, .mouse = ed_mouse,
};

/* ======================================================================== */
/* Skarlet Settings                                                          */
/* ======================================================================== */

static const char *const setting_names[] = {
    "Theme", "Accent colour", "Wallpaper", "Clock format", "Clock seconds", "Widgets",
};
static const char *const wallpapers[] = { "Glow", "Dots", "Plain" };

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
    gfx_text(&font_title, x + 28, y + 20, "Appearance", t->text, 255);
    gfx_text(&font_ui, x + 28, y + 52, "Workspace look and behaviour", t->text_dim, 255);
    for (int i = 0; i < ARRAY_LEN(setting_names); i++) {
        int ry = y + 88 + i * 52;
        int selected = i == w->s.list.sel;
        gfx_rrect(x + 20, ry, cw - 40, 44, 10, selected ? t->hover : t->window, 255);
        if (selected)
            gfx_rrect(x + 20, ry + 10, 4, 24, 2, focused ? t->accent : t->text_dim, 255);
        gfx_text(&font_ui, x + 40, ry + 13, setting_names[i], t->text, 255);
        char val[24];
        set_value(i, val, sizeof val);
        int right = x + cw - 40;
        if (i == 1) {
            /* The accent row shows every colour; the chosen one is ringed. */
            for (int a = g_accent_count - 1, cx = right - 12; a >= 0; a--, cx -= 30) {
                gfx_circle(cx, ry + 22, 10, g_accents[a].color, 255);
                if (a == g_accent_index)
                    gfx_ring(cx, ry + 22, 14, 2, t->text, 255);
            }
            gfx_text_right(&font_ui_bold, right - 30 * g_accent_count - 8, ry + 13, val,
                           t->text, 255);
        } else {
            /* A "spin" control: < value >. */
            int vw = 150;
            gfx_rrect(right - vw, ry + 7, vw, 30, 15, t->input, 255);
            gfx_icon(IC_CHEVRON_L, right - vw + 6, ry + 13, 18, t->text_dim, 255);
            gfx_icon(IC_CHEVRON_R, right - 24, ry + 13, 18, t->text_dim, 255);
            gfx_text_center(&font_ui, right - vw + 24, ry + 13, vw - 48, val, t->text, 255);
        }
    }
    gfx_text(&font_small, x + 28, y + ch - 24, "Up/Down: choose    Left/Right/Enter: change",
             t->text_dim, 255);
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

/* Click a row to select it; click the arrows of its control (or a colour
 * swatch) to change it. */
static void set_mouse(struct window *w, struct mouse m, int cw, int ch)
{
    (void)ch;
    if (m.type != MOUSE_DOWN || m.button != 1)
        return;
    int i = (m.y - 88) / 52;
    if (m.y < 88 || i >= ARRAY_LEN(setting_names) || (m.y - 88) % 52 >= 44)
        return;
    w->s.list.sel = i;
    int right = cw - 40;
    if (i == 1) {
        for (int a = g_accent_count - 1, cx = right - 12; a >= 0; a--, cx -= 30)
            if (m.x >= cx - 14 && m.x < cx + 14)
                theme_apply(g_theme_index, a);
    } else if (m.x >= right - 150 && m.x < right) {
        set_change(i, m.x < right - 75 ? -1 : 1);
    }
}

static const struct app settings_app = {
    .w = 640, .h = 500, .init = set_init, .draw = set_draw, .key = set_key, .mouse = set_mouse,
};

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
    if (c->count < MAX_WIN)
        c->pids[c->count] = pid;
    int ry = c->y + c->row * 34, selected = c->count == c->sel;
    if (selected)
        sel_pill(c->x + 12, ry, c->cw - 24, 30, c->focused);
    uint32_t tc = sel_text(selected, c->focused);
    char num[16];
    k_snprintf(num, sizeof num, "%d", pid);
    gfx_text(&font_ui, c->x + 28, ry + 6, num, tc, 255);
    gfx_app_icon(g_apps[app].icon, c->x + 90, ry + 5, 20, g_apps[app].color);
    gfx_text(&font_ui, c->x + 118, ry + 6, g_apps[app].id, tc, 255);
    k_snprintf(num, sizeof num, "%d", desk + 1);
    gfx_text(&font_ui, c->x + 250, ry + 6, num, tc, 255);
    gfx_text_fit(&font_ui, c->x + 310, ry + 6, c->cw - 330, title, tc, 255);
    c->row++;
    c->count++;
}

static struct monitor_ctx monitor_last;

static void mon_init(struct window *w, const char *arg)
{
    (void)arg;
    k_strlcpy(w->title, "Skarlet Monitor", sizeof w->title);
}

static void stat_card(int x, int y, int w, const char *label, const char *value)
{
    gfx_rrect(x, y, w, 62, 10, g_theme->input, 255);
    gfx_text(&font_small, x + 14, y + 10, label, g_theme->text_dim, 255);
    gfx_text(&font_title, x + 14, y + 28, value, g_theme->text, 255);
}

static void mon_draw(struct window *w, int x, int y, int cw, int ch, int focused)
{
    const struct theme *t = g_theme;
    char v[32];
    uint32_t up = svc_uptime();
    int cardw = (cw - 24 * 2 - 2 * 12) / 3;
    k_snprintf(v, sizeof v, "%u:%02u", up / 60, up % 60);
    stat_card(x + 24, y + 16, cardw, "Uptime", v);
    k_snprintf(v, sizeof v, "%d / %d", vfs_used(), VFS_MAX_NODES);
    stat_card(x + 24 + cardw + 12, y + 16, cardw, "Files", v);
    k_snprintf(v, sizeof v, "%d KiB", vfs_bytes_used() / 1024);
    stat_card(x + 24 + 2 * (cardw + 12), y + 16, cardw, "Data", v);

    int hy = y + 96;
    gfx_text(&font_small, x + 28, hy, "PID", t->text_dim, 255);
    gfx_text(&font_small, x + 118, hy, "Name", t->text_dim, 255);
    gfx_text(&font_small, x + 250, hy, "Desk", t->text_dim, 255);
    gfx_text(&font_small, x + 310, hy, "Window", t->text_dim, 255);
    gfx_rect(x + 20, hy + 20, cw - 40, 1, t->divider, 255);
    struct monitor_ctx c = { x, hy + 28, cw, 0, w->s.list.sel, focused, 0, { 0 } };
    svc_each_window(monitor_row, &c);
    if (w->s.list.sel >= c.count)
        w->s.list.sel = MAX(c.count - 1, 0);
    monitor_last = c;
    gfx_text(&font_small, x + 28, y + ch - 24, "Delete: end the selected process", t->text_dim,
             255);
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

static void mon_mouse(struct window *w, struct mouse m, int cw, int ch)
{
    (void)cw, (void)ch;
    int r = (m.y - 124) / 34;
    if (m.type == MOUSE_DOWN && m.button == 1 && m.y >= 124 && r < monitor_last.count)
        w->s.list.sel = r;
}

static const struct app monitor_app = {
    .w = 720, .h = 440, .init = mon_init, .draw = mon_draw, .key = mon_key, .mouse = mon_mouse,
};

/* Other programs draw their own windows and get their own keys and clicks,
 * so the external "app" does nothing. */
static void ext_init(struct window *w, const char *arg) { (void)w, (void)arg; }
static void ext_draw(struct window *w, int x, int y, int cw, int ch, int focused)
{
    (void)w, (void)x, (void)y, (void)cw, (void)ch, (void)focused;
}
static void ext_key(struct window *w, struct key k) { (void)w, (void)k; }
static const struct app external_app = {
    .w = 640, .h = 480, .init = ext_init, .draw = ext_draw, .key = ext_key,
};

const struct app *const g_app_impl[APP_SLOTS] = {
    [APP_TERMINAL] = &terminal_app,
    [APP_FILES] = &files_app,
    [APP_WRITE] = &write_app,
    [APP_SETTINGS] = &settings_app,
    [APP_MONITOR] = &monitor_app,
    [APP_EXTERNAL] = &external_app,
};
