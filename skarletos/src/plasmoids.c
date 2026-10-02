/* plasmoids.c - desktop widgets.
 *
 * A plasmoid type is a little table of functions (init, draw, key) plus a
 * name and default size.  The desktop containment does not know anything
 * about clocks or notes; it draws each widget's card (and title, if it has
 * one), asks the plasmoid to draw its contents inside, and forwards keys to
 * the focused one.  Adding a new widget means writing three functions and
 * adding one row to g_plasmoid_types.
 */
#include "desktop.h"
#include "gfx.h"
#include "lib.h"

/* ---- Folder View: the contents of ~/Desktop ----------------------------- */

#define FV_ROW 32

static int desktop_dir(void) { return vfs_lookup(VFS_ROOT, "/home/user/Desktop"); }

static void fv_draw(struct plasmoid *p, int x, int y, int w, int h, int focused)
{
    const struct theme *t = g_theme;
    int kids[VFS_MAX_NODES];
    int dir = desktop_dir();
    int n = dir < 0 ? 0 : vfs_list(dir, kids, VFS_MAX_NODES);
    int rows = h / FV_ROW;
    if (p->sel >= n)
        p->sel = MAX(n - 1, 0);
    if (p->sel < p->top)
        p->top = p->sel;
    if (p->sel >= p->top + rows)
        p->top = p->sel - rows + 1;
    if (n == 0)
        gfx_text(&font_ui, x + 8, y + 6, "(empty)", t->text_dim, 255);
    for (int r = 0; r < rows && p->top + r < n; r++) {
        int node = kids[p->top + r], selected = focused && p->top + r == p->sel;
        int ry = y + r * FV_ROW;
        if (selected)
            gfx_rrect(x, ry, w, FV_ROW - 4, 8, t->accent, 255);
        int isdir = vfs_is_dir(node);
        gfx_icon(isdir ? IC_FOLDER : IC_FILE, x + 8, ry + 5, 18,
                 selected ? WHITE : isdir ? RGB(0xd8, 0x96, 0x2e) : t->text_dim, 255);
        gfx_text_fit(&font_ui, x + 36, ry + 5, w - 44, vfs_name(node),
                     selected ? WHITE : t->text, 255);
    }
}

static int fv_key(struct plasmoid *p, struct key k)
{
    int kids[VFS_MAX_NODES];
    int dir = desktop_dir();
    int n = dir < 0 ? 0 : vfs_list(dir, kids, VFS_MAX_NODES);
    if (k.code == K_UP && p->sel > 0) {
        p->sel--;
    } else if (k.code == K_DOWN && p->sel < n - 1) {
        p->sel++;
    } else if (k.code == K_ENTER && n > 0) {
        char path[VFS_PATH_MAX];
        vfs_path(kids[p->sel], path, sizeof path);
        svc_launch(vfs_is_dir(kids[p->sel]) ? APP_FILES : APP_WRITE, path);
    } else {
        return 0;
    }
    return 1;
}

/* ---- Notes: a sticky note you can type into ------------------------------ */

static void notes_init(struct plasmoid *p)
{
    k_strlcpy(p->text, "Welcome! Type here.\nTab moves between widgets.", sizeof p->text);
}

static void notes_draw(struct plasmoid *p, int x, int y, int w, int h, int focused)
{
    const uint32_t ink = RGB(0x3a, 0x2c, 0x10);
    /* Wrap by pixel width (not at word boundaries, to keep it simple). */
    char line[96];
    int len = 0, lw = 0, ly = y;
    const struct font *f = &font_ui;
    for (const char *s = p->text;; s++) {
        int ch = (unsigned char)*s;
        int adv = (ch >= 32 && ch < 127) ? f->glyphs[ch - 32].adv : 0;
        if (!*s || *s == '\n' || lw + adv > w || len == (int)sizeof line - 1) {
            line[len] = 0;
            if (ly + f->line <= y + h)
                gfx_text(f, x, ly, line, ink, 255);
            if (!*s) {
                if (focused && ly + f->line <= y + h)
                    gfx_rect(x + lw + 1, ly + 1, 2, f->line - 2, g_theme->accent, 255);
                break;
            }
            ly += f->line + 2;
            len = lw = 0;
            if (*s == '\n')
                continue;
        }
        line[len++] = *s;
        lw += adv;
    }
}

