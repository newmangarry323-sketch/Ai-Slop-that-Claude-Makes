/* workspace.c - the Plasma-style shell: desktop containment, panel,
 * Skarlet Launcher (after KDE's Kickoff), Skarlet Runner (after KRunner), the
 * desktop toolbox, the Add Widgets and Activities strips, and notifications.
 * It owns the main loop (ws_tick) and decides which part of the screen gets
 * each key press.
 *
 * The layout follows the KDE Plasma 4.8 desktop: a panel along the bottom
 * (launcher, pager, task manager, system tray, clock, show desktop, panel
 * toolbox), a launcher with its search field at the top and its tabs at the
 * bottom, a runner that drops down from the top edge, and widget and activity
 * pickers that open as a strip just above the panel.
 */
#include "desktop.h"
#include "gfx.h"
#include "lib.h"

struct workspace g_ws;

/* ======================================================================== */
/* Actions: everything a menu entry, launcher entry or runner result can do  */
/* ======================================================================== */

enum {
    ACT_NONE, ACT_LAUNCH, ACT_PLACE, ACT_OPENFILE, ACT_RUN, ACT_CALC, ACT_CATEGORY,
    ACT_LOGOUT, ACT_REBOOT, ACT_POWEROFF,
    ACT_STRIP_WIDGETS, ACT_ADDW, ACT_REMOVEW, ACT_STRIP_ACTIVITIES, ACT_SWITCH_ACT,
    ACT_NEW_ACT, ACT_TOGGLE_LOCK, ACT_HELP,
};

