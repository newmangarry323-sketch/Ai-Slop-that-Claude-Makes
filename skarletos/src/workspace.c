/* workspace.c - the Plasma-style shell: desktop containment, panel,
 * Skarlet Launcher (after KDE's Kickoff), Skarlet Runner (after KRunner), the
 * desktop menu, the Add Widgets and Activities sheets, and notifications.
 * It owns the main loop (ws_tick) and decides which part of the screen gets
 * each key press.
 *
 * The look follows current desktops such as KDE Plasma 6: a floating,
 * translucent panel with icon-only task buttons, rounded cards with soft
 * shadows, a launcher with a sidebar and a search field, a runner centred
 * near the top of the screen, and notifications as cards above the panel.
 */
#include "desktop.h"
#include "gfx.h"
#include "lib.h"

struct workspace g_ws;

/* Empty defaults for the optional platform hooks (see platform.h). */
__attribute__((weak)) int plat_login(const char *password)
{
    (void)password;
    return 1;
}
__attribute__((weak)) const char *plat_login_hint(void)
{
    return "Press Enter to log in (any password)";
}
__attribute__((weak)) int plat_app_count(void) { return 0; }
__attribute__((weak)) const struct installed_app *plat_app(int i)
{
    (void)i;
    return 0;
}
__attribute__((weak)) void plat_app_start(int i) { (void)i; }

/* ======================================================================== */
/* Actions: everything a menu entry, launcher entry or runner result can do  */
/* ======================================================================== */

enum {
    ACT_NONE, ACT_LAUNCH, ACT_PLACE, ACT_OPENFILE, ACT_RUN, ACT_CALC, ACT_CATEGORY,
    ACT_LOGOUT, ACT_REBOOT, ACT_POWEROFF,
    ACT_STRIP_WIDGETS, ACT_ADDW, ACT_REMOVEW, ACT_STRIP_ACTIVITIES, ACT_SWITCH_ACT,
    ACT_NEW_ACT, ACT_TOGGLE_LOCK, ACT_HELP, ACT_INSTALLED,
};

struct item {
    char label[40];
    char hint[48];
    int act, arg;
    char str[VFS_PATH_MAX];
    int disabled;
    int header;    /* a section title, not selectable */
    int icon;      /* IC_* */
    uint32_t tint; /* tile colour behind the icon */
};

#define MAX_ITEMS 24

static struct item items[MAX_ITEMS];
static int nitems, sel;

static void run_item(struct item *it);

static struct item *add_item(const char *label, const char *hint, int act, int arg,
                             const char *str)
{
    if (nitems >= MAX_ITEMS)
        return 0;
    struct item *it = &items[nitems++];
    k_memset(it, 0, sizeof *it);
    k_strlcpy(it->label, label, sizeof it->label);
    k_strlcpy(it->hint, hint ? hint : "", sizeof it->hint);
    it->act = act;
    it->arg = arg;
    k_strlcpy(it->str, str ? str : "", sizeof it->str);
    it->icon = -1;
    return it;
}

static void set_icon(struct item *it, int icon, uint32_t tint)
{
    if (it) {
        it->icon = icon;
        it->tint = tint;
    }
}

static void add_header(const char *title)
{
    struct item *it = add_item(title, 0, ACT_NONE, 0, 0);
    if (it)
        it->header = 1;
}

/* Drop a section header that ended up with nothing under it. */
static void end_section(int header_index)
{
    if (nitems == header_index + 1)
        nitems--;
}

static void sel_first(void)
{
    sel = 0;
    while (sel < nitems && items[sel].header)
        sel++;
    if (sel >= nitems)
        sel = 0;
}

static void sel_move(int dir)
{
    int i = sel + dir;
    while (i >= 0 && i < nitems && items[i].header)
        i += dir;
    if (i >= 0 && i < nitems)
        sel = i;
}

static void sel_fix(void)
{
    if (sel >= nitems || sel < 0 || items[sel].header)
        sel_first();
}

/* ---- popups ------------------------------------------------------------- */

enum { POP_NONE, POP_LAUNCHER, POP_RUNNER, POP_MENU, POP_STRIP };
enum { STRIP_WIDGETS, STRIP_ACTIVITIES };
static int popup, strip_kind;
static char query[40];
static int qlen;
static int launch_tab, launch_cat = -1, launch_top;
static char login_pw[64];
static int login_failed;
static int recent[5], nrecent;
static char recent_docs[4][VFS_PATH_MAX];
static int nrecent_docs;

static void close_popup(void)
{
    popup = POP_NONE;
    nitems = 0;
}

static void remember_recent(int app)
{
    int keep[5], n = 0;
    keep[n++] = app;
    for (int i = 0; i < nrecent && n < 5; i++)
        if (recent[i] != app)
            keep[n++] = recent[i];
    for (int i = 0; i < n; i++)
        recent[i] = keep[i];
    nrecent = n;
}

void ws_note_document(const char *path)
{
    char keep[4][VFS_PATH_MAX];
    int n = 0;
    k_strlcpy(keep[n++], path, VFS_PATH_MAX);
    for (int i = 0; i < nrecent_docs && n < 4; i++)
        if (k_strcmp(recent_docs[i], path) != 0)
            k_strlcpy(keep[n++], recent_docs[i], VFS_PATH_MAX);
    for (int i = 0; i < n; i++)
        k_strlcpy(recent_docs[i], keep[i], VFS_PATH_MAX);
    nrecent_docs = n;
}

/* ---- clickable regions and layers ------------------------------------------
 * While a frame is drawn, each clickable thing records its rectangle; a click
 * then goes to the top-most rectangle under the pointer.  Recording them in
 * the drawing code keeps what you see and what you can click in step. */

#define MAX_HITS 200
static struct {
    int x, y, w, h, kind, arg;
} hits[MAX_HITS];
static int nhits;

#define MAX_LAYERS 8
static struct layer layers[MAX_LAYERS];
static int nlayers;

void ws_hit(int x, int y, int w, int h, int kind, int arg)
{
    if (nhits >= MAX_HITS)
        return;
    hits[nhits].x = x, hits[nhits].y = y, hits[nhits].w = w, hits[nhits].h = h;
    hits[nhits].kind = kind, hits[nhits].arg = arg;
    nhits++;
}

int ws_hit_at(int x, int y, int *arg)
{
    for (int i = nhits - 1; i >= 0; i--)
        if (x >= hits[i].x && x < hits[i].x + hits[i].w && y >= hits[i].y &&
            y < hits[i].y + hits[i].h) {
            if (arg)
                *arg = hits[i].arg;
            return hits[i].kind;
        }
    return HIT_NONE;
}

static void add_layer(int x, int y, int w, int h, int r)
{
    if (nlayers < MAX_LAYERS) {
        struct layer l = { x, y, w, h, r };
        layers[nlayers++] = l;
    }
}

int ws_layers(struct layer *out, int max)
{
    int n = MIN(nlayers, max);
    for (int i = 0; i < n; i++)
        out[i] = layers[i];
    return n;
}

int ws_popup_open(void) { return popup != POP_NONE; }

/* ---- drawing helpers ------------------------------------------------------ */

/* A floating card: soft shadow, translucent fill, hairline border.  Cards
 * float above the windows, so each is also a layer; clicks inside one that
 * hit nothing else are swallowed rather than going to what is behind. */
static void card(int x, int y, int w, int h, int r)
{
    const struct theme *t = g_theme;
    add_layer(x, y, w, h, r);
    ws_hit(x, y, w, h, HIT_SWALLOW, 0);
    gfx_shadow(x, y + 6, w, h, r, 28, t->shadow_a);
    gfx_rrect(x, y, w, h, r, t->card, t->card_a);
    gfx_rrect_line(x, y, w, h, r, t->border, 160);
}

/* An icon on a coloured rounded tile (or bare, if the item has no tint). */
static void item_icon(const struct item *it, int x, int y, int size, int selected)
{
    if (it->icon < 0)
        return;
    if (it->tint)
        gfx_app_icon(it->icon, x, y, size, it->tint);
    else
        gfx_icon(it->icon, x + size / 6, y + size / 6, size * 2 / 3,
                 selected ? WHITE : g_theme->accent_hi, 255);
}

static uint32_t neutral_tint(void) { return RGB(0x5d, 0x57, 0x64); }

/* ======================================================================== */
/* Notifications                                                             */
/* ======================================================================== */

void svc_notify(const char *title, const char *text)
{
    k_strlcpy(g_ws.toast_title, title, sizeof g_ws.toast_title);
    k_strlcpy(g_ws.toast_text, text, sizeof g_ws.toast_text);
    g_ws.toast_until = g_ws.uptime + 5;
    g_ws.need_redraw = 1;
}

uint32_t svc_uptime(void) { return g_ws.uptime; }

static int toast_active(void) { return g_ws.toast_title[0] && g_ws.uptime < g_ws.toast_until; }

#define SHEET_H 236

/* A notification is a card above the system tray: an icon, a title, the
 * message and a close button.  It moves up when a sheet is open. */