static int notes_key(struct plasmoid *p, struct key k)
{
    int len = k_strlen(p->text);
    if (k.code == K_BACKSPACE) {
        if (len > 0)
            p->text[len - 1] = 0;
    } else if (k.code == K_ENTER || (k.code >= 32 && k.code < 127 && !(k.mods & (MOD_CTRL | MOD_ALT)))) {
        if (len < (int)sizeof p->text - 1) {
            p->text[len] = (char)k.code;
            p->text[len + 1] = 0;
        }
    } else {
        return 0;
    }
    return 1;
}

/* ---- Digital clock ------------------------------------------------------ */

static const char *const months[] = { "January", "February", "March", "April", "May", "June",
                                      "July", "August", "September", "October", "November",
                                      "December" };

static void clock_draw(struct plasmoid *p, int x, int y, int w, int h, int focused)
{
    (void)p;
    (void)focused;
    (void)h;
    const struct theme *t = g_theme;
    int hour = g_ws.now.hour;
    if (!g_ws.clock24) {
        hour %= 12;
        if (hour == 0)
            hour = 12;
    }
    char time[8];
    /* The colon blinks: shown on even seconds. */
    k_snprintf(time, sizeof time, "%02d%c%02d", hour, g_ws.now.second % 2 ? ' ' : ':',
               g_ws.now.minute);
    gfx_text_center(&font_big, x, y, w, time, t->text, 255);
    char date[32];
    int m = (g_ws.now.month >= 1 && g_ws.now.month <= 12) ? g_ws.now.month : 1;
    k_snprintf(date, sizeof date, "%d %s %d", g_ws.now.day, months[m - 1], g_ws.now.year);
    gfx_text_center(&font_ui, x, y + 72, w, date, t->text_dim, 255);
}

/* ---- System monitor ------------------------------------------------------ */

static void bar(int x, int y, int w, const char *label, int pct)
{
    const struct theme *t = g_theme;
    pct = MAX(0, MIN(pct, 100));
    gfx_text(&font_small, x, y, label, t->text_dim, 255);
    char num[8];
    k_snprintf(num, sizeof num, "%d%%", pct);
    gfx_text_right(&font_small, x + w, y, num, t->text_dim, 255);
    gfx_rrect(x, y + 18, w, 8, 4, t->input, 255);
    int fill = pct * w / 100;
    if (fill > 0)
        gfx_rrect(x, y + 18, MAX(fill, 8), 8, 4, t->accent, 255);
}

static void sysmon_draw(struct plasmoid *p, int x, int y, int w, int h, int focused)
{
    (void)p;
    (void)focused;
    (void)h;
    struct usage_bar bars[3];
    int n = plat_usage(bars, 3);
    for (int i = 0; i < n; i++)
        bar(x, y + 36 * i, w, bars[i].label, bars[i].pct);
    char line[40];
    uint32_t up = svc_uptime();
    k_snprintf(line, sizeof line, "Uptime %u:%02u:%02u", up / 3600, (up / 60) % 60, up % 60);
    gfx_text(&font_small, x, y + 110, line, g_theme->text_dim, 255);
}

/* The default bars: how full the in-memory file system is, and how many
 * windows are open.  The Linux session provides CPU, memory and disk. */
__attribute__((weak)) int plat_usage(struct usage_bar *out, int max)
{
    struct window *vis[MAX_WIN];
    int vals[3] = { vfs_used() * 100 / VFS_MAX_NODES,
                    (int)((int64_t)vfs_bytes_used() * 100 / (VFS_MAX_NODES * VFS_FILE_MAX)),
                    wm_list_visible(vis, MAX_WIN) * 100 / MAX_WIN };
    static const char *const labels[3] = { "Files", "Data", "Windows" };
    int n = MIN(max, 3);
    for (int i = 0; i < n; i++) {
        k_strlcpy(out[i].label, labels[i], sizeof out[i].label);
        out[i].pct = vals[i];
    }
    return n;
}

/* ---- Fifteen Puzzle: slide the tiles into order -------------------------- */

static int fifteen_blank(struct plasmoid *p)
{
    for (int i = 0; i < 16; i++)
        if (p->tiles[i] == 0)
            return i;
    return 15;
}

