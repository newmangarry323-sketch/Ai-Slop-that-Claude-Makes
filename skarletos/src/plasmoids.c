/* plasmoids.c - desktop widgets.
 *
 * A plasmoid type is a little table of functions (init, draw, key) plus a
 * name and default size.  The desktop containment does not know anything
 * about clocks or notes; it just asks each plasmoid to draw itself inside its
 * rectangle and forwards keys to the focused one.  Adding a new widget means
 * writing three functions and adding one row to g_plasmoid_types.
 */
#include "desktop.h"
#include "gfx.h"
#include "lib.h"

/* ---- Folder View: the contents of ~/Desktop ----------------------------- */

static int desktop_dir(void) { return vfs_lookup(VFS_ROOT, "/home/user/Desktop"); }

static void fv_draw(struct plasmoid *p, int x, int y, int w, int h, int focused)
{
    const struct theme *t = g_theme;
    int kids[VFS_MAX_NODES];
    int dir = desktop_dir();
    int n = dir < 0 ? 0 : vfs_list(dir, kids, VFS_MAX_NODES);
    if (p->sel >= n)
        p->sel = MAX(n - 1, 0);
    if (p->sel < p->top)
        p->top = p->sel;
    if (p->sel >= p->top + h)
        p->top = p->sel - h + 1;
    if (n == 0)
        gfx_text(x + 1, y, "(empty)", t->widget);
    for (int r = 0; r < h && p->top + r < n; r++) {
        int node = kids[p->top + r];
        uint8_t a = (focused && p->top + r == p->sel) ? t->sel : t->widget;
        gfx_fill(x, y + r, w, 1, ' ', a);
        gfx_put(x + 1, y + r, vfs_is_dir(node) ? CH_SQUARE : CH_MENU, a);
        gfx_textw(x + 3, y + r, vfs_name(node), w - 4, a);
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
    uint8_t a = g_theme->notes;
    gfx_fill(x, y, w, h, ' ', a);
    /* Simple word-unaware wrapping. */
    int cx = 0, cy = 0;
    for (const char *s = p->text; *s && cy < h; s++) {
        if (*s == '\n' || cx >= w) {
            cy++;
            cx = 0;
            if (*s == '\n')
                continue;
        }
        if (cy < h)
            gfx_put(x + cx++, y + cy, (unsigned char)*s, a);
    }
    if (focused && cy < h)
        gfx_put(x + MIN(cx, w - 1), y + cy, '_', ATTR(g_accents[g_accent_index].dark, YELLOW));
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

/* ---- Digital clock with big digits -------------------------------------- */

/* 3x3 digit font: '#' = full block, '^' = upper half block. */
static const char *const big_digits[10][3] = {
    { "#^#", "# #", "^^^" }, { "^# ", " # ", "^^^" }, { "^^#", "#^^", "^^^" },
    { "^^#", " ^#", "^^^" }, { "# #", "^^#", "  ^" }, { "#^^", "^^#", "^^^" },
    { "#^^", "#^#", "^^^" }, { "^^#", "  #", "  ^" }, { "#^#", "#^#", "^^^" },
    { "#^#", "^^#", "^^^" },
};

static void big_digit(int x, int y, int d, uint8_t a)
{
    for (int r = 0; r < 3; r++)
        for (int c = 0; c < 3; c++) {
            char ch = big_digits[d][r][c];
            gfx_put(x + c, y + r, ch == '#' ? CH_FULL : ch == '^' ? CH_UPPER : ' ', a);
        }
}

static void clock_draw(struct plasmoid *p, int x, int y, int w, int h, int focused)
{
    (void)p;
    (void)focused;
    const struct theme *t = g_theme;
    int hour = g_ws.now.hour;
    if (!g_ws.clock24) {
        hour %= 12;
        if (hour == 0)
            hour = 12;
    }
    int digits[4] = { hour / 10, hour % 10, g_ws.now.minute / 10, g_ws.now.minute % 10 };
    int ox = x + (w - 17) / 2;
    uint8_t a = t->widget_head;
    for (int i = 0; i < 4; i++)
        big_digit(ox + i * 4 + (i >= 2 ? 3 : 0), y, digits[i], a);
    /* Blinking colon: shown on even seconds. */
    if (g_ws.now.second % 2 == 0) {
        gfx_put(ox + 8, y, 0xF9, a);
        gfx_put(ox + 8, y + 1, 0xF9, a);
    }
    if (h > 3) {
        static const char *const months[] = { "January", "February", "March", "April",
                                              "May", "June", "July", "August",
                                              "September", "October", "November", "December" };
        char date[32];
        int m = (g_ws.now.month >= 1 && g_ws.now.month <= 12) ? g_ws.now.month : 1;
        k_snprintf(date, sizeof date, "%d %s %d", g_ws.now.day, months[m - 1], g_ws.now.year);
        gfx_center(x, y + 3, w, date, t->widget);
    }
}

/* ---- System monitor ------------------------------------------------------ */

static void bar(int x, int y, int w, const char *label, int used, int total)
{
    const struct theme *t = g_theme;
    gfx_text(x, y, label, t->widget);
    int bw = w - 8;
    int fill = total > 0 ? used * bw / total : 0;
    for (int i = 0; i < bw; i++)
        gfx_put(x + 7 + i, y, i < fill ? CH_SHADE3 : CH_SHADE1,
                i < fill ? t->widget_head : t->widget_border);
}

static void sysmon_draw(struct plasmoid *p, int x, int y, int w, int h, int focused)
{
    (void)p;
    (void)focused;
    (void)h;
    struct window *vis[MAX_WIN];
    bar(x, y, w, "Files", vfs_used(), VFS_MAX_NODES);
    bar(x, y + 1, w, "Data", vfs_bytes_used(), VFS_MAX_NODES * VFS_FILE_MAX);
    bar(x, y + 2, w, "Windows", wm_list_visible(vis, MAX_WIN), MAX_WIN);
    char line[40];
    uint32_t up = svc_uptime();
    k_snprintf(line, sizeof line, "Uptime %u:%02u:%02u", up / 3600, (up / 60) % 60, up % 60);
    gfx_text(x, y + 3, line, g_theme->widget);
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
    (void)h;
    (void)focused;
    const struct theme *t = g_theme;
    x += (w - 12) / 2; /* centre the 12-column grid in the widget */
    for (int i = 0; i < 16; i++) {
        int v = p->tiles[i];
        char buf[4];
        if (v)
            k_snprintf(buf, sizeof buf, "%2d ", v);
        else
            k_strlcpy(buf, "   ", sizeof buf);
        /* Checkerboard colouring makes the 3-character tiles easy to tell apart. */
        uint8_t a = v == 0 ? t->widget : ((i % 4 + i / 4) % 2 ? t->accent_alt : t->accent);
        gfx_text(x + (i % 4) * 3, y + i / 4, buf, a);
    }
    if (fifteen_solved(p))
        gfx_text(x, y + 4, "Solved!", t->widget_head);
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
                        .desc = "Shows the files on your desktop", .icon = CH_SQUARE,
                        .header = 1, .w = 24, .h = 10, .draw = fv_draw, .key = fv_key },
    [PL_NOTES] = { .id = "notes", .name = "Notes", .desc = "A sticky note to type into",
                   .icon = CH_MENU, .w = 24, .h = 7, .init = notes_init, .draw = notes_draw,
                   .key = notes_key },
    [PL_CLOCK] = { .id = "clock", .name = "Digital Clock", .desc = "Big clock with the date",
                   .icon = CH_SUN, .w = 21, .h = 6, .draw = clock_draw },
    [PL_SYSMON] = { .id = "systemmonitor", .name = "System Monitor",
                    .desc = "File system and window usage", .icon = CH_UTRI, .header = 1,
                    .w = 26, .h = 8, .draw = sysmon_draw },
    [PL_FIFTEEN] = { .id = "fifteen", .name = "Fifteen Puzzle",
                     .desc = "Arrows slide tiles, n = new game", .icon = '#', .header = 1,
                     .w = 18, .h = 9, .init = fifteen_init, .draw = fifteen_draw,
                     .key = fifteen_key },
};