static void draw_toast(void)
{
    if (!toast_active())
        return;
    const struct theme *t = g_theme;
    int w = 400, h = 96, x = g_w - w - 16, y = PANEL_Y - 12 - h;
    if (popup == POP_STRIP)
        y = PANEL_Y - 12 - SHEET_H - 12 - h;
    card(x, y, w, h, 16);
    ws_hit(x, y, w, h, HIT_TOAST, 0); /* a click dismisses it */
    gfx_circle(x + 36, y + 36, 20, t->accent, 255);
    gfx_icon(IC_BELL, x + 24, y + 24, 24, WHITE, 255);
    gfx_text_fit(&font_ui_bold, x + 68, y + 18, w - 110, g_ws.toast_title, t->text, 255);
    gfx_icon(IC_CLOSE, x + w - 34, y + 16, 16, t->text_dim, 255);
    /* Wrap the message over two lines, breaking at the last space that fits. */
    const char *msg = g_ws.toast_text;
    char line[100];
    int tw = w - 88, len = MAX(0, MIN(k_strlen(msg), (int)sizeof line - 1)), cut = len;
    if (gfx_text_width(&font_ui, msg) > tw) {
        for (cut = len; cut > 0; cut--) {
            if (msg[cut] != ' ')
                continue;
            k_strlcpy(line, msg, cut + 1);
            if (gfx_text_width(&font_ui, line) <= tw)
                break;
        }
        if (cut == 0)
            cut = len;
    }
    k_strlcpy(line, msg, cut + 1); /* the first cut characters */
    gfx_text(&font_ui, x + 68, y + 42, line, t->text_dim, 255);
    if (cut < len)
        gfx_text_fit(&font_ui, x + 68, y + 62, tw, msg + cut + 1, t->text_dim, 255);
}

/* ======================================================================== */
/* Activities and the desktop containment                                    */
/* ======================================================================== */

static struct activity *cur_act(void) { return &g_ws.activities[g_ws.activity]; }

static int overlaps(struct activity *a, int x, int y, int w, int h)
{
    for (int i = 0; i < MAX_WIDGETS; i++) {
        struct plasmoid *p = &a->widgets[i];
        if (p->used && x < p->x + p->w + 16 && p->x < x + w + 16 && y < p->y + p->h + 16 &&
            p->y < y + h + 16)
            return 1;
    }
    return 0;
}

/* Add a widget at the first free spot, scanning from the top right, like
 * Plasma filling the empty space of the desktop. */
void ws_place_widget(struct activity *a, int type)
{
    const struct plasmoid_type *pt = &g_plasmoid_types[type];
    struct plasmoid *p = 0;
    for (int i = 0; i < MAX_WIDGETS; i++)
        if (!a->widgets[i].used) {
            p = &a->widgets[i];
            a->focus = i;
            break;
        }
    if (!p) {
        svc_notify("SkarletOS", "This activity has no room for more widgets.");
        return;
    }
    k_memset(p, 0, sizeof *p);
    p->used = 1;
    p->type = type;
    p->w = pt->w;
    p->h = pt->h;
    p->x = 24;
    p->y = 24;
    for (int y = 24; y + pt->h <= DESK_BOTTOM; y += 16) {
        for (int x = g_w - pt->w - 24; x >= 16; x -= 16)
            if (!overlaps(a, x, y, pt->w, pt->h)) {
                p->x = x;
                p->y = y;
                goto placed;
            }
    }
placed:
    if (pt->init)
        pt->init(p);
}

static void new_activity(const char *name)
{
    if (g_ws.nactivities >= MAX_ACTIVITIES)
        return;
    struct activity *a = &g_ws.activities[g_ws.nactivities++];
    k_memset(a, 0, sizeof *a);
    k_strlcpy(a->name, name, sizeof a->name);
    a->focus = -1;
}

static void remove_activity(int idx)
{
    if (g_ws.nactivities <= 1 || idx < 0 || idx >= g_ws.nactivities)
        return;
    wm_activity_removed(idx);
    for (int i = idx; i < g_ws.nactivities - 1; i++)
        g_ws.activities[i] = g_ws.activities[i + 1];
    g_ws.nactivities--;
    if (g_ws.activity == idx)
        g_ws.activity = 0;
    else if (g_ws.activity > idx)
        g_ws.activity--;
    svc_notify("Activities", "Activity removed.");
}

/* When a widget has focus and widgets are unlocked, a small handle appears
 * beside it, as in Plasma's edit mode: remove (Delete) and move (Alt+arrows). */
static void draw_applet_handle(struct plasmoid *p)
{
    const struct theme *t = g_theme;
    int hx = p->x + p->w + 10;
    if (hx + 40 > g_w)
        hx = p->x - 50;
    gfx_rrect(hx, p->y, 40, 84, 12, t->card, t->card_a);
    gfx_rrect_line(hx, p->y, 40, 84, 12, t->border, 160);
    gfx_icon(IC_CLOSE, hx + 11, p->y + 12, 18, t->text, 255);
    gfx_icon(IC_GRID, hx + 11, p->y + 50, 18, t->text_dim, 255);
    int idx = (int)(p - cur_act()->widgets);
    ws_hit(hx, p->y, 40, 40, HIT_WIDGET_REMOVE, idx);
    ws_hit(hx, p->y + 40, 40, 44, HIT_WIDGET_MOVE, idx);
}

static void draw_widgets(void)
{
    const struct theme *t = g_theme;
    struct activity *a = cur_act();
    int desk_focus = !wm_focused() && popup == POP_NONE;
    for (int i = 0; i < MAX_WIDGETS; i++) {
        struct plasmoid *p = &a->widgets[i];
        if (!p->used)
            continue;
        const struct plasmoid_type *pt = &g_plasmoid_types[p->type];
        int focused = desk_focus && a->focus == i;
        ws_hit(p->x, p->y, p->w, p->h, HIT_WIDGET, i);
        gfx_shadow(p->x, p->y + 4, p->w, p->h, 18, 22, t->shadow_a * 2 / 3);
        if (pt->card)
            gfx_rrect(p->x, p->y, p->w, p->h, 18, pt->card, 250);
        else
            gfx_rrect(p->x, p->y, p->w, p->h, 18, t->card, t->card_a - 30);
        if (focused) {
            gfx_rrect_line(p->x, p->y, p->w, p->h, 18, t->accent_hi, 255);
            gfx_rrect_line(p->x + 1, p->y + 1, p->w - 2, p->h - 2, 17, t->accent_hi, 255);
        } else {
            gfx_rrect_line(p->x, p->y, p->w, p->h, 18, t->border, 120);
        }
        int cy = p->y + 18, ch = p->h - 36;
        if (pt->header) {
            /* Plasma widgets carry their title inside, at the top. */
            gfx_icon(pt->icon, p->x + 18, p->y + 16, 20, t->accent_hi, 255);
            gfx_text(&font_ui_bold, p->x + 46, p->y + 18, pt->name, t->text, 255);
            cy += 38;
            ch -= 38;
        }
        gfx_clip(p->x + 16, cy, p->w - 32, ch);
        pt->draw(p, p->x + 16, cy, p->w - 32, ch, focused);
        gfx_noclip();
        if (focused && !g_ws.locked)
            draw_applet_handle(p);
    }
}

static void widget_focus_next(int dir)
{
    struct activity *a = cur_act();
    for (int step = 1; step <= MAX_WIDGETS; step++) {
        int i = ((a->focus < 0 ? (dir > 0 ? -1 : 0) : a->focus) + dir * step + MAX_WIDGETS * 2) %
                MAX_WIDGETS;
        if (a->widgets[i].used) {
            a->focus = i;
            return;
        }
    }
    a->focus = -1;
}

static void desktop_key(struct key k)
{
    struct activity *a = cur_act();
    if (k.code == K_TAB) {
        widget_focus_next(k.mods & MOD_SHIFT ? -1 : 1);
        return;
    }
    if (a->focus < 0 || !a->widgets[a->focus].used)
        return;
    struct plasmoid *p = &a->widgets[a->focus];
    if (k.code == K_ESC) {
        a->focus = -1;
        return;
    }
    int moving = (k.mods & MOD_ALT) && k.code >= K_UP && k.code <= K_RIGHT;
    if ((moving || k.code == K_DELETE) && g_ws.locked) {
        svc_notify("SkarletOS", "Widgets are locked. Unlock them from the desktop menu (Alt+F12).");
        return;
    }
    if (k.code == K_DELETE) {
        char msg[48];
        k_snprintf(msg, sizeof msg, "Removed %s.", g_plasmoid_types[p->type].name);
        p->used = 0;
        a->focus = -1;
        svc_notify("SkarletOS", msg);
        return;
    }
    if (moving) {
        int dx = k.code == K_LEFT ? -16 : k.code == K_RIGHT ? 16 : 0;
        int dy = k.code == K_UP ? -16 : k.code == K_DOWN ? 16 : 0;
        p->x = MAX(0, MIN(p->x + dx, g_w - p->w));
        p->y = MAX(0, MIN(p->y + dy, DESK_BOTTOM - p->h));
        return;
    }
    const struct plasmoid_type *pt = &g_plasmoid_types[p->type];
    if (pt->key)
        pt->key(p, k);
}

/* ======================================================================== */
/* The panel: a floating containment holding applets in a row               */
/* ======================================================================== */

static const char *const day_names[] = { "Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat" };
static const char *const month_short[] = { "Jan", "Feb", "Mar", "Apr", "May", "Jun",
                                           "Jul", "Aug", "Sep", "Oct", "Nov", "Dec" };