/* Slide the tile that is next to the blank in direction (dx,dy) into it. */
static int fifteen_move(struct plasmoid *p, int dx, int dy)
{
    int b = fifteen_blank(p), bx = b % 4, by = b / 4;
    int sx = bx - dx, sy = by - dy; /* the tile that moves into the blank */
    if (sx < 0 || sx > 3 || sy < 0 || sy > 3)
        return 0;
    p->tiles[b] = p->tiles[sy * 4 + sx];
    p->tiles[sy * 4 + sx] = 0;
    return 1;
}

static void fifteen_init(struct plasmoid *p)
{
    for (int i = 0; i < 16; i++)
        p->tiles[i] = (i + 1) % 16;
    /* Shuffle with random legal moves, so the puzzle is always solvable. */
    static const int dirs[4][2] = { { 1, 0 }, { -1, 0 }, { 0, 1 }, { 0, -1 } };
    for (int i = 0; i < 300; i++) {
        int d = (int)(k_rand() % 4);
        fifteen_move(p, dirs[d][0], dirs[d][1]);
    }
}

static int fifteen_solved(struct plasmoid *p)
{
    for (int i = 0; i < 16; i++)
        if (p->tiles[i] != (i + 1) % 16)
            return 0;
    return 1;
}

static void fifteen_draw(struct plasmoid *p, int x, int y, int w, int h, int focused)
{
    (void)focused;
    const struct theme *t = g_theme;
    int cell = MIN(w, h - 24) / 4, gap = 6, size = cell - gap;
    int ox = x + (w - cell * 4 + gap) / 2;
    uint32_t light = gfx_mix(t->accent, WHITE, 70);
    for (int i = 0; i < 16; i++) {
        int v = p->tiles[i];
        int tx = ox + (i % 4) * cell, ty = y + (i / 4) * cell;
        if (!v) {
            gfx_rrect(tx, ty, size, size, 9, t->input, 255);
            continue;
        }
        /* Checkerboard colouring makes neighbouring tiles easy to tell apart. */
        gfx_rrect(tx, ty, size, size, 9, (i % 4 + i / 4) % 2 ? light : t->accent, 255);
        char num[4];
        k_snprintf(num, sizeof num, "%d", v);
        gfx_text_center(&font_ui_bold, tx, ty + (size - font_ui_bold.line) / 2, size, num, WHITE,
                        255);
    }
    if (fifteen_solved(p))
        gfx_text_center(&font_ui_bold, x, y + cell * 4, w, "Solved!", t->accent_hi, 255);
}

static int fifteen_key(struct plasmoid *p, struct key k)
{
    switch (k.code) {
    case K_LEFT: fifteen_move(p, -1, 0); break;
    case K_RIGHT: fifteen_move(p, 1, 0); break;
    case K_UP: fifteen_move(p, 0, -1); break;
    case K_DOWN: fifteen_move(p, 0, 1); break;
    case 'n': fifteen_init(p); break;
    default: return 0;
    }
    return 1;
}

const struct plasmoid_type g_plasmoid_types[PL_COUNT] = {
    [PL_FOLDERVIEW] = { .id = "folderview", .name = "Folder View",
                        .desc = "Shows the files on your desktop", .icon = IC_FOLDER,
                        .header = 1, .w = 280, .h = 260, .draw = fv_draw, .key = fv_key },
    [PL_NOTES] = { .id = "notes", .name = "Notes", .desc = "A sticky note to type into",
                   .icon = IC_NOTES, .w = 280, .h = 170, .card = RGB(0xf6, 0xe0, 0x7a),
                   .init = notes_init, .draw = notes_draw, .key = notes_key },
    [PL_CLOCK] = { .id = "clock", .name = "Digital Clock", .desc = "Big clock with the date",
                   .icon = IC_CLOCK, .w = 320, .h = 132, .draw = clock_draw },
    [PL_SYSMON] = { .id = "systemmonitor", .name = "System Monitor",
                    .desc = "File system and window usage", .icon = IC_MONITOR, .header = 1,
                    .w = 300, .h = 200, .draw = sysmon_draw },
    [PL_FIFTEEN] = { .id = "fifteen", .name = "Fifteen Puzzle",
                     .desc = "Arrows slide tiles, n = new game", .icon = IC_PUZZLE, .header = 1,
                     .w = 260, .h = 300, .init = fifteen_init, .draw = fifteen_draw,
                     .key = fifteen_key },
};