struct item {
    char label[40];
    char hint[48];
    int act, arg;
    char str[VFS_PATH_MAX];
    int disabled;
    int header; /* a section title, not selectable */
    int icon;
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
    return it;
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
static char menu_title[32];
static int menu_x, menu_y, menu_w;
static char login_pw[24];
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

/* Text in the accent colour, readable on the panel of either theme. */
static uint8_t panel_accent_text(void)
{
    const struct accent *a = &g_accents[g_accent_index];
    return g_theme_index == 0 ? (uint8_t)((g_theme->panel & 0xF0) | a->dark)
                              : (uint8_t)((g_theme->panel & 0xF0) | a->light);
}

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

#define STRIP_H 9

/* A notification pops up above the system tray, like Plasma's: an icon and
 * title, a close button, and the message under a separator.  It moves above
 * an open widget strip, and waits while the launcher is open (the tray's
 * notification icon stays lit meanwhile). */
static void draw_toast(void)
{
    if (!g_ws.toast_title[0] || g_ws.uptime >= g_ws.toast_until || popup == POP_LAUNCHER)
        return;
    const struct theme *t = g_theme;
    int w = 42, h = 6, x = SCR_W - w - 1, y = PANEL_RIM - h;
    if (popup == POP_STRIP)
        y = PANEL_RIM - STRIP_H - h;
    int tw = w - 6;
    gfx_fill(x, y, w, h, ' ', t->toast);
    gfx_box(x, y, w, h, t->menu_border, 0);
    gfx_put(x + 2, y + 1, 'i', t->accent);
    gfx_textw(x + 4, y + 1, g_ws.toast_title, tw - 2, t->menu_head);
    gfx_put(x + w - 3, y + 1, 'x', t->menu_dim);
    gfx_hline(x + 1, y + 2, w - 2, t->menu_border);
    /* Wrap the message over two lines, breaking at the last space. */
    const char *msg = g_ws.toast_text;
    int len = k_strlen(msg), cut = len;
    if (len > tw) {
        cut = tw;
        while (cut > tw / 2 && msg[cut] != ' ')
            cut--;
    }
    gfx_textw(x + 3, y + 3, msg, cut, t->toast);
    if (cut < len)
        gfx_textw(x + 3, y + 4, msg + cut + (msg[cut] == ' '), tw, t->toast);
    gfx_shadow(x, y, w, h);
}

/* ======================================================================== */
/* Activities and the desktop containment                                    */
/* ======================================================================== */

static struct activity *cur_act(void) { return &g_ws.activities[g_ws.activity]; }

static int overlaps(struct activity *a, int x, int y, int w, int h)
{
    for (int i = 0; i < MAX_WIDGETS; i++) {
        struct plasmoid *p = &a->widgets[i];
        if (p->used && x < p->x + p->w + 2 && p->x < x + w + 2 && y < p->y + p->h &&
            p->y < y + h)
            return 1;
    }
    return 0;
}

/* Add a widget at the first free spot, scanning right-to-left like Plasma
 * filling the empty space of the desktop. */
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
    p->x = 2;
    p->y = 1;
    for (int y = 1; y + pt->h <= DESK_H; y++) {
        for (int x = SCR_W - pt->w - 3; x >= 1; x -= 2)
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

static void draw_wallpaper(void)
{
    const struct theme *t = g_theme;
    for (int y = 0; y <= PANEL_RIM; y++) {
        for (int x = 0; x < SCR_W; x++) {
            int ch = ' ';
            uint8_t a = t->desk;
            if (g_ws.wallpaper == 0) {
                /* "Horizon": two soft diagonal light streaks. */
                int d1 = y * 5 - x + 12, d2 = y * 5 - x - 30;
                ch = t->desk_ch;
                if ((d1 >= -3 && d1 <= 3) || (d2 >= -2 && d2 <= 2)) {
                    ch = CH_SHADE2;
                    a = t->desk_hi;
                } else if ((d1 >= -8 && d1 <= 8) || (d2 >= -6 && d2 <= 6)) {
                    a = t->desk_hi;
                }
            } else if (g_ws.wallpaper == 1) {
                if (x % 4 == 0 && y % 2 == 0)
                    ch = CH_DOT;
            }
            gfx_put(x, y, ch, a);
        }
    }
}

/* When a widget has focus and widgets are unlocked, Plasma 4 showed an
 * "applet handle" beside it with buttons to remove, resize, rotate or
 * configure it.  Ours shows the two things the keyboard can do: remove
 * (Delete) and move (Alt+arrows). */
static void draw_applet_handle(struct plasmoid *p)
{
    const struct theme *t = g_theme;
    int hx = p->x + p->w;
    if (hx >= SCR_W)
        hx = p->x - 1;
    gfx_fill(hx, p->y, 1, 4, ' ', t->panel);
    gfx_put(hx, p->y + 1, 'x', t->panel);
    gfx_put(hx, p->y + 2, 0x12, t->panel); /* up/down arrow */
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
        gfx_fill(p->x, p->y, p->w, p->h, ' ', t->widget);
        gfx_box(p->x, p->y, p->w, p->h, focused ? t->widget_focus : t->widget_border, 0);
        int cy = p->y + 1, ch = p->h - 2;
        if (pt->header) {
            /* Plasma widgets carry their title inside, above a thin line. */
            gfx_center(p->x + 1, cy, p->w - 2, pt->name, t->widget_head);
            gfx_hline(p->x + 1, cy + 1, p->w - 2, t->widget_border);
            cy += 2;
            ch -= 2;
        }
        gfx_clip(p->x + 1, cy, p->w - 2, ch);
        pt->draw(p, p->x + 1, cy, p->w - 2, ch, focused);
        gfx_noclip();
        if (focused && !g_ws.locked)
            draw_applet_handle(p);
    }
    /* The desktop toolbox, the "cashew" in the top right corner. */
    gfx_text(SCR_W - 4, 0, " \x0f ", popup == POP_MENU ? t->accent : t->panel);
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
        svc_notify("SkarletOS", "Widgets are locked. Unlock them in the toolbox (Alt+F12).");
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
        int dx = k.code == K_LEFT ? -2 : k.code == K_RIGHT ? 2 : 0;
        int dy = k.code == K_UP ? -1 : k.code == K_DOWN ? 1 : 0;
        p->x = MAX(0, MIN(p->x + dx, SCR_W - p->w));
        p->y = MAX(0, MIN(p->y + dy, DESK_H - p->h));
        return;
    }
    const struct plasmoid_type *pt = &g_plasmoid_types[p->type];
    if (pt->key)
        pt->key(p, k);
}

/* ======================================================================== */
/* The panel: a containment holding panel applets in a row                   */
/* ======================================================================== */

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

static void applet_launcher(int x, int w)
{
    (void)w;
    gfx_text(x, PANEL_Y, " S ", popup == POP_LAUNCHER ? g_theme->accent_alt : g_theme->accent);
}

/* The pager: one cell per virtual desktop.  Desktops that hold windows are
 * shaded, like the little window outlines in Plasma's pager. */
static void applet_pager(int x, int w)
{
    (void)w;
    for (int d = 0; d < NUM_DESKS; d++) {
        char s[4] = { ' ', (char)('1' + d), ' ', 0 };
        uint8_t a = d == g_ws.desk ? g_theme->accent
                    : wm_count_on_desk(d) ? g_theme->panel_btn : g_theme->panel;
        gfx_text(x + d * 3, PANEL_Y, s, a);
    }
}

static void applet_tasks(int x, int w)
{
    struct window *vis[MAX_WIN];
    int n = wm_list_visible(vis, MAX_WIN);
    struct window *focused = wm_focused();
    /* Task buttons keep a stable order (by pid), not stacking order. */
    for (int i = 1; i < n; i++)
        for (int j = i; j > 0 && vis[j]->pid < vis[j - 1]->pid; j--) {
            struct window *tmp = vis[j];
            vis[j] = vis[j - 1];
            vis[j - 1] = tmp;
        }
    if (n == 0)
        return;
    int bw = MIN(22, w / n);
    for (int i = 0; i < n; i++) {
        uint8_t a = vis[i] == focused ? g_theme->accent : g_theme->panel_btn;
        int bx = x + i * bw;
        gfx_fill(bx, PANEL_Y, bw - 1, 1, ' ', a);
        gfx_put(bx + 1, PANEL_Y, g_apps[vis[i]->app].icon, a);
        gfx_textw(bx + 3, PANEL_Y, vis[i]->title, bw - 4, a);
    }
}