static int weekday(int y, int m, int d)
{
    static const int t[] = { 0, 3, 2, 5, 0, 3, 5, 1, 4, 6, 2, 4 };
    if (m < 3)
        y -= 1;
    return (y + y / 4 - y / 100 + y / 400 + t[m - 1] + d) % 7;
}

void ws_format_time(char *out, int size, int seconds)
{
    int h = g_ws.now.hour;
    const char *suffix = "";
    if (!g_ws.clock24) {
        suffix = h < 12 ? " AM" : " PM";
        h %= 12;
        if (h == 0)
            h = 12;
    }
    if (seconds)
        k_snprintf(out, size, "%d:%02d:%02d%s", h, g_ws.now.minute, g_ws.now.second, suffix);
    else
        k_snprintf(out, size, "%d:%02d%s", h, g_ws.now.minute, suffix);
}

static void format_date(char *out, int size)
{
    int m = (g_ws.now.month >= 1 && g_ws.now.month <= 12) ? g_ws.now.month : 1;
    k_snprintf(out, size, "%s %d %s", day_names[weekday(g_ws.now.year, m, g_ws.now.day)],
               g_ws.now.day, month_short[m - 1]);
}

static void draw_panel(void)
{
    const struct theme *t = g_theme;
    int px = PANEL_MARGIN, py = PANEL_Y, pw = g_w - 2 * PANEL_MARGIN, ph = PANEL_H;
    add_layer(px, py, pw, ph, 14);
    ws_hit(px, py, pw, ph, HIT_PANEL, 0);
    gfx_shadow(px, py + 4, pw, ph, 14, 20, t->shadow_a / 2);
    gfx_rrect(px, py, pw, ph, 14, t->panel, t->panel_a);
    gfx_rrect_line(px, py, pw, ph, 14, t->border, 140);
    int cy = py + ph / 2;

    /* Launcher button: the SkarletOS logo on an accent circle. */
    int x = px + 8;
    if (popup == POP_LAUNCHER)
        gfx_rrect(x, py + 4, 40, 40, 10, t->hover, 255);
    gfx_circle(x + 20, cy, 15, t->accent, 255);
    gfx_icon(IC_LOGO, x + 9, cy - 11, 22, WHITE, 255);
    ws_hit(x, py, 44, ph, HIT_PANEL_LAUNCHER, 0);
    x += 52;

    /* Pager: one small box per virtual desktop. */
    for (int d = 0; d < 4; d++) {
        char num[2] = { (char)('1' + d), 0 };
        int bx = x + d * 30;
        ws_hit(bx, py + 6, 28, ph - 12, HIT_PANEL_DESK, d);
        if (d == g_ws.desk) {
            gfx_rrect(bx, cy - 11, 26, 22, 6, t->accent, 255);
            gfx_text_center(&font_small, bx, cy - 8, 26, num, WHITE, 255);
        } else {
            gfx_rrect(bx, cy - 11, 26, 22, 6, wm_count_on_desk(d) ? t->input : t->panel, 255);
            gfx_rrect_line(bx, cy - 11, 26, 22, 6, t->border, 255);
            gfx_text_center(&font_small, bx, cy - 8, 26, num, t->text_dim, 255);
        }
    }
    x += 4 * 30 + 12;
    gfx_rect(x, py + 12, 1, ph - 24, t->divider, 255);
    x += 12;

    /* Task manager: icon-only buttons with a running indicator underneath,
     * wider and in the accent colour for the active window.  Minimized
     * windows keep their button, drawn faded. */
    struct window *vis[MAX_WIN];
    int n = wm_list_desk(vis, MAX_WIN);
    struct window *focused = wm_focused();
    for (int i = 1; i < n; i++) /* stable order: by pid, not stacking order */
        for (int j = i; j > 0 && vis[j]->pid < vis[j - 1]->pid; j--) {
            struct window *tmp = vis[j];
            vis[j] = vis[j - 1];
            vis[j - 1] = tmp;
        }
    n = MIN(n, (px + pw - 560 - x) / 48); /* leave room for the tray and clock */
    for (int i = 0; i < n; i++) {
        int bx = x + i * 48;
        if (vis[i] == focused)
            gfx_rrect(bx, py + 4, 44, 40, 10, t->hover, 255);
        struct window *tw = vis[i];
        gfx_app_icon(tw->ext_icon ? tw->ext_icon : g_apps[tw->app].icon, bx + 8, py + 8, 28,
                     tw->ext_color ? tw->ext_color : g_apps[tw->app].color);
        if (vis[i]->minimized)
            gfx_rrect(bx + 8, py + 8, 28, 28, 7, t->panel, 140);
        ws_hit(bx, py + 4, 44, 40, HIT_PANEL_TASK, vis[i]->pid);
        if (vis[i] == focused)
            gfx_rrect(bx + 12, py + ph - 6, 20, 3, 1, t->accent_hi, 255);
        else
            gfx_rrect(bx + 19, py + ph - 6, 6, 3, 1, t->text_dim, 255);
    }

    /* Right side, from the edge inwards: show desktop, clock, system tray. */
    int rx = px + pw - 8;
    rx -= 36;
    ws_hit(rx, py + 6, 36, 36, HIT_PANEL_SHOWDESK, 0);
    if (g_ws.dashboard)
        gfx_rrect(rx, py + 6, 36, 36, 10, t->accent, 255);
    gfx_icon(IC_DESKTOP, rx + 8, cy - 10, 20, g_ws.dashboard ? WHITE : t->text_dim, 255);
    rx -= 12;
    gfx_rect(rx, py + 12, 1, ph - 24, t->divider, 255);
    char tbuf[16], dbuf[24];
    ws_format_time(tbuf, sizeof tbuf, g_ws.clock_seconds);
    format_date(dbuf, sizeof dbuf);
    int cw = MAX(gfx_text_width(&font_ui_bold, tbuf), gfx_text_width(&font_small, dbuf));
    rx -= 14 + cw;
    gfx_text_center(&font_ui_bold, rx, py + 6, cw, tbuf, t->text, 255);
    gfx_text_center(&font_small, rx, py + 26, cw, dbuf, t->text_dim, 255);
    rx -= 20;
    static const int tray[] = { IC_BELL, IC_VOLUME, IC_NETWORK };
    for (int i = 0; i < 3; i++) {
        rx -= 32;
        gfx_icon(tray[i], rx + 6, cy - 10, 20, t->text, 220);
        if (tray[i] == IC_BELL && toast_active())
            gfx_circle(rx + 25, cy - 9, 4, t->accent_hi, 255);
    }
}

/* ======================================================================== */
/* Skarlet Launcher (after Kickoff)                                          */
/* ======================================================================== */

/* The sections down the side; Kickoff's tabs in KDE 4 and Plasma 6. */
static const char *const launch_tabs[] = { "Favorites", "Applications", "Places",
                                           "Recently Used", "Power" };
static const int launch_tab_icons[] = { IC_STAR, IC_GRID, IC_HOME, IC_RECENT, IC_POWER };
/* Application categories, after the freedesktop.org menu specification
 * (its "Accessories" is "Utilities" here): System first, where SkarletOS's
 * own apps are, then the rest alphabetically.  Empty ones are not shown. */
static const char *const launch_cats[] = {
    "System", "Development", "Education", "Games", "Graphics", "Internet", "Multimedia",
    "Office", "Science", "Settings", "Utilities",
};

static int category_count(int c)
{
    int count = 0;
    for (int a = 0; a < APP_COUNT; a++)
        count += k_strcmp(g_apps[a].category, launch_cats[c]) == 0;
    for (int i = 0; i < plat_app_count(); i++)
        count += k_strcmp(plat_app(i)->category, launch_cats[c]) == 0;
    return count;
}
static const struct { const char *name, *path; int icon; } launch_places[] = {
    { "Home", "/home/user", IC_HOME }, { "Desktop", "/home/user/Desktop", IC_DESKTOP },
    { "Documents", "/home/user/Documents", IC_FILE }, { "Music", "/home/user/Music", IC_MUSIC },
    { "Root", "/", IC_DRIVE }, { "Temp", "/tmp", IC_RECENT },
};

/* Built-in apps have ids below INSTALLED; installed programs are
 * INSTALLED + their index in plat_app(). */
#define INSTALLED 1000

static void add_app_item(int app)
{
    if (app >= INSTALLED) {
        const struct installed_app *ia = plat_app(app - INSTALLED);
        if (ia)
            set_icon(add_item(ia->name, ia->comment, ACT_INSTALLED, app - INSTALLED, 0), ia->icon,
                     ia->color);
        return;
    }
    set_icon(add_item(g_apps[app].name, g_apps[app].generic, ACT_LAUNCH, app, 0), g_apps[app].icon,
             g_apps[app].color);
}

static int installed_matches(const struct installed_app *ia, const char *q)
{
    return k_strcasestr(ia->name, q) || k_strcasestr(ia->comment, q) ||
           k_strcasestr(ia->category, q);
}

static void add_place_item(int i)
{
    set_icon(add_item(launch_places[i].name, launch_places[i].path, ACT_PLACE, 0,
                      launch_places[i].path),
             launch_places[i].icon, neutral_tint());
}

static void add_leave_items(const char *filter)
{
    static const struct { const char *label, *hint, *section; int act, icon; } leave[] = {
        { "Log out", "End the session", "Session", ACT_LOGOUT, IC_LOGOUT },
        { "Restart", "Restart the computer", "System", ACT_REBOOT, IC_RESTART },
        { "Shut down", "Turn off the computer", "System", ACT_POWEROFF, IC_POWER },
    };
    const char *section = 0;
    for (int i = 0; i < ARRAY_LEN(leave); i++) {
        if (filter && !k_strcasestr(leave[i].label, filter))
            continue;
        if (!filter && (!section || k_strcmp(section, leave[i].section) != 0)) {
            section = leave[i].section;
            add_header(section);
        }
        set_icon(add_item(leave[i].label, leave[i].hint, leave[i].act, 0, 0), leave[i].icon,
                 g_theme->accent);
    }
}

static void launcher_build(void)
{
    nitems = 0;
    if (qlen > 0) {
        int h = nitems;
        add_header("Applications");
        for (int a = 0; a < APP_COUNT; a++)
            if (k_strcasestr(g_apps[a].name, query) || k_strcasestr(g_apps[a].generic, query) ||
                k_strcasestr(g_apps[a].id, query))
                add_app_item(a);
        for (int i = 0; i < plat_app_count(); i++)
            if (installed_matches(plat_app(i), query))
                add_app_item(INSTALLED + i);
        end_section(h);
        h = nitems;
        add_header("Places");
        for (int i = 0; i < ARRAY_LEN(launch_places); i++)
            if (k_strcasestr(launch_places[i].name, query))
                add_place_item(i);
        end_section(h);
        h = nitems;
        add_header("Power");
        add_leave_items(query);
        end_section(h);
    } else if (launch_tab == 0) {
        for (int i = 0; i < plat_app_count(); i++)
            if (plat_app(i)->favorite)
                add_app_item(INSTALLED + i);
        add_app_item(APP_TERMINAL);
        add_app_item(APP_FILES);
        add_app_item(APP_WRITE);
        add_app_item(APP_SETTINGS);
    } else if (launch_tab == 1) {
        if (launch_cat < 0) {
            /* Top level: the categories, which open like folders. */
            for (int c = 0; c < ARRAY_LEN(launch_cats); c++) {
                int count = category_count(c);
                if (count == 0)
                    continue;
                char hint[32];
                k_snprintf(hint, sizeof hint, "%d application%s", count, count == 1 ? "" : "s");
                set_icon(add_item(launch_cats[c], hint, ACT_CATEGORY, c, 0), IC_GRID,
                         neutral_tint());
            }
        } else {
            for (int a = 0; a < APP_COUNT; a++)
                if (k_strcmp(g_apps[a].category, launch_cats[launch_cat]) == 0)
                    add_app_item(a);
            for (int i = 0; i < plat_app_count(); i++)
                if (k_strcmp(plat_app(i)->category, launch_cats[launch_cat]) == 0)
                    add_app_item(INSTALLED + i);
        }
    } else if (launch_tab == 2) {
        add_header("Applications");
        add_app_item(APP_SETTINGS);
        add_app_item(APP_MONITOR);
        add_header("Places");
        for (int i = 0; i < ARRAY_LEN(launch_places); i++)
            add_place_item(i);
    } else if (launch_tab == 3) {
        int h = nitems;
        add_header("Applications");
        for (int i = 0; i < nrecent; i++)
            add_app_item(recent[i]);
        end_section(h);
        h = nitems;
        add_header("Documents");
        for (int i = 0; i < nrecent_docs; i++) {
            const char *name = recent_docs[i];
            for (const char *p = recent_docs[i]; *p; p++)
                if (*p == '/')
                    name = p + 1;
            set_icon(add_item(name, recent_docs[i], ACT_OPENFILE, 0, recent_docs[i]), IC_FILE,
                     g_apps[APP_WRITE].color);
        }
        end_section(h);
    } else {
        add_leave_items(0);
    }
    sel_fix();
}

#define LAUNCH_W 760
#define LAUNCH_H 580
#define LAUNCH_ROW 54

static void draw_launcher(void)
{
    const struct theme *t = g_theme;
    int w = MIN(LAUNCH_W, g_w - 16), h = MIN(LAUNCH_H, PANEL_Y - 24);
    int x = PANEL_MARGIN, y = PANEL_Y - 10 - h;
    card(x, y, w, h, 18);

    /* Header: the user on the left, the search field on the right. */
    gfx_circle(x + 44, y + 38, 22, t->accent, 255);
    gfx_icon(IC_USER, x + 30, y + 24, 28, WHITE, 255);
    gfx_text(&font_ui_bold, x + 78, y + 20, "user", t->text, 255);
    gfx_text(&font_small, x + 78, y + 40, "on skarlet", t->text_dim, 255);
    int sw = 340, sx = x + w - sw - 24;
    gfx_rrect(sx, y + 20, sw, 38, 19, t->input, 255);
    gfx_icon(IC_SEARCH, sx + 12, y + 29, 20, t->text_dim, 255);
    if (qlen) {
        int tw = gfx_text(&font_ui, sx + 42, y + 30, query, t->text, 255);
        gfx_rect(sx + 43 + tw, y + 29, 2, 20, t->accent_hi, 255);
    } else {
        gfx_rect(sx + 42, y + 29, 2, 20, t->accent_hi, 255);
        gfx_text(&font_ui, sx + 50, y + 30, "Search:", t->text_dim, 255);
    }
    gfx_rect(x + 16, y + 76, w - 32, 1, t->divider, 255);

    /* Sidebar: the sections. */
    int sbw = 210;
    for (int i = 0; i < ARRAY_LEN(launch_tabs); i++) {
        int ry = y + 92 + i * 44, on = i == launch_tab && qlen == 0;
        if (on)
            gfx_rrect(x + 12, ry, sbw - 20, 38, 10, t->accent, 255);
        ws_hit(x + 12, ry, sbw - 20, 38, HIT_TAB, i);
        gfx_icon(launch_tab_icons[i], x + 26, ry + 9, 20, on ? WHITE : t->accent_hi, 255);
        gfx_text(&font_ui, x + 58, ry + 10, launch_tabs[i], on ? WHITE : t->text, 255);
    }
    gfx_rect(x + sbw, y + 92, 1, h - 92 - 56, t->divider, 255);

    /* Content: section headers and two-line entries; scroll to the selection. */
    int cx = x + sbw + 14, cw = w - sbw - 30, top = y + 88, avail = h - 88 - 60;
    if (qlen == 0 && launch_tab == 1 && launch_cat >= 0) {
        gfx_icon(IC_CHEVRON_L, cx + 6, top + 8, 16, t->accent_hi, 255);
        char crumb[48];
        k_snprintf(crumb, sizeof crumb, "All Applications  /  %s", launch_cats[launch_cat]);
        gfx_text(&font_ui_bold, cx + 28, top + 7, crumb, t->text, 255);
        ws_hit(cx, top, cw, 32, HIT_CRUMB, 0);
        top += 36;
        avail -= 36;
    }
    if (nitems == 0)
        gfx_text(&font_ui, cx + 16, top + 16, "Nothing here yet.", t->text_dim, 255);
    if (launch_top > sel)
        launch_top = sel;
    for (;;) {
        int used = 0;
        for (int i = launch_top; i <= sel && i < nitems; i++)
            used += items[i].header ? 32 : LAUNCH_ROW;
        if (used <= avail || launch_top >= sel)
            break;
        launch_top++;
    }
    int ry = top;
    for (int i = launch_top; i < nitems; i++) {
        struct item *it = &items[i];
        int need = it->header ? 32 : LAUNCH_ROW;
        if (ry + need > top + avail)
            break;
        if (it->header) {
            gfx_text(&font_small, cx + 12, ry + 12, it->label, t->text_dim, 255);
        } else {
            int on = i == sel;
            ws_hit(cx, ry + 2, cw, LAUNCH_ROW - 4, HIT_ITEM, i);
            if (on)
                gfx_rrect(cx, ry + 2, cw, LAUNCH_ROW - 4, 12, t->accent, 255);
            item_icon(it, cx + 10, ry + 9, 36, on);
            gfx_text_fit(&font_ui_bold, cx + 58, ry + 9, cw - 70, it->label,
                         on ? WHITE : t->text, 255);
            gfx_text_fit(&font_small, cx + 58, ry + 29, cw - 70, it->hint,
                         on ? gfx_mix(t->accent, WHITE, 210) : t->text_dim, 255);
        }
        ry += need;
    }

    /* Footer. */
    gfx_rect(x + 16, y + h - 52, w - 32, 1, t->divider, 255);
    gfx_text(&font_small, x + 24, y + h - 33,
             "Type to search    Up/Down: choose    Left/Right: section    Enter: open    Esc: close",
             t->text_dim, 255);
}

/* ======================================================================== */
/* Skarlet Runner (after KRunner)                                            */
/* ======================================================================== */