/* System tray: the arrow that shows hidden icons, the device notifier, the
 * volume, and the notifications icon (lit while a notification is shown). */
static void applet_tray(int x, int w)
{
    (void)w;
    const struct theme *t = g_theme;
    int active = g_ws.toast_title[0] && g_ws.uptime < g_ws.toast_until;
    gfx_put(x + 1, PANEL_Y, CH_UTRI, t->panel_dim);
    gfx_put(x + 3, PANEL_Y, 0x16, t->panel); /* a drive-like bar */
    gfx_put(x + 5, PANEL_Y, CH_NOTE, t->panel);
    gfx_put(x + 7, PANEL_Y, 'i', active ? t->accent : t->panel_dim);
}

static void applet_clock(int x, int w)
{
    char buf[16];
    ws_format_time(buf, sizeof buf, g_ws.clock_seconds);
    gfx_text(x + w - k_strlen(buf) - 1, PANEL_Y, buf, g_theme->panel);
}

static void applet_show_desktop(int x, int w)
{
    (void)w;
    gfx_put(x + 1, PANEL_Y, CH_HOUSE, g_ws.dashboard ? g_theme->accent : g_theme->panel);
}

static void applet_toolbox(int x, int w)
{
    (void)w;
    gfx_put(x + 1, PANEL_Y, CH_SUN, g_theme->panel_dim);
}

struct panel_applet {
    const char *name;
    int width; /* 0 = take the remaining space */
    void (*draw)(int x, int w);
};

/* The panel's applets, left to right, as on the default Plasma 4 panel:
 * launcher, pager, task manager, system tray, clock, "show desktop", and
 * the panel's own toolbox at the end. */
static const struct panel_applet panel_applets[] = {
    { "launcher", 3, applet_launcher },
    { "pager", 12, applet_pager },
    { "tasks", 0, applet_tasks },
    { "systray", 8, applet_tray },
    { "clock", 10, applet_clock },
    { "showdesktop", 2, applet_show_desktop },
    { "toolbox", 2, applet_toolbox },
};

static void draw_panel(void)
{
    const struct theme *t = g_theme;
    /* The rim: lower half blocks in the panel colour over whatever is above,
     * so the panel looks taller than one text row and slightly rounded. */
    for (int x = 0; x < SCR_W; x++) {
        uint8_t under = gfx_attr_at(x, PANEL_RIM) >> 4;
        gfx_put(x, PANEL_RIM, CH_LOWER, (uint8_t)((under << 4) | (t->panel >> 4)));
    }
    gfx_fill(0, PANEL_Y, SCR_W, 1, ' ', t->panel);
    int fixed = 0, n = ARRAY_LEN(panel_applets);
    for (int i = 0; i < n; i++)
        fixed += panel_applets[i].width + 1;
    int x = 0;
    for (int i = 0; i < n; i++) {
        int w = panel_applets[i].width ? panel_applets[i].width : SCR_W - fixed + 1;
        panel_applets[i].draw(x, w);
        x += w + 1;
    }
}

/* ======================================================================== */
/* Skarlet Launcher (after Kickoff)                                          */
/* ======================================================================== */

/* Kickoff's five tabs, with the icons drawn above their names. */
static const char *const launch_tabs[] = { "Favorites", "Applications", "Computer",
                                           "Recently Used", "Leave" };
static const int launch_tab_icons[] = { 0x03, CH_MENU, CH_SQUARE, CH_CIRCLE, 0x1B };
static const char *const launch_cats[] = { "System", "Utilities", "Settings" };
static const char *const launch_places[][2] = {
    { "Home", "/home/user" }, { "Desktop", "/home/user/Desktop" },
    { "Documents", "/home/user/Documents" }, { "Music", "/home/user/Music" },
    { "Root", "/" }, { "Temp", "/tmp" },
};

static void add_app_item(int app)
{
    struct item *it = add_item(g_apps[app].name, g_apps[app].generic, ACT_LAUNCH, app, 0);
    if (it)
        it->icon = g_apps[app].icon;
}

static void add_place_item(int i)
{
    struct item *it = add_item(launch_places[i][0], launch_places[i][1], ACT_PLACE, 0,
                               launch_places[i][1]);
    if (it)
        it->icon = CH_HOUSE;
}