static void runner_build(void)
{
    nitems = 0;
    if (qlen == 0) {
        sel = 0;
        return;
    }
    /* Calculator runner: "=6*7" or anything that evaluates, like "6*7". */
    const char *expr = query[0] == '=' ? query + 1 : query;
    int32_t v;
    if (k_eval(expr, &v) == 0 && (query[0] == '=' || k_strchr(expr, '+') || k_strchr(expr, '-') ||
                                  k_strchr(expr, '*') || k_strchr(expr, '/') || k_strchr(expr, '%'))) {
        char label[40], result[16];
        k_snprintf(result, sizeof result, "%d", v);
        k_snprintf(label, sizeof label, "= %s", result);
        set_icon(add_item(label, "Calculator", ACT_CALC, 0, result), IC_CALC, g_theme->accent);
    }
    /* Application runner. */
    for (int a = 0; a < APP_COUNT; a++)
        if (k_strcasestr(g_apps[a].name, query) || k_strcasestr(g_apps[a].id, query) ||
            k_strcasestr(g_apps[a].generic, query))
            set_icon(add_item(g_apps[a].name, "Launch application", ACT_LAUNCH, a, 0),
                     g_apps[a].icon, g_apps[a].color);
    for (int i = 0; i < plat_app_count() && nitems < MAX_ITEMS - 3; i++)
        if (installed_matches(plat_app(i), query))
            set_icon(add_item(plat_app(i)->name, "Launch application", ACT_INSTALLED, i, 0),
                     plat_app(i)->icon, plat_app(i)->color);
    /* Places runner: anything that looks like a path. */
    int node = vfs_lookup(VFS_ROOT, query);
    if ((query[0] == '/' || query[0] == '~') && node >= 0) {
        char path[VFS_PATH_MAX], label[40];
        vfs_path(node, path, sizeof path);
        k_snprintf(label, sizeof label, "Open %s", path);
        set_icon(add_item(label, vfs_is_dir(node) ? "Skarlet Files" : "Skarlet Write",
                          vfs_is_dir(node) ? ACT_PLACE : ACT_OPENFILE, 0, path),
                 vfs_is_dir(node) ? IC_FOLDER : IC_FILE,
                 vfs_is_dir(node) ? g_apps[APP_FILES].color : g_apps[APP_WRITE].color);
    }
    /* Command line runner: run it in a terminal. */
    char label[48];
    k_snprintf(label, sizeof label, "Run \"%s\"", query);
    set_icon(add_item(label, "Command line (Skarlet Terminal)", ACT_RUN, 0, query), IC_TERMINAL,
             g_apps[APP_TERMINAL].color);
    if (sel >= nitems)
        sel = MAX(nitems - 1, 0);
}

static void draw_runner(void)
{
    const struct theme *t = g_theme;
    int w = MIN(720, g_w - 32), x = (g_w - w) / 2, y = g_h / 7;
    int rows = MIN(nitems, 6);
    int h = 64 + (rows ? 12 + rows * 52 : 34);
    card(x, y, w, h, 18);
    gfx_icon(IC_SEARCH, x + 20, y + 18, 28, t->accent_hi, 255);
    if (qlen) {
        int tw = gfx_text(&font_title, x + 64, y + 18, query, t->text, 255);
        gfx_rect(x + 66 + tw, y + 18, 2, 28, t->accent_hi, 255);
    } else {
        gfx_rect(x + 64, y + 18, 2, 28, t->accent_hi, 255);
        gfx_text(&font_title, x + 72, y + 18, "Search", t->text_dim, 160);
    }
    gfx_icon(IC_CLOSE, x + w - 40, y + 22, 18, t->text_dim, 255);
    ws_hit(x + w - 52, y + 10, 42, 42, HIT_CLOSE_POPUP, 0);
    if (!rows) {
        gfx_text(&font_small, x + 64, y + 62, "Apps, places like /etc, maths like 6*7, commands",
                 t->text_dim, 255);
        return;
    }
    gfx_rect(x + 16, y + 64, w - 32, 1, t->divider, 255);
    for (int r = 0; r < rows; r++) {
        int idx = r + (sel >= rows ? sel - rows + 1 : 0);
        struct item *it = &items[idx];
        int on = it == &items[sel], ry = y + 72 + r * 52;
        ws_hit(x + 10, ry, w - 20, 48, HIT_ITEM, idx);
        if (on)
            gfx_rrect(x + 10, ry, w - 20, 48, 12, t->accent, 255);
        item_icon(it, x + 22, ry + 8, 32, on);
        gfx_text_fit(&font_ui_bold, x + 68, ry + 14, w - 340, it->label, on ? WHITE : t->text,
                     255);
        gfx_text_right(&font_small, x + w - 28, ry + 16, it->hint,
                       on ? gfx_mix(t->accent, WHITE, 210) : t->text_dim, 255);
    }
}

/* ======================================================================== */
/* Desktop menu (Alt+F12)                                                    */
/* ======================================================================== */

static void open_toolbox(void)
{
    struct activity *a = cur_act();
    nitems = 0;
    struct item *it = add_item("Add Widgets...", "", ACT_STRIP_WIDGETS, 0, 0);
    set_icon(it, IC_PLUS, 0);
    if (it)
        it->disabled = g_ws.locked;
    set_icon(add_item("Activities...", "", ACT_STRIP_ACTIVITIES, 0, 0), IC_ACTIVITY, 0);
    if (!g_ws.locked && a->focus >= 0 && a->widgets[a->focus].used) {
        char label[40];
        k_snprintf(label, sizeof label, "Remove %s",
                   g_plasmoid_types[a->widgets[a->focus].type].name);
        set_icon(add_item(label, "", ACT_REMOVEW, a->focus, 0), IC_CLOSE, 0);
    }
    set_icon(add_item(g_ws.locked ? "Unlock Widgets" : "Lock Widgets", "", ACT_TOGGLE_LOCK, 0, 0),
             IC_LOCK, 0);
    set_icon(add_item("Desktop Settings", "", ACT_LAUNCH, APP_SETTINGS, 0), IC_SETTINGS, 0);
    set_icon(add_item("Keyboard Shortcuts", "", ACT_HELP, 0, 0), IC_KEYBOARD, 0);
    popup = POP_MENU;
    sel = 0;
}

static void draw_menu(void)
{
    const struct theme *t = g_theme;
    int w = 300, h = 52 + nitems * 42 + 8, x = g_w - w - 16, y = 16;
    card(x, y, w, h, 16);
    gfx_text(&font_small, x + 20, y + 18, "Desktop", t->text_dim, 255);
    for (int i = 0; i < nitems; i++) {
        struct item *it = &items[i];
        int ry = y + 44 + i * 42, on = i == sel;
        ws_hit(x + 8, ry, w - 16, 38, HIT_ITEM, i);
        if (on)
            gfx_rrect(x + 8, ry, w - 16, 38, 10, t->accent, 255);
        uint32_t c = on ? WHITE : it->disabled ? t->text_dim : t->text;
        gfx_icon(it->icon, x + 22, ry + 9, 20, on ? WHITE : t->accent_hi, it->disabled ? 120 : 255);
        gfx_text(&font_ui, x + 54, ry + 10, it->label, c, it->disabled && !on ? 150 : 255);
    }
}

/* ======================================================================== */
/* The Add Widgets and Activities sheets, above the panel                    */
/* ======================================================================== */

#define TILE_W 168
#define TILE_H 132

static void strip_build(void)
{
    nitems = 0;
    struct item *it;
    if (strip_kind == STRIP_WIDGETS) {
        for (int i = 0; i < PL_COUNT; i++) {
            const struct plasmoid_type *pt = &g_plasmoid_types[i];
            if (qlen && !k_strcasestr(pt->name, query) && !k_strcasestr(pt->desc, query))
                continue;
            set_icon(add_item(pt->name, pt->desc, ACT_ADDW, i, 0), pt->icon, g_theme->accent);
        }
    } else {
        for (int i = 0; i < g_ws.nactivities; i++)
            set_icon(add_item(g_ws.activities[i].name,
                              i == g_ws.activity ? "The current activity" : "Switch to this activity",
                              ACT_SWITCH_ACT, i, 0),
                     IC_ACTIVITY, i == g_ws.activity ? g_theme->accent : neutral_tint());
        it = add_item("New Activity", "Create an activity with a Folder View", ACT_NEW_ACT, 0, 0);
        set_icon(it, IC_PLUS, neutral_tint());
        if (it)
            it->disabled = g_ws.nactivities >= MAX_ACTIVITIES;
    }
    if (sel >= nitems)
        sel = MAX(nitems - 1, 0);
}

static void open_strip(int kind)
{
    popup = POP_STRIP;
    strip_kind = kind;
    qlen = 0;
    query[0] = 0;
    sel = kind == STRIP_ACTIVITIES ? g_ws.activity : 0;
    strip_build();
}

static void draw_strip(void)
{
    const struct theme *t = g_theme;
    int x = PANEL_MARGIN, w = g_w - 2 * PANEL_MARGIN, h = SHEET_H, y = PANEL_Y - 12 - h;
    card(x, y, w, h, 18);
    int widgets = strip_kind == STRIP_WIDGETS;
    int tw = gfx_text(&font_title, x + 24, y + 18, widgets ? "Add Widgets" : "Activities", t->text,
                      255);
    if (widgets) {
        int sx = x + 24 + tw + 28;
        gfx_rrect(sx, y + 16, 280, 34, 17, t->input, 255);
        gfx_icon(IC_SEARCH, sx + 10, y + 23, 18, t->text_dim, 255);
        if (qlen)
            gfx_text(&font_ui, sx + 36, y + 24, query, t->text, 255);
        else
            gfx_text(&font_ui, sx + 36, y + 24, "Search:", t->text_dim, 255);
    }
    gfx_icon(IC_CLOSE, x + w - 40, y + 22, 18, t->text_dim, 255);
    ws_hit(x + w - 52, y + 10, 42, 42, HIT_CLOSE_POPUP, 0);
    if (nitems > 0)
        gfx_text_fit(&font_ui, x + 24, y + 58, w - 48, items[sel].hint, t->text_dim, 255);
    else
        gfx_text(&font_ui, x + 24, y + 58, "No widgets match the search.", t->text_dim, 255);

    int visible = (w - 48 + 16) / (TILE_W + 16);
    int first = sel >= visible ? sel - visible + 1 : 0;
    for (int i = 0; i < visible && first + i < nitems; i++) {
        struct item *it = &items[first + i];
        int on = first + i == sel, tx = x + 24 + i * (TILE_W + 16), ty = y + 86;
        ws_hit(tx, ty, TILE_W, TILE_H, HIT_ITEM, first + i);
        gfx_rrect(tx, ty, TILE_W, TILE_H, 14, on ? gfx_mix(t->input, t->accent, 70) : t->input, 255);
        if (on) {
            gfx_rrect_line(tx, ty, TILE_W, TILE_H, 14, t->accent_hi, 255);
            gfx_rrect_line(tx + 1, ty + 1, TILE_W - 2, TILE_H - 2, 13, t->accent_hi, 255);
        }
        item_icon(it, tx + (TILE_W - 56) / 2, ty + 18, 56, on);
        gfx_text_center(&font_ui_bold, tx + 8, ty + 90, TILE_W - 16, it->label,
                        it->disabled ? t->text_dim : t->text, 255);
    }
    if (first + visible < nitems)
        gfx_icon(IC_CHEVRON_R, x + w - 30, y + 140, 20, t->text_dim, 255);
    if (first > 0)
        gfx_icon(IC_CHEVRON_L, x + 6, y + 140, 20, t->text_dim, 255);
    gfx_text(&font_small, x + 24, y + h - 26,
             widgets ? "Left/Right: choose    Enter: add    type to search    Esc: close"
                     : "Left/Right: choose    Enter: switch    Delete: remove    Esc: close",
             t->text_dim, 255);
}

static void strip_key(struct key k)
{
    if (k.code == K_ESC) {
        close_popup();
    } else if (k.code == K_ENTER) {
        if (sel < nitems)
            run_item(&items[sel]);
    } else if (k.code == K_LEFT || k.code == K_UP) {
        if (sel > 0)
            sel--;
    } else if (k.code == K_RIGHT || k.code == K_DOWN) {
        if (sel < nitems - 1)
            sel++;
    } else if (k.code == K_DELETE && strip_kind == STRIP_ACTIVITIES) {
        if (sel < nitems && items[sel].act == ACT_SWITCH_ACT) {
            if (g_ws.nactivities > 1)
                remove_activity(items[sel].arg);
            else
                svc_notify("Activities", "The last activity cannot be removed.");
            sel = MIN(sel, g_ws.nactivities - 1); /* stay on an activity */
            strip_build();
        }
    } else if (strip_kind == STRIP_WIDGETS && k.code == K_BACKSPACE) {
        if (qlen > 0)
            query[--qlen] = 0;
        sel = 0;
        strip_build();
    } else if (strip_kind == STRIP_WIDGETS && k.code >= 32 && k.code < 127 &&
               !(k.mods & (MOD_CTRL | MOD_ALT)) && qlen < (int)sizeof query - 1) {
        query[qlen++] = (char)k.code;
        query[qlen] = 0;
        sel = 0;
        strip_build();
    }
}

/* ======================================================================== */
/* Running actions                                                           */
/* ======================================================================== */

static void logout(void)
{
    close_popup();
    wm_close_all();
    g_ws.phase = PHASE_LOGIN;
    g_ws.dashboard = 0;
    g_ws.move_mode = 0;
    login_pw[0] = 0;
}

void svc_reboot(void)
{
    close_popup();
    g_ws.phase = PHASE_OFF;
    g_ws.off_reboot = 1;
    ws_draw();
    plat_present(g_px, g_w, g_h);
    plat_reboot();
}

void svc_poweroff(void)
{
    close_popup();
    g_ws.phase = PHASE_OFF;
    g_ws.off_reboot = 0;
    ws_draw();
    plat_present(g_px, g_w, g_h);
    plat_poweroff();
}

static void run_item(struct item *it)
{
    if (it->disabled || it->header)
        return;
    int act = it->act, arg = it->arg;
    char str[VFS_PATH_MAX];
    k_strlcpy(str, it->str, sizeof str);
    close_popup();

    switch (act) {
    case ACT_LAUNCH:
        remember_recent(arg);
        svc_launch(arg, 0);
        break;
    case ACT_INSTALLED:
        remember_recent(INSTALLED + arg);
        plat_app_start(arg);
        break;
    case ACT_PLACE: svc_launch(APP_FILES, str); break;
    case ACT_OPENFILE: svc_launch(APP_WRITE, str); break;
    case ACT_RUN: svc_launch(APP_TERMINAL, str); break;
    case ACT_CALC:
        /* Put the result back into the runner so it can be used further. */
        popup = POP_RUNNER;
        k_strlcpy(query, str, sizeof query);
        qlen = k_strlen(query);
        sel = 0;
        runner_build();
        break;
    case ACT_LOGOUT: logout(); break;
    case ACT_REBOOT: svc_reboot(); break;
    case ACT_POWEROFF: svc_poweroff(); break;
    case ACT_STRIP_WIDGETS: open_strip(STRIP_WIDGETS); break;
    case ACT_ADDW: {
        ws_place_widget(cur_act(), arg);
        char msg[64];
        k_snprintf(msg, sizeof msg, "Added %s to \"%s\".", g_plasmoid_types[arg].name,
                   cur_act()->name);
        svc_notify("SkarletOS", msg);
        break;
    }
    case ACT_REMOVEW:
        cur_act()->widgets[arg].used = 0;
        cur_act()->focus = -1;
        break;
    case ACT_STRIP_ACTIVITIES: open_strip(STRIP_ACTIVITIES); break;
    case ACT_SWITCH_ACT:
        g_ws.activity = arg;
        svc_notify("Activities", g_ws.activities[arg].name);
        break;
    case ACT_NEW_ACT: {
        char name[20];
        k_snprintf(name, sizeof name, "Activity %d", g_ws.nactivities + 1);
        new_activity(name);
        g_ws.activity = g_ws.nactivities - 1;
        ws_place_widget(cur_act(), PL_FOLDERVIEW);
        svc_notify("Activities", "Created a new activity with a Folder View.");
        break;
    }
    case ACT_TOGGLE_LOCK:
        g_ws.locked ^= 1;
        svc_notify("SkarletOS", g_ws.locked ? "Widgets locked." : "Widgets unlocked.");
        break;
    case ACT_HELP: svc_launch(APP_WRITE, "/home/user/Desktop/README.txt"); break;
    }
}

/* ======================================================================== */
/* Key routing                                                               */
/* ======================================================================== */

/* Enter (or a click) on the selected entry of the launcher, runner or menu. */
static void activate_sel(void)
{
    if (sel >= nitems)
        return;
    if (popup == POP_LAUNCHER && items[sel].act == ACT_CATEGORY) {
        launch_cat = items[sel].arg;
        launch_top = 0;
        launcher_build();
        sel_first();
        return;
    }
    run_item(&items[sel]);
}

static void popup_key(struct key k)
{
    if (popup == POP_STRIP) {
        strip_key(k);
        return;
    }
    if (k.code == K_ESC) {
        close_popup();
        return;
    }
    if (k.code == K_UP || k.code == K_DOWN) {
        sel_move(k.code == K_UP ? -1 : 1);
        return;
    }
    if (k.code == K_ENTER) {
        activate_sel();
        return;
    }
    if (popup == POP_MENU)
        return;

    if (popup == POP_LAUNCHER && qlen == 0 && (k.code == K_LEFT || k.code == K_RIGHT)) {
        int n = ARRAY_LEN(launch_tabs);
        launch_tab = (launch_tab + (k.code == K_RIGHT ? 1 : n - 1)) % n;
        launch_cat = -1;
        launch_top = 0;
        sel = 0;
    } else if (k.code == K_BACKSPACE) {
        if (qlen > 0)
            query[--qlen] = 0;
        else if (popup == POP_LAUNCHER && launch_cat >= 0)
            launch_cat = -1; /* back up out of a category */
        sel = 0;
        launch_top = 0;
    } else if (k.code >= 32 && k.code < 127 && !(k.mods & (MOD_CTRL | MOD_ALT)) &&
               qlen < (int)sizeof query - 1) {
        query[qlen++] = (char)k.code;
        query[qlen] = 0;
        sel = 0;
        launch_top = 0;
    }
    if (popup == POP_LAUNCHER)
        launcher_build();
    else
        runner_build();
}

static void open_popup(int which)
{
    int was = popup;
    close_popup();
    if (was == which)
        return; /* the shortcut toggles */
    qlen = 0;
    query[0] = 0;
    sel = 0;
    launch_tab = 0; /* always open on Favorites */
    launch_cat = -1;
    launch_top = 0;
    popup = which;
    if (which == POP_LAUNCHER)
        launcher_build();
    else if (which == POP_RUNNER)
        runner_build();
}

static void log_in(void)
{
    if (!plat_login(login_pw)) {
        login_failed = 1;
        login_pw[0] = 0;
        return;
    }
    login_failed = 0;
    k_memset(login_pw, 0, sizeof login_pw);
    g_ws.phase = PHASE_DESKTOP;
    svc_notify("Welcome", "Alt+F1 opens the launcher, Alt+F2 Skarlet Runner.");
}