static void add_leave_items(const char *filter)
{
    static const struct { const char *label, *hint, *section; int act, icon; } leave[] = {
        { "Log out", "End the session", "Session", ACT_LOGOUT, 0x1B },
        { "Restart", "Restart the computer", "System", ACT_REBOOT, CH_CIRCLE },
        { "Shut down", "Turn off the computer", "System", ACT_POWEROFF, CH_SQUARE },
    };
    const char *section = 0;
    for (int i = 0; i < ARRAY_LEN(leave); i++) {
        if (filter && !k_strcasestr(leave[i].label, filter))
            continue;
        if (!filter && (!section || k_strcmp(section, leave[i].section) != 0)) {
            section = leave[i].section;
            add_header(section);
        }
        struct item *it = add_item(leave[i].label, leave[i].hint, leave[i].act, 0, 0);
        if (it)
            it->icon = leave[i].icon;
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
        end_section(h);
        h = nitems;
        add_header("Places");
        for (int i = 0; i < ARRAY_LEN(launch_places); i++)
            if (k_strcasestr(launch_places[i][0], query))
                add_place_item(i);
        end_section(h);
        h = nitems;
        add_header("Leave");
        add_leave_items(query);
        end_section(h);
    } else if (launch_tab == 0) {
        add_app_item(APP_TERMINAL);
        add_app_item(APP_FILES);
        add_app_item(APP_WRITE);
        add_app_item(APP_SETTINGS);
    } else if (launch_tab == 1) {
        if (launch_cat < 0) {
            /* Top level: the categories, which open like folders. */
            for (int c = 0; c < ARRAY_LEN(launch_cats); c++) {
                int count = 0;
                for (int a = 0; a < APP_COUNT; a++)
                    count += k_strcmp(g_apps[a].category, launch_cats[c]) == 0;
                char hint[32];
                k_snprintf(hint, sizeof hint, "%d application%s", count, count == 1 ? "" : "s");
                struct item *it = add_item(launch_cats[c], hint, ACT_CATEGORY, c, 0);
                if (it)
                    it->icon = CH_RTRI;
            }
        } else {
            for (int a = 0; a < APP_COUNT; a++)
                if (k_strcmp(g_apps[a].category, launch_cats[launch_cat]) == 0)
                    add_app_item(a);
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
            struct item *it = add_item(name, recent_docs[i], ACT_OPENFILE, 0, recent_docs[i]);
            if (it)
                it->icon = CH_MENU;
        }
        end_section(h);
    } else {
        add_leave_items(0);
    }
    sel_fix();
}

#define LAUNCH_W 67
#define LAUNCH_H 20

static void draw_launcher(void)
{
    const struct theme *t = g_theme;
    int w = LAUNCH_W, h = LAUNCH_H, x = 0, y = PANEL_RIM - h;
    gfx_fill(x, y, w, h, ' ', t->menu);
    gfx_box(x, y, w, h, t->menu_border, 0);

    /* Header: who is logged in on the left, the search field on the right. */
    gfx_put(x + 2, y + 1, CH_SMILE, t->menu_head);
    gfx_text(x + 4, y + 1, "user", t->menu_head);
    gfx_text(x + 9, y + 1, "on skarlet", t->menu_dim);
    gfx_text(x + w - 31, y + 1, "Search:", t->menu);
    gfx_textw(x + w - 23, y + 1, query, 21, t->input);
    gfx_put(x + w - 23 + MIN(qlen, 20), y + 1, '_', t->input);
    gfx_hline(x + 1, y + 2, w - 2, t->menu_border);

    int top = y + 3, rows = h - 7;
    if (qlen == 0 && launch_tab == 1 && launch_cat >= 0) {
        /* Kickoff's breadcrumb: back to all applications. */
        char crumb[48];
        k_snprintf(crumb, sizeof crumb, "%c All Applications  %c  %s", CH_LTRI, CH_RTRI,
                   launch_cats[launch_cat]);
        gfx_text(x + 2, top, crumb, t->menu_head);
        top++;
        rows--;
    }
    if (nitems == 0)
        gfx_text(x + 4, top + 1, "Nothing here yet.", t->menu_dim);

    /* Entries are two lines (name, then description); section headers one.
     * Scroll so the selected entry is visible. */
    if (launch_top > sel)
        launch_top = sel;
    for (;;) {
        int used = 0;
        for (int i = launch_top; i <= sel && i < nitems; i++)
            used += items[i].header ? 1 : 2;
        if (used <= rows || launch_top >= sel)
            break;
        launch_top++;
    }
    int row = 0;
    for (int i = launch_top; i < nitems; i++) {
        struct item *it = &items[i];
        int need = it->header ? 1 : 2;
        if (row + need > rows)
            break;
        int ry = top + row;
        if (it->header) {
            gfx_hline(x + 1, ry, w - 2, t->menu_border);
            char title[44];
            k_snprintf(title, sizeof title, " %s ", it->label);
            gfx_text(x + 3, ry, title, t->menu_head);
        } else {
            int is_sel = i == sel;
            uint8_t a = is_sel ? t->menu_hi : t->menu;
            uint8_t d = is_sel ? t->menu_hi : t->menu_dim;
            gfx_fill(x + 1, ry, w - 2, 2, ' ', a);
            if (it->icon)
                gfx_put(x + 3, ry, it->icon, is_sel ? t->menu_hi : t->menu_head);
            gfx_textw(x + 5, ry, it->label, w - 8, a);
            gfx_textw(x + 5, ry + 1, it->hint, w - 8, d);
        }
        row += need;
    }

    /* The tab bar: icons above names. */
    gfx_hline(x + 1, y + h - 4, w - 2, t->menu_border);
    int cw = (w - 2) / ARRAY_LEN(launch_tabs);
    for (int i = 0; i < ARRAY_LEN(launch_tabs); i++) {
        int cx = x + 1 + i * cw;
        uint8_t a = (i == launch_tab && qlen == 0) ? t->menu_hi : t->menu;
        gfx_fill(cx, y + h - 3, cw, 2, ' ', a);
        gfx_put(cx + cw / 2, y + h - 3, launch_tab_icons[i],
                (i == launch_tab && qlen == 0) ? t->menu_hi : t->menu_head);
        gfx_center(cx, y + h - 2, cw, launch_tabs[i], a);
    }
    gfx_shadow(x, y, w, h);
}