static int global_shortcut(struct key k)
{
    int alt = k.mods & MOD_ALT, ctrl = k.mods & MOD_CTRL;
    if ((alt && k.code == K_F1) || (k.code == K_META && !k.mods)) {
        open_popup(POP_LAUNCHER);
    } else if (alt && k.code == K_F2) {
        open_popup(POP_RUNNER);
    } else if (alt && k.code == K_F12) {
        if (popup == POP_MENU || popup == POP_STRIP)
            close_popup();
        else
            open_toolbox();
    } else if (ctrl && k.code == K_ESC) {
        close_popup();
        svc_launch(APP_MONITOR, 0);
    } else if (ctrl && k.code >= K_F1 && k.code < K_F1 + NUM_DESKS) {
        close_popup();
        g_ws.desk = k.code - K_F1;
        g_ws.move_mode = 0;
    } else if (ctrl && k.code == K_F12) {
        close_popup();
        g_ws.dashboard ^= 1;
    } else if (alt && k.code == K_TAB) {
        close_popup();
        g_ws.move_mode = 0;
        wm_cycle();
    } else if (alt && k.code == K_F4) {
        if (popup != POP_NONE) {
            close_popup();
        } else if (wm_focused()) {
            wm_close(wm_focused());
        }
    } else if (alt && k.code == K_F7) {
        close_popup();
        if (wm_focused())
            g_ws.move_mode ^= 1;
    } else {
        return 0;
    }
    return 1;
}

void ws_key(struct key k)
{
    g_ws.need_redraw = 1;
    if (g_ws.phase == PHASE_OFF)
        return;
    if (g_ws.phase == PHASE_LOGIN) {
        int len = k_strlen(login_pw);
        if (k.code == K_ENTER) {
            log_in();
        } else if (k.code == K_BACKSPACE && len > 0) {
            login_pw[len - 1] = 0;
        } else if (k.code >= 32 && k.code < 127 && len < (int)sizeof login_pw - 1) {
            login_failed = 0;
            login_pw[len] = (char)k.code;
            login_pw[len + 1] = 0;
        }
        return;
    }

    if (global_shortcut(k))
        return;
    if (popup != POP_NONE) {
        popup_key(k);
        return;
    }
    struct window *w = wm_focused();
    if (g_ws.move_mode && w) {
        if (k.code == K_LEFT)
            w->x = MAX(w->x - 24, -w->w + 120);
        else if (k.code == K_RIGHT)
            w->x = MIN(w->x + 24, g_w - 120);
        else if (k.code == K_UP)
            w->y = MAX(w->y - 24, 0);
        else if (k.code == K_DOWN)
            w->y = MIN(w->y + 24, DESK_BOTTOM - TITLE_H);
        else if (k.code == K_ENTER || k.code == K_ESC)
            g_ws.move_mode = 0;
        return;
    }
    if (w)
        g_app_impl[w->app]->key(w, k);
    else
        desktop_key(k);
}

/* ======================================================================== */
/* The mouse                                                                 */
/* ======================================================================== */

/* What a held button is doing: moving or resizing a window, moving a widget. */
static struct {
    int kind; /* HIT_WIN_TITLE, HIT_WIN_RESIZE, HIT_WIDGET_MOVE or 0 */
    int arg;  /* window pid or widget index */
    int ox, oy, ow, oh, px, py;
} drag;

static int is_popup_hit(int kind)
{
    return kind == HIT_SWALLOW || kind == HIT_ITEM || kind == HIT_TAB || kind == HIT_CRUMB ||
           kind == HIT_CLOSE_POPUP;
}

static void drag_move(struct mouse m)
{
    int dx = m.x - drag.px, dy = m.y - drag.py;
    if (drag.kind == HIT_WIDGET_MOVE) {
        struct plasmoid *p = &cur_act()->widgets[drag.arg];
        p->x = MAX(0, MIN(drag.ox + dx, g_w - p->w));
        p->y = MAX(0, MIN(drag.oy + dy, DESK_BOTTOM - p->h));
        return;
    }
    struct window *w = wm_by_pid(drag.arg);
    if (!w) {
        drag.kind = 0;
        return;
    }
    if (drag.kind == HIT_WIN_TITLE) {
        if (w->maximized) {
            /* Dragging a maximized window restores its size under the pointer. */
            wm_toggle_maximize(w);
            drag.ox = m.x - w->w / 2;
            drag.oy = 0;
            drag.px = m.x, drag.py = m.y;
            dx = dy = 0;
        }
        w->x = MAX(-w->w + 120, MIN(drag.ox + dx, g_w - 120));
        w->y = MAX(0, MIN(drag.oy + dy, DESK_BOTTOM - TITLE_H));
    } else if (drag.kind == HIT_WIN_RESIZE) {
        w->w = MAX(240, MIN(drag.ow + dx, g_w - w->x));
        w->h = MAX(TITLE_H + 120, MIN(drag.oh + dy, DESK_BOTTOM - w->y));
    }
}

static void start_drag(int kind, int arg, int ox, int oy, int ow, int oh, struct mouse m)
{
    drag.kind = kind, drag.arg = arg;
    drag.ox = ox, drag.oy = oy, drag.ow = ow, drag.oh = oh;
    drag.px = m.x, drag.py = m.y;
}

static void window_click(int kind, struct window *w, struct mouse m)
{
    if (!w)
        return;
    wm_activate(w);
    if (kind == HIT_WIN_CLOSE) {
        wm_close(w);
    } else if (kind == HIT_WIN_MAX) {
        wm_toggle_maximize(w);
    } else if (kind == HIT_WIN_MIN) {
        wm_minimize(w);
    } else if (kind == HIT_WIN_TITLE) {
        if (m.clicks >= 2)
            wm_toggle_maximize(w);
        else
            start_drag(kind, w->pid, w->x, w->y, w->w, w->h, m);
    } else if (kind == HIT_WIN_RESIZE) {
        start_drag(kind, w->pid, w->x, w->y, w->w, w->h, m);
    } else if (kind == HIT_WIN_CLIENT) {
        int cx, cy, cw, ch;
        wm_client_rect(w, &cx, &cy, &cw, &ch);
        const struct app *impl = g_app_impl[w->app];
        if (impl->mouse && m.x >= cx && m.y >= cy && m.x < cx + cw && m.y < cy + ch) {
            struct mouse rel = m;
            rel.x -= cx, rel.y -= cy;
            impl->mouse(w, rel, cw, ch);
        }
    }
}

static void popup_click(int kind, int arg, struct mouse m)
{
    if (kind == HIT_CLOSE_POPUP) {
        close_popup();
    } else if (kind == HIT_TAB) {
        qlen = 0;
        query[0] = 0;
        launch_tab = arg;
        launch_cat = -1;
        launch_top = 0;
        launcher_build();
        sel_first();
    } else if (kind == HIT_CRUMB) {
        launch_cat = -1;
        launch_top = 0;
        launcher_build();
        sel_first();
    } else if (kind == HIT_ITEM && arg < nitems && !items[arg].header) {
        /* Sheets pick a tile with one click and use it with a second (or a
         * double click); lists act at once, like Plasma's launcher. */
        int again = sel == arg || m.clicks >= 2;
        sel = arg;
        if (popup != POP_STRIP || again)
            activate_sel();
    }
}

void ws_mouse(struct mouse m)
{
    if (g_ws.phase == PHASE_OFF)
        return;
    if (m.type == MOUSE_MOVE) {
        if (drag.kind) {
            drag_move(m);
            g_ws.need_redraw = 1;
        }
        return;
    }
    if (m.type == MOUSE_UP) {
        drag.kind = 0;
        return;
    }
    int arg = 0, kind = ws_hit_at(m.x, m.y, &arg);
    g_ws.need_redraw = 1;
    if (g_ws.phase == PHASE_LOGIN) {
        if (m.type == MOUSE_DOWN && kind == HIT_LOGIN)
            log_in();
        return;
    }
    if (m.type == MOUSE_WHEEL) {
        int up = m.button == 4;
        if (popup != POP_NONE && is_popup_hit(kind)) {
            if (popup == POP_STRIP)
                sel = up ? MAX(sel - 1, 0) : MIN(sel + 1, MAX(nitems - 1, 0));
            else
                sel_move(up ? -1 : 1);
        } else if (kind >= HIT_WIN_TITLE && kind <= HIT_WIN_RESIZE) {
            struct window *w = wm_by_pid(arg);
            if (w && kind == HIT_WIN_CLIENT)
                window_click(kind, w, m);
        } else if (kind == HIT_PANEL_DESK || kind == HIT_PANEL || kind == HIT_PANEL_LAUNCHER) {
            close_popup();
            g_ws.desk = (g_ws.desk + (up ? NUM_DESKS - 1 : 1)) % NUM_DESKS;
        }
        return;
    }
    if (m.button != 1 && !(kind == HIT_WIN_CLIENT && m.button == 3))
        return;

    /* A click outside an open popup closes it (the launcher button toggles). */
    if (popup != POP_NONE && !is_popup_hit(kind) && kind != HIT_PANEL_LAUNCHER) {
        close_popup();
        return;
    }
    struct activity *a = cur_act();
    switch (kind) {
    case HIT_ITEM: case HIT_TAB: case HIT_CRUMB: case HIT_CLOSE_POPUP:
        popup_click(kind, arg, m);
        break;
    case HIT_PANEL_LAUNCHER: open_popup(POP_LAUNCHER); break;
    case HIT_PANEL_DESK:
        g_ws.desk = arg;
        g_ws.move_mode = 0;
        break;
    case HIT_PANEL_TASK: {
        struct window *w = wm_by_pid(arg);
        if (w && w == wm_focused())
            wm_minimize(w);
        else
            wm_activate(w);
        break;
    }
    case HIT_PANEL_SHOWDESK: g_ws.dashboard ^= 1; break;
    case HIT_TOAST: g_ws.toast_until = 0; break;
    case HIT_WIN_TITLE: case HIT_WIN_CLOSE: case HIT_WIN_MAX: case HIT_WIN_MIN:
    case HIT_WIN_CLIENT: case HIT_WIN_RESIZE:
        window_click(kind, wm_by_pid(arg), m);
        break;
    case HIT_WIDGET:
        a->focus = arg;
        break;
    case HIT_WIDGET_REMOVE:
        a->focus = arg;
        desktop_key((struct key){ K_DELETE, 0 });
        break;
    case HIT_WIDGET_MOVE:
        a->focus = arg;
        if (g_ws.locked)
            svc_notify("SkarletOS", "Widgets are locked. Unlock them from the desktop menu (Alt+F12).");
        else
            start_drag(kind, arg, a->widgets[arg].x, a->widgets[arg].y, 0, 0, m);
        break;
    case HIT_NONE:
        a->focus = -1; /* a click on the bare desktop */
        break;
    }
}