/* ======================================================================== */
/* Skarlet Runner (after KRunner): drops down from the top edge              */
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
    struct item *it;
    if (k_eval(expr, &v) == 0 && (query[0] == '=' || k_strchr(expr, '+') || k_strchr(expr, '-') ||
                                  k_strchr(expr, '*') || k_strchr(expr, '/') || k_strchr(expr, '%'))) {
        char label[40], result[16];
        k_snprintf(result, sizeof result, "%d", v);
        k_snprintf(label, sizeof label, "= %s", result);
        it = add_item(label, "Calculator", ACT_CALC, 0, result);
        if (it)
            it->icon = '=';
    }
    /* Application runner. */
    for (int a = 0; a < APP_COUNT; a++)
        if (k_strcasestr(g_apps[a].name, query) || k_strcasestr(g_apps[a].id, query) ||
            k_strcasestr(g_apps[a].generic, query)) {
            it = add_item(g_apps[a].name, "Launch application", ACT_LAUNCH, a, 0);
            if (it)
                it->icon = g_apps[a].icon;
        }
    /* Places runner: anything that looks like a path. */
    int node = vfs_lookup(VFS_ROOT, query);
    if ((query[0] == '/' || query[0] == '~') && node >= 0) {
        char path[VFS_PATH_MAX], label[40];
        vfs_path(node, path, sizeof path);
        k_snprintf(label, sizeof label, "Open %s", path);
        it = add_item(label, vfs_is_dir(node) ? "Skarlet Files" : "Skarlet Write",
                      vfs_is_dir(node) ? ACT_PLACE : ACT_OPENFILE, 0, path);
        if (it)
            it->icon = CH_HOUSE;
    }
    /* Command line runner: run it in a terminal. */
    char label[48];
    k_snprintf(label, sizeof label, "Run \"%s\"", query);
    it = add_item(label, "Command line (Skarlet Terminal)", ACT_RUN, 0, query);
    if (it)
        it->icon = '>';
    if (sel >= nitems)
        sel = MAX(nitems - 1, 0);
}

/* A one-line list with icons, used by the runner and the toolbox menu. */
static void draw_item_list(int x, int y, int w, int rows)
{
    const struct theme *t = g_theme;
    int top = sel >= rows ? sel - rows + 1 : 0;
    int lw = 12, any_hint = 0;
    for (int i = 0; i < nitems; i++) {
        lw = MAX(lw, k_strlen(items[i].label));
        any_hint |= items[i].hint[0] != 0;
    }
    /* Leave room for a hint column only if some entry has one. */
    lw = any_hint ? MIN(lw, w * 2 / 3) : w - 4;
    for (int r = 0; r < rows && top + r < nitems; r++) {
        struct item *it = &items[top + r];
        int is_sel = top + r == sel;
        uint8_t a = is_sel ? t->menu_hi : (it->disabled ? t->menu_dim : t->menu);
        uint8_t hint = is_sel ? t->menu_hi : t->menu_dim;
        gfx_fill(x, y + r, w, 1, ' ', a);
        if (it->icon)
            gfx_put(x + 1, y + r, it->icon, is_sel ? t->menu_hi : t->menu_head);
        gfx_textw(x + 3, y + r, it->label, lw, a);
        gfx_textw(x + lw + 5, y + r, it->hint, w - lw - 6, hint);
    }
}

static void draw_runner(void)
{
    const struct theme *t = g_theme;
    int w = 60, x = (SCR_W - w) / 2, y = 0;
    int rows = MIN(nitems, 6);
    int h = 2 + (rows ? rows + 1 : 1);
    gfx_fill(x, y, w, h, ' ', t->menu);
    /* No top edge: the runner hangs from the top of the screen. */
    gfx_box(x, y - 1, w, h + 1, t->menu_border, 0);
    gfx_put(x + 2, y, '?', t->menu_dim);    /* help */
    gfx_put(x + 4, y, CH_SUN, t->menu_dim); /* settings */
    gfx_textw(x + 6, y, query, w - 12, t->input);
    gfx_put(x + 6 + MIN(qlen, w - 13), y, '_', t->input);
    gfx_put(x + w - 3, y, 'x', t->menu_dim); /* close */
    if (rows) {
        gfx_hline(x + 1, y + 1, w - 2, t->menu_border);
        draw_item_list(x + 1, y + 2, w - 2, rows);
    } else {
        gfx_text(x + 3, y + 1, "Apps, places like /etc, maths like 6*7, commands",
                 t->menu_dim);
    }
    gfx_shadow(x, y, w, h);
}

/* ======================================================================== */
/* Desktop toolbox menu (Alt+F12)                                            */
/* ======================================================================== */

static void open_menu(const char *title, int x, int y, int w)
{
    popup = POP_MENU;
    k_strlcpy(menu_title, title, sizeof menu_title);
    menu_x = x;
    menu_y = y;
    menu_w = w;
    sel = 0;
}

static void open_toolbox(void)
{
    struct activity *a = cur_act();
    nitems = 0;
    struct item *it = add_item("Add Widgets...", "", ACT_STRIP_WIDGETS, 0, 0);
    if (it) {
        it->icon = '+';
        it->disabled = g_ws.locked;
    }
    it = add_item("Activities...", "", ACT_STRIP_ACTIVITIES, 0, 0);
    if (it)
        it->icon = CH_CIRCLE;
    if (!g_ws.locked && a->focus >= 0 && a->widgets[a->focus].used) {
        char label[40];
        k_snprintf(label, sizeof label, "Remove %s",
                   g_plasmoid_types[a->widgets[a->focus].type].name);
        it = add_item(label, "", ACT_REMOVEW, a->focus, 0);
        if (it)
            it->icon = 'x';
    }
    it = add_item(g_ws.locked ? "Unlock Widgets" : "Lock Widgets", "", ACT_TOGGLE_LOCK, 0, 0);
    if (it)
        it->icon = 0x08;
    it = add_item("Desktop Settings", "", ACT_LAUNCH, APP_SETTINGS, 0);
    if (it)
        it->icon = CH_SUN;
    it = add_item("Keyboard Shortcuts", "", ACT_HELP, 0, 0);
    if (it)
        it->icon = CH_MENU;
    open_menu("Desktop Toolbox", SCR_W - 30, 1, 28);
}

static void draw_menu(void)
{
    const struct theme *t = g_theme;
    int h = nitems + 2;
    gfx_fill(menu_x, menu_y, menu_w, h, ' ', t->menu);
    gfx_box(menu_x, menu_y, menu_w, h, t->menu_border, 0);
    char title[40];
    k_snprintf(title, sizeof title, " %s ", menu_title);
    gfx_text(menu_x + 2, menu_y, title, t->menu_head);
    draw_item_list(menu_x + 1, menu_y + 1, menu_w - 2, nitems);
    gfx_shadow(menu_x, menu_y, menu_w, h);
}

/* ======================================================================== */
/* The Add Widgets and Activities strips, above the panel                    */
/* ======================================================================== */

#define TILE_W 14