/* ======================================================================== */
/* Screens and the main loop                                                 */
/* ======================================================================== */

/* The login screen, like a lock screen: a big clock over the darkened
 * wallpaper, the user's avatar and a password field. */
static void draw_login(void)
{
    const struct theme *t = g_theme;
    gfx_wallpaper(g_ws.wallpaper);
    gfx_rect(0, 0, g_w, g_h, BLACK, t->dark ? 110 : 70);
    char clock[16], date[40];
    ws_format_time(clock, sizeof clock, 0);
    int m = (g_ws.now.month >= 1 && g_ws.now.month <= 12) ? g_ws.now.month : 1;
    static const char *const months[] = { "January", "February", "March", "April", "May",
                                          "June", "July", "August", "September", "October",
                                          "November", "December" };
    static const char *const days[] = { "Sunday", "Monday", "Tuesday", "Wednesday", "Thursday",
                                        "Friday", "Saturday" };
    k_snprintf(date, sizeof date, "%s, %d %s", days[weekday(g_ws.now.year, m, g_ws.now.day)],
               g_ws.now.day, months[m - 1]);
    int cy = g_h / 2 - 260;
    gfx_text_center(&font_huge, 0, cy, g_w, clock, WHITE, 255);
    gfx_text_center(&font_title, 0, cy + 118, g_w, date, WHITE, 220);

    int ay = g_h / 2 + 10;
    gfx_circle(g_w / 2, ay, 52, WHITE, 60);
    gfx_circle(g_w / 2, ay, 48, t->accent, 255);
    gfx_icon(IC_USER, g_w / 2 - 30, ay - 30, 60, WHITE, 255);
    gfx_text_center(&font_title, 0, ay + 62, g_w, "user", WHITE, 255);

    int fw = 320, fx = (g_w - fw) / 2, fy = ay + 106;
    gfx_rrect(fx, fy, fw, 44, 22, WHITE, 40);
    gfx_rrect_line(fx, fy, fw, 44, 22, WHITE, 90);
    int n = k_strlen(login_pw);
    if (n == 0)
        gfx_text(&font_ui, fx + 22, fy + 13, "Password", WHITE, 150);
    for (int i = 0; i < n && i < 20; i++)
        gfx_circle(fx + 26 + i * 16, fy + 22, 5, WHITE, 255);
    ws_hit(fx + fw - 40, fy + 4, 36, 36, HIT_LOGIN, 0);
    gfx_circle(fx + fw - 22, fy + 22, 16, t->accent, 255);
    gfx_icon(IC_CHEVRON_R, fx + fw - 33, fy + 11, 22, WHITE, 255);
    gfx_text_center(&font_ui, 0, fy + 62, g_w, login_failed ? "Wrong password, try again" :
                    plat_login_hint(), WHITE, 170);

    gfx_text_center(&font_ui_bold, 0, g_h - 70, g_w, "Welcome to SkarletOS", WHITE, 230);
    gfx_text_center(&font_small, 0, g_h - 46, g_w, "x86-64  |  inspired by KDE Plasma", WHITE,
                    150);
}

static void draw_off(void)
{
    gfx_rect(0, 0, g_w, g_h, BLACK, 255);
    gfx_circle(g_w / 2, g_h / 2 - 70, 36, MAROON, 255);
    gfx_icon(IC_LOGO, g_w / 2 - 22, g_h / 2 - 92, 44, WHITE, 255);
    if (g_ws.off_reboot) {
        gfx_text_center(&font_title, 0, g_h / 2, g_w, "Restarting...", WHITE, 255);
    } else {
        gfx_text_center(&font_title, 0, g_h / 2, g_w, "SkarletOS has shut down.", WHITE, 255);
        gfx_text_center(&font_ui, 0, g_h / 2 + 40, g_w,
                        "It is now safe to turn off your computer.", RGB(0xb0, 0xa8, 0xb2), 255);
    }
}

void ws_draw(void)
{
    gfx_noclip();
    gfx_textlog_reset();
    nhits = 0;
    nlayers = 0;
    if (g_ws.phase == PHASE_OFF) {
        draw_off();
        return;
    }
    if (g_ws.phase == PHASE_LOGIN) {
        draw_login();
        return;
    }
    /* Back to front: wallpaper, widgets on the desktop, windows (hidden while
     * the dashboard shows the widgets), the panel, popups, notifications. */
    gfx_wallpaper(g_ws.wallpaper);
    draw_widgets();
    wm_draw();
    draw_panel();
    if (g_ws.move_mode) {
        const char *msg = "Moving window: arrow keys, Enter to finish";
        int w = gfx_text_width(&font_ui_bold, msg) + 40;
        add_layer((g_w - w) / 2, 16, w, 36, 18);
        gfx_rrect((g_w - w) / 2, 16, w, 36, 18, g_theme->accent, 255);
        gfx_text_center(&font_ui_bold, (g_w - w) / 2, 25, w, msg, WHITE, 255);
    }
    if (popup == POP_LAUNCHER)
        draw_launcher();
    else if (popup == POP_RUNNER)
        draw_runner();
    else if (popup == POP_MENU)
        draw_menu();
    else if (popup == POP_STRIP)
        draw_strip();
    draw_toast();
}

static int last_second_of_day = -1;

static void update_clock(void)
{
    plat_time(&g_ws.now);
    int sod = g_ws.now.hour * 3600 + g_ws.now.minute * 60 + g_ws.now.second;
    if (sod == last_second_of_day)
        return;
    if (last_second_of_day >= 0)
        g_ws.uptime += (uint32_t)((sod - last_second_of_day + 86400) % 86400);
    last_second_of_day = sod;
    g_ws.need_redraw = 1;
}

void ws_tick(void)
{
    struct key k;
    while (plat_key_poll(&k))
        ws_key(k);
    /* Once shut down nothing changes any more: the final screen stays as it
     * is, even if the machine takes a moment to turn off (or never does). */
    if (g_ws.phase == PHASE_OFF)
        return;
    update_clock();
    if (g_ws.phase == PHASE_DESKTOP)
        wm_idle();
    if (g_ws.need_redraw) {
        g_ws.need_redraw = 0;
        ws_draw();
        plat_present(g_px, g_w, g_h);
    }
}

void ws_init(void)
{
    int w, h;
    plat_display_size(&w, &h);
    gfx_init(w, h);
    k_memset(&g_ws, 0, sizeof g_ws);
    popup = POP_NONE;
    nrecent = 0;
    nrecent_docs = 0;
    login_pw[0] = 0;
    last_second_of_day = -1;
    theme_apply(0, 0); /* Skarlet Dark with the default maroon accent */
    g_ws.clock24 = 1;
    vfs_init();
    wm_close_all();
    plat_time(&g_ws.now);
    k_srand((uint32_t)(g_ws.now.second * 7919 + g_ws.now.minute * 104729 + 1));

    /* Two activities to show the idea: each has its own widgets. */
    new_activity("Desktop");
    struct activity *a = &g_ws.activities[0];
    ws_place_widget(a, PL_FOLDERVIEW);
    a->widgets[0].x = 24;
    a->widgets[0].y = 24;
    ws_place_widget(a, PL_CLOCK);
    ws_place_widget(a, PL_NOTES);
    a->widgets[2].x = 24;
    a->widgets[2].y = 24 + g_plasmoid_types[PL_FOLDERVIEW].h + 20;
    a->focus = -1;

    new_activity("Play");
    a = &g_ws.activities[1];
    ws_place_widget(a, PL_FIFTEEN);
    ws_place_widget(a, PL_SYSMON);
    a->focus = -1;

    g_ws.phase = PHASE_LOGIN;
    g_ws.need_redraw = 1;
}