static void strip_build(void)
{
    nitems = 0;
    struct item *it;
    if (strip_kind == STRIP_WIDGETS) {
        for (int i = 0; i < PL_COUNT; i++) {
            const struct plasmoid_type *pt = &g_plasmoid_types[i];
            if (qlen && !k_strcasestr(pt->name, query) && !k_strcasestr(pt->desc, query))
                continue;
            it = add_item(pt->name, pt->desc, ACT_ADDW, i, 0);
            if (it)
                it->icon = pt->icon;
        }
    } else {
        for (int i = 0; i < g_ws.nactivities; i++) {
            it = add_item(g_ws.activities[i].name,
                          i == g_ws.activity ? "The current activity" : "Switch to this activity",
                          ACT_SWITCH_ACT, i, 0);
            if (it)
                it->icon = i == g_ws.activity ? CH_BULLET : CH_CIRCLE;
        }
        it = add_item("New Activity", "Create an activity with a Folder View", ACT_NEW_ACT, 0, 0);
        if (it) {
            it->icon = '+';
            it->disabled = g_ws.nactivities >= MAX_ACTIVITIES;
        }
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

static void draw_tile(int x, int y, struct item *it, int selected)
{
    const struct theme *t = g_theme;
    uint8_t bg = selected ? t->accent : t->panel_btn;
    gfx_fill(x, y, TILE_W, 5, ' ', bg);
    gfx_box(x, y, TILE_W, 5, selected ? t->accent : t->panel_sep, 0);
    /* The icon, in a small block of its own. */
    gfx_text(x + TILE_W / 2 - 2, y + 1, "    ", selected ? t->accent_alt : t->accent);
    gfx_put(x + TILE_W / 2 - 1, y + 1, it->icon, selected ? t->accent_alt : t->accent);
    /* The name, over two lines if it does not fit on one. */
    int len = k_strlen(it->label), inner = TILE_W - 2, cut = len;
    if (len > inner) {
        cut = inner;
        while (cut > 0 && it->label[cut] != ' ')
            cut--;
        if (cut == 0)
            cut = inner;
    }
    char line[TILE_W];
    k_strlcpy(line, it->label, MIN(cut + 1, (int)sizeof line));
    gfx_center(x + 1, y + 2, inner, line, it->disabled && !selected ? t->panel_dim : bg);
    if (cut < len)
        gfx_center(x + 1, y + 3, inner, it->label + cut + (it->label[cut] == ' '),
                   it->disabled && !selected ? t->panel_dim : bg);
}

static void draw_strip(void)
{
    const struct theme *t = g_theme;
    int y0 = PANEL_RIM - STRIP_H;
    /* Same rim and colour as the panel, so it reads as part of it. */
    for (int x = 0; x < SCR_W; x++) {
        uint8_t under = gfx_attr_at(x, y0) >> 4;
        gfx_put(x, y0, CH_LOWER, (uint8_t)((under << 4) | (t->panel >> 4)));
    }
    gfx_fill(0, y0 + 1, SCR_W, STRIP_H - 1, ' ', t->panel);

    int widgets = strip_kind == STRIP_WIDGETS;
    gfx_text(2, y0 + 1, widgets ? "Add Widgets" : "Activities", panel_accent_text());
    if (widgets) {
        gfx_text(20, y0 + 1, "Search:", t->panel);
        gfx_textw(28, y0 + 1, query, 20, t->input);
        gfx_put(28 + MIN(qlen, 19), y0 + 1, '_', t->input);
    }
    gfx_text(SCR_W - 10, y0 + 1, "x Close", t->panel_dim);
    if (nitems > 0)
        gfx_textw(2, y0 + 2, items[sel].hint, SCR_W - 4, t->panel_dim);
    else
        gfx_text(2, y0 + 2, "No widgets match the search.", t->panel_dim);

    int visible = (SCR_W - 2) / (TILE_W + 1);
    int first = sel >= visible ? sel - visible + 1 : 0;
    for (int i = 0; i < visible && first + i < nitems; i++)
        draw_tile(2 + i * (TILE_W + 1), y0 + 3, &items[first + i], first + i == sel);
    if (first > 0)
        gfx_put(0, y0 + 5, CH_LTRI, t->panel);
    if (first + visible < nitems)
        gfx_put(SCR_W - 1, y0 + 5, CH_RTRI, t->panel);

    gfx_text(2, y0 + 8,
             widgets ? "Left/Right: choose   Enter: add   type to search   Esc: close"
                     : "Left/Right: choose   Enter: switch   Delete: remove   Esc: close",
             t->panel_dim);
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
    plat_present(g_screen);
    plat_reboot();
}

void svc_poweroff(void)
{
    close_popup();
    g_ws.phase = PHASE_OFF;
    g_ws.off_reboot = 0;
    ws_draw();
    plat_present(g_screen);
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
        if (sel < nitems) {
            if (popup == POP_LAUNCHER && items[sel].act == ACT_CATEGORY) {
                launch_cat = items[sel].arg;
                launch_top = 0;
                launcher_build();
                sel_first();
                return;
            }
            run_item(&items[sel]);
        }
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

static int global_shortcut(struct key k)
{
    int alt = k.mods & MOD_ALT, ctrl = k.mods & MOD_CTRL;
    if (alt && k.code == K_F1) {
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
            g_ws.phase = PHASE_DESKTOP;
            svc_notify("Welcome", "Alt+F1 opens the launcher, Alt+F2 Skarlet Runner.");
        } else if (k.code == K_BACKSPACE && len > 0) {
            login_pw[len - 1] = 0;
        } else if (k.code >= 32 && k.code < 127 && len < (int)sizeof login_pw - 1) {
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
            w->x = MAX(w->x - 2, -w->w + 10);
        else if (k.code == K_RIGHT)
            w->x = MIN(w->x + 2, SCR_W - 10);
        else if (k.code == K_UP)
            w->y = MAX(w->y - 1, 0);
        else if (k.code == K_DOWN)
            w->y = MIN(w->y + 1, DESK_H - 1);
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
/* Screens and the main loop                                                 */
/* ======================================================================== */

static void draw_login(void)
{
    const struct theme *t = g_theme;
    draw_wallpaper();
    gfx_fill(0, PANEL_Y, SCR_W, 1, ' ', t->desk);
    char clock[16];
    ws_format_time(clock, sizeof clock, 0);
    gfx_center(0, 2, SCR_W, clock, t->desk_hi);

    int w = 44, h = 11, x = (SCR_W - w) / 2, y = 7;
    gfx_fill(x, y, w, h, ' ', t->menu);
    gfx_box(x, y, w, h, t->menu_border, 1);
    gfx_center(x, y + 1, w, "Welcome to SkarletOS", t->menu_head);
    gfx_center(x, y + 2, w, "x86-64 / Plasma 4 inspired", t->menu_dim);
    gfx_fill(x + 18, y + 4, 8, 3, CH_SHADE1, t->menu_dim);
    gfx_put(x + 21, y + 4, CH_SMILE, t->menu_head);
    gfx_put(x + 22, y + 4, CH_SMILE, t->menu_head);
    gfx_center(x, y + 5, w, "user", t->menu);
    gfx_text(x + 6, y + 7, "Password:", t->menu);
    char stars[24];
    int n = k_strlen(login_pw);
    for (int i = 0; i < n; i++)
        stars[i] = '*';
    stars[n] = 0;
    gfx_textw(x + 16, y + 7, stars, 20, t->input);
    gfx_center(x, y + 9, w, "Press Enter to log in (any password)", t->menu_dim);
    gfx_shadow(x, y, w, h);
}

static void draw_off(void)
{
    gfx_fill(0, 0, SCR_W, SCR_H, ' ', ATTR(LGRAY, BLACK));
    if (g_ws.off_reboot) {
        gfx_center(0, 11, SCR_W, "Restarting...", ATTR(WHITE, BLACK));
    } else {
        gfx_center(0, 11, SCR_W, "SkarletOS has shut down.", ATTR(WHITE, BLACK));
        gfx_center(0, 13, SCR_W, "It is now safe to turn off your computer.",
                   ATTR(LGRAY, BLACK));
    }
}

void ws_draw(void)
{
    gfx_noclip();
    if (g_ws.phase == PHASE_OFF) {
        draw_off();
        return;
    }
    if (g_ws.phase == PHASE_LOGIN) {
        draw_login();
        return;
    }
    /* Back to front: wallpaper, widgets on the desktop, windows (hidden while
     * the dashboard shows the widgets), popups, notifications (always on top,
     * as in Plasma), and the panel last, so no drop shadow can darken it. */
    draw_wallpaper();
    draw_widgets();
    wm_draw();
    if (g_ws.move_mode)
        gfx_text(SCR_W / 2 - 18, PANEL_RIM - 1, " Moving: arrows, Enter to finish ",
                 g_theme->accent);
    if (popup == POP_LAUNCHER)
        draw_launcher();
    else if (popup == POP_RUNNER)
        draw_runner();
    else if (popup == POP_MENU)
        draw_menu();
    else if (popup == POP_STRIP)
        draw_strip();
    draw_toast();
    draw_panel();
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
    update_clock();
    if (g_ws.phase == PHASE_DESKTOP)
        wm_idle();
    if (g_ws.need_redraw) {
        g_ws.need_redraw = 0;
        ws_draw();
        plat_present(g_screen);
    }
}

void ws_init(void)
{
    k_memset(&g_ws, 0, sizeof g_ws);
    popup = POP_NONE;
    nrecent = 0;
    nrecent_docs = 0;
    login_pw[0] = 0;
    last_second_of_day = -1;
    theme_apply(0, 0); /* Skarlet Light with the default maroon accent */
    g_ws.clock24 = 1;
    vfs_init();
    wm_close_all();
    plat_time(&g_ws.now);
    k_srand((uint32_t)(g_ws.now.second * 7919 + g_ws.now.minute * 104729 + 1));

    /* Two activities to show the idea: each has its own widgets. */
    new_activity("Desktop");
    struct activity *a = &g_ws.activities[0];
    ws_place_widget(a, PL_FOLDERVIEW);
    a->widgets[0].x = 2;
    a->widgets[0].y = 1;
    ws_place_widget(a, PL_CLOCK);
    ws_place_widget(a, PL_NOTES);
    a->widgets[2].x = 2;
    a->widgets[2].y = 12;
    a->focus = -1;

    new_activity("Play");
    a = &g_ws.activities[1];
    ws_place_widget(a, PL_FIFTEEN);
    ws_place_widget(a, PL_SYSMON);
    a->focus = -1;

    g_ws.phase = PHASE_LOGIN;
    g_ws.need_redraw = 1;
}
