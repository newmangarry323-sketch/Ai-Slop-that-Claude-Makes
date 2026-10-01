/* workspace.c - the Plasma-style shell: desktop containment, panel,
 * Kickoff launcher, KRunner, the desktop toolbox, activities and
 * notifications.  It owns the main loop (ws_tick) and decides which part of
 * the screen gets each key press.
 */
#include "desktop.h"
#include "gfx.h"
#include "lib.h"

struct workspace g_ws;

/* ======================================================================== */
/* Actions: everything a menu entry, launcher entry or KRunner result can do */
/* ======================================================================== */

enum {
    ACT_NONE, ACT_LAUNCH, ACT_PLACE, ACT_OPENFILE, ACT_RUN, ACT_CALC,
    ACT_LOGOUT, ACT_REBOOT, ACT_POWEROFF,
    ACT_MENU_ADDW, ACT_ADDW, ACT_REMOVEW, ACT_MENU_ACTIVITIES, ACT_SWITCH_ACT,
    ACT_NEW_ACT, ACT_DEL_ACT, ACT_TOGGLE_LOCK, ACT_HELP,
};

struct item {
    char label[40];
    char hint[48];
    int act, arg;
    char str[VFS_PATH_MAX];
    int disabled;
};

#define MAX_ITEMS 20

static struct item items[MAX_ITEMS];
static int nitems, sel;

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

/* ---- popups ------------------------------------------------------------- */

enum { POP_NONE, POP_KICKOFF, POP_KRUNNER, POP_MENU };
static int popup;
static char query[40];
static int qlen;
static int kick_tab;
static char menu_title[32];
static int menu_x, menu_y, menu_w;
static char login_pw[24];
static int recent[5], nrecent;

static void close_popup(void)
{
    popup = POP_NONE;
    nitems = 0;
}

static void remember_recent(int app)
{
    int i, n = 0;
    int keep[5];
    keep[n++] = app;
    for (i = 0; i < nrecent && n < 5; i++)
        if (recent[i] != app)
            keep[n++] = recent[i];
    for (i = 0; i < n; i++)
        recent[i] = keep[i];
    nrecent = n;
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

static void draw_toast(void)
{
    if (!g_ws.toast_title[0] || g_ws.uptime >= g_ws.toast_until)
        return;
    const struct theme *t = g_theme;
    int w = 42, h = 5, x = SCR_W - w - 1, y = PANEL_Y - h;
    int tw = w - 6;
    gfx_fill(x, y, w, h, ' ', t->toast);
    gfx_box(x, y, w, h, t->menu_border, 0);
    gfx_put(x + 2, y + 1, 'i', t->panel_hi);
    gfx_textw(x + 4, y + 1, g_ws.toast_title, tw, t->menu_head);
    /* Wrap the message over two lines, breaking at the last space. */
    const char *msg = g_ws.toast_text;
    int len = k_strlen(msg), cut = len;
    if (len > tw) {
        cut = tw;
        while (cut > tw / 2 && msg[cut] != ' ')
            cut--;
    }
    gfx_textw(x + 4, y + 2, msg, cut, t->toast);
    if (cut < len)
        gfx_textw(x + 4, y + 3, msg + cut + (msg[cut] == ' '), tw, t->toast);
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
        if (p->used && x < p->x + p->w + 1 && p->x < x + w + 1 && y < p->y + p->h &&
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
        svc_notify("Plasma", "This activity has no room for more widgets.");
        return;
    }
    k_memset(p, 0, sizeof *p);
    p->used = 1;
    p->type = type;
    p->w = pt->w;
    p->h = pt->h;
    p->x = 2;
    p->y = 2;
    for (int y = 2; y + pt->h <= DESK_H; y++) {
        for (int x = SCR_W - pt->w - 2; x >= 1; x -= 2)
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

static void draw_wallpaper(void)
{
    const struct theme *t = g_theme;
    for (int y = 0; y < DESK_H; y++) {
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
        uint8_t frame = focused ? t->widget_focus : t->widget_border;
        gfx_fill(p->x, p->y, p->w, p->h, ' ', t->widget);
        gfx_box(p->x, p->y, p->w, p->h, frame, focused);
        char title[32];
        k_snprintf(title, sizeof title, " %s ", pt->name);
        gfx_center(p->x + 1, p->y, p->w - 2, title, focused ? t->menu_hi : t->widget_head);
        gfx_clip(p->x + 1, p->y + 1, p->w - 2, p->h - 2);
        pt->draw(p, p->x + 1, p->y + 1, p->w - 2, p->h - 2, focused);
        gfx_noclip();
    }
    /* The desktop toolbox, the "cashew" in the top right corner. */
    gfx_text(SCR_W - 4, 0, " \x0f ", popup == POP_MENU ? t->panel_hi : t->panel);
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
    if ((k.mods & MOD_ALT) && k.code >= K_UP && k.code <= K_RIGHT) {
        if (g_ws.locked) {
            svc_notify("Plasma", "Widgets are locked. Unlock them in the toolbox (Alt+F12).");
            return;
        }
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
    gfx_text(x, PANEL_Y, " K ", popup == POP_KICKOFF ? g_theme->panel_hi : ATTR(WHITE, BLUE));
}

static void applet_pager(int x, int w)
{
    (void)w;
    for (int d = 0; d < NUM_DESKS; d++) {
        char s[4] = { ' ', (char)('1' + d), ' ', 0 };
        gfx_text(x + d * 3, PANEL_Y, s, d == g_ws.desk ? g_theme->panel_hi : g_theme->panel);
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
        uint8_t a = vis[i] == focused ? g_theme->panel_hi : g_theme->panel;
        int bx = x + i * bw;
        gfx_fill(bx, PANEL_Y, bw - 1, 1, ' ', a);
        gfx_put(bx + 1, PANEL_Y, CH_SQUARE, a);
        gfx_textw(bx + 3, PANEL_Y, vis[i]->title, bw - 5, a);
    }
}

static void applet_tray(int x, int w)
{
    (void)w;
    int active = g_ws.toast_title[0] && g_ws.uptime < g_ws.toast_until;
    gfx_put(x + 1, PANEL_Y, CH_NOTE, g_theme->panel);
    gfx_put(x + 3, PANEL_Y, 'i', active ? g_theme->panel_hi : g_theme->panel_dim);
}

static void applet_clock(int x, int w)
{
    char buf[16];
    ws_format_time(buf, sizeof buf, g_ws.clock_seconds);
    gfx_textw(x, PANEL_Y, "", w, g_theme->panel);
    gfx_text(x + w - k_strlen(buf) - 1, PANEL_Y, buf, g_theme->panel);
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

/* The panel's applets, left to right: launcher, pager, task manager,
 * system tray, clock and the panel's own toolbox at the end. */
static const struct panel_applet panel_applets[] = {
    { "launcher", 3, applet_launcher },
    { "pager", 12, applet_pager },
    { "tasks", 0, applet_tasks },
    { "systray", 5, applet_tray },
    { "clock", 12, applet_clock },
    { "toolbox", 3, applet_toolbox },
};

static void draw_panel(void)
{
    const struct theme *t = g_theme;
    gfx_fill(0, PANEL_Y, SCR_W, 1, ' ', t->panel);
    int fixed = 0, n = ARRAY_LEN(panel_applets);
    for (int i = 0; i < n; i++)
        fixed += panel_applets[i].width + 1;
    int x = 0;
    for (int i = 0; i < n; i++) {
        int w = panel_applets[i].width ? panel_applets[i].width : SCR_W - fixed;
        panel_applets[i].draw(x, w);
        x += w;
        if (i + 1 < n)
            gfx_put(x++, PANEL_Y, CH_V, t->panel_sep);
    }
}

/* ======================================================================== */
/* Kickoff: the application launcher                                         */
/* ======================================================================== */

static const char *const kick_tabs[] = { "Favorites", "Applications", "Computer", "Recent",
                                         "Leave" };
static const char *const kick_places[][2] = {
    { "Home", "/home/user" }, { "Desktop", "/home/user/Desktop" },
    { "Documents", "/home/user/Documents" }, { "Music", "/home/user/Music" },
    { "Root", "/" }, { "Temp", "/tmp" },
};

static void add_app_item(int app)
{
    char hint[48];
    k_snprintf(hint, sizeof hint, "%s", g_apps[app].generic);
    add_item(g_apps[app].name, hint, ACT_LAUNCH, app, 0);
}

static void add_leave_items(const char *filter)
{
    static const struct { const char *label, *hint; int act; } leave[] = {
        { "Log out", "End the session", ACT_LOGOUT },
        { "Restart", "Reboot the computer", ACT_REBOOT },
        { "Shut down", "Turn off the computer", ACT_POWEROFF },
    };
    for (int i = 0; i < ARRAY_LEN(leave); i++)
        if (!filter || k_strcasestr(leave[i].label, filter))
            add_item(leave[i].label, leave[i].hint, leave[i].act, 0, 0);
}

static void kickoff_build(void)
{
    nitems = 0;
    if (qlen > 0) {
        for (int a = 0; a < APP_COUNT; a++)
            if (k_strcasestr(g_apps[a].name, query) || k_strcasestr(g_apps[a].generic, query) ||
                k_strcasestr(g_apps[a].id, query))
                add_app_item(a);
        for (int i = 0; i < ARRAY_LEN(kick_places); i++)
            if (k_strcasestr(kick_places[i][0], query))
                add_item(kick_places[i][0], kick_places[i][1], ACT_PLACE, 0, kick_places[i][1]);
        add_leave_items(query);
    } else if (kick_tab == 0) {
        add_app_item(APP_KONSOLE);
        add_app_item(APP_DOLPHIN);
        add_app_item(APP_KWRITE);
        add_app_item(APP_SETTINGS);
    } else if (kick_tab == 1) {
        for (int a = 0; a < APP_COUNT; a++) {
            char hint[48];
            k_snprintf(hint, sizeof hint, "%s / %s", g_apps[a].category, g_apps[a].generic);
            add_item(g_apps[a].name, hint, ACT_LAUNCH, a, 0);
        }
    } else if (kick_tab == 2) {
        for (int i = 0; i < ARRAY_LEN(kick_places); i++)
            add_item(kick_places[i][0], kick_places[i][1], ACT_PLACE, 0, kick_places[i][1]);
    } else if (kick_tab == 3) {
        for (int i = 0; i < nrecent; i++)
            add_app_item(recent[i]);
    } else {
        add_leave_items(0);
    }
    if (sel >= nitems)
        sel = MAX(nitems - 1, 0);
}

static void draw_item_list(int x, int y, int w, int rows)
{
    const struct theme *t = g_theme;
    int top = sel >= rows ? sel - rows + 1 : 0;
    /* Label column: as wide as the longest label, leaving room for hints. */
    int lw = 12;
    for (int i = 0; i < nitems; i++)
        lw = MAX(lw, k_strlen(items[i].label));
    lw = MIN(lw, w * 2 / 3);
    for (int r = 0; r < rows && top + r < nitems; r++) {
        struct item *it = &items[top + r];
        int is_sel = top + r == sel;
        uint8_t a = is_sel ? t->menu_hi : (it->disabled ? t->menu_dim : t->menu);
        uint8_t hint = is_sel ? t->menu_hi : t->menu_dim;
        gfx_fill(x, y + r, w, 1, ' ', a);
        gfx_textw(x + 1, y + r, it->label, lw, a);
        gfx_textw(x + lw + 3, y + r, it->hint, w - lw - 4, hint);
    }
}

static void draw_kickoff(void)
{
    const struct theme *t = g_theme;
    int w = 50, h = 18, x = 0, y = PANEL_Y - h;
    gfx_fill(x, y, w, h, ' ', t->menu);
    gfx_box(x, y, w, h, t->menu_border, 0);
    gfx_put(x + 2, y + 1, CH_SMILE, t->menu_head);
    gfx_text(x + 4, y + 1, "User user on plasmix", t->menu_head);
    gfx_text(x + 2, y + 2, "Search:", t->menu);
    gfx_textw(x + 10, y + 2, query, w - 12, t->input);
    gfx_put(x + 10 + qlen, y + 2, '_', t->input);
    gfx_hline(x + 1, y + 3, w - 2, t->menu_border);
    if (qlen > 0)
        gfx_text(x + 2, y + 3, " Search results ", t->menu_head);
    draw_item_list(x + 1, y + 4, w - 2, h - 8);
    if (nitems == 0)
        gfx_text(x + 3, y + 5, "Nothing here yet.", t->menu_dim);
    gfx_hline(x + 1, y + h - 3, w - 2, t->menu_border);
    int tx = x + 2;
    for (int i = 0; i < ARRAY_LEN(kick_tabs); i++) {
        uint8_t a = (i == kick_tab && qlen == 0) ? t->menu_hi : t->menu;
        gfx_text(tx, y + h - 2, kick_tabs[i], a);
        tx += k_strlen(kick_tabs[i]) + 2;
    }
    gfx_shadow(x, y, w, h);
}

/* ======================================================================== */
/* KRunner: one line that launches, opens, calculates and runs commands      */
/* ======================================================================== */

static void krunner_build(void)
{
    nitems = 0;
    if (qlen == 0) {
        if (sel >= nitems)
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
        add_item(label, "Calculator", ACT_CALC, 0, result);
    }
    /* Application runner. */
    for (int a = 0; a < APP_COUNT; a++)
        if (k_strcasestr(g_apps[a].name, query) || k_strcasestr(g_apps[a].id, query) ||
            k_strcasestr(g_apps[a].generic, query))
            add_item(g_apps[a].name, "Launch application", ACT_LAUNCH, a, 0);
    /* Places runner: anything that looks like a path. */
    int node = vfs_lookup(VFS_ROOT, query);
    if ((query[0] == '/' || query[0] == '~') && node >= 0) {
        char path[VFS_PATH_MAX], label[40];
        vfs_path(node, path, sizeof path);
        k_snprintf(label, sizeof label, "Open %s", path);
        add_item(label, vfs_is_dir(node) ? "Dolphin" : "KWrite",
                 vfs_is_dir(node) ? ACT_PLACE : ACT_OPENFILE, 0, path);
    }
    /* Command line runner: run it in a terminal. */
    char label[48];
    k_snprintf(label, sizeof label, "Run \"%s\"", query);
    add_item(label, "Command line (Konsole)", ACT_RUN, 0, query);
    if (sel >= nitems)
        sel = MAX(nitems - 1, 0);
}

static void draw_krunner(void)
{
    const struct theme *t = g_theme;
    int w = 56, x = (SCR_W - w) / 2, y = 0;
    int rows = MIN(nitems, 6);
    int h = 3 + (rows ? rows + 1 : 1);
    gfx_fill(x, y, w, h, ' ', t->menu);
    gfx_box(x, y, w, h, t->menu_border, 0);
    gfx_text(x + 2, y, " KRunner ", t->menu_head);
    gfx_put(x + 2, y + 1, CH_RTRI, t->menu_head);
    gfx_textw(x + 4, y + 1, query, w - 6, t->input);
    gfx_put(x + 4 + qlen, y + 1, '_', t->input);
    if (rows) {
        gfx_hline(x + 1, y + 2, w - 2, t->menu_border);
        draw_item_list(x + 1, y + 3, w - 2, rows);
    } else {
        gfx_text(x + 2, y + 2, "Apps, places like /etc, maths like 6*7, commands",
                 t->menu_dim);
    }
    gfx_shadow(x, y, w, h);
}

/* ======================================================================== */
/* Toolbox menus (Alt+F12): widgets, activities, locking                     */
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
    struct item *it = add_item("Add Widgets...", "", ACT_MENU_ADDW, 0, 0);
    if (g_ws.locked && it)
        it->disabled = 1;
    if (!g_ws.locked && a->focus >= 0 && a->widgets[a->focus].used) {
        char label[40];
        k_snprintf(label, sizeof label, "Remove %s",
                   g_plasmoid_types[a->widgets[a->focus].type].name);
        add_item(label, "", ACT_REMOVEW, a->focus, 0);
    }
    add_item("Activities...", "", ACT_MENU_ACTIVITIES, 0, 0);
    add_item(g_ws.locked ? "Unlock Widgets" : "Lock Widgets", "", ACT_TOGGLE_LOCK, 0, 0);
    add_item("Keyboard Shortcuts", "", ACT_HELP, 0, 0);
    open_menu("Desktop Toolbox", SCR_W - 30, 1, 28);
}

static void open_addwidgets(void)
{
    nitems = 0;
    for (int i = 0; i < PL_COUNT; i++)
        add_item(g_plasmoid_types[i].name, g_plasmoid_types[i].desc, ACT_ADDW, i, 0);
    open_menu("Add Widgets", SCR_W - 62, 1, 60);
}

static void open_activities(void)
{
    nitems = 0;
    for (int i = 0; i < g_ws.nactivities; i++) {
        char label[40];
        k_snprintf(label, sizeof label, "%c %s", i == g_ws.activity ? CH_BULLET : ' ',
                   g_ws.activities[i].name);
        add_item(label, "", ACT_SWITCH_ACT, i, 0);
    }
    struct item *it = add_item("+ New Activity", "", ACT_NEW_ACT, 0, 0);
    if (it && g_ws.nactivities >= MAX_ACTIVITIES)
        it->disabled = 1;
    if (g_ws.nactivities > 1)
        add_item("- Remove This Activity", "", ACT_DEL_ACT, 0, 0);
    open_menu("Activities", SCR_W - 30, 1, 28);
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
    for (int i = 0; i < nitems; i++) {
        struct item *it = &items[i];
        uint8_t a = i == sel ? t->menu_hi : (it->disabled ? t->menu_dim : t->menu);
        gfx_fill(menu_x + 1, menu_y + 1 + i, menu_w - 2, 1, ' ', a);
        gfx_textw(menu_x + 2, menu_y + 1 + i, it->label, menu_w > 40 ? 18 : menu_w - 4, a);
        if (menu_w > 40)
            gfx_textw(menu_x + 21, menu_y + 1 + i, it->hint, menu_w - 23,
                      i == sel ? t->menu_hi : t->menu_dim);
    }
    gfx_shadow(menu_x, menu_y, menu_w, h);
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
    if (it->disabled)
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
    case ACT_PLACE: svc_launch(APP_DOLPHIN, str); break;
    case ACT_OPENFILE: svc_launch(APP_KWRITE, str); break;
    case ACT_RUN: svc_launch(APP_KONSOLE, str); break;
    case ACT_CALC:
        /* Put the result back into KRunner so it can be used further. */
        popup = POP_KRUNNER;
        k_strlcpy(query, str, sizeof query);
        qlen = k_strlen(query);
        sel = 0;
        krunner_build();
        break;
    case ACT_LOGOUT: logout(); break;
    case ACT_REBOOT: svc_reboot(); break;
    case ACT_POWEROFF: svc_poweroff(); break;
    case ACT_MENU_ADDW: open_addwidgets(); break;
    case ACT_ADDW: {
        ws_place_widget(cur_act(), arg);
        char msg[64];
        k_snprintf(msg, sizeof msg, "Added %s to \"%s\".", g_plasmoid_types[arg].name,
                   cur_act()->name);
        svc_notify("Plasma", msg);
        break;
    }
    case ACT_REMOVEW:
        cur_act()->widgets[arg].used = 0;
        cur_act()->focus = -1;
        break;
    case ACT_MENU_ACTIVITIES: open_activities(); break;
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
    case ACT_DEL_ACT: {
        int victim = g_ws.activity;
        wm_activity_removed(victim);
        for (int i = victim; i < g_ws.nactivities - 1; i++)
            g_ws.activities[i] = g_ws.activities[i + 1];
        g_ws.nactivities--;
        g_ws.activity = 0;
        svc_notify("Activities", "Activity removed.");
        break;
    }
    case ACT_TOGGLE_LOCK:
        g_ws.locked ^= 1;
        svc_notify("Plasma", g_ws.locked ? "Widgets locked." : "Widgets unlocked.");
        break;
    case ACT_HELP: svc_launch(APP_KWRITE, "/home/user/Desktop/README.txt"); break;
    }
}

/* ======================================================================== */
/* Key routing                                                               */
/* ======================================================================== */

static void popup_key(struct key k)
{
    if (k.code == K_ESC) {
        close_popup();
        return;
    }
    if (k.code == K_UP) {
        if (sel > 0)
            sel--;
        return;
    }
    if (k.code == K_DOWN) {
        if (sel < nitems - 1)
            sel++;
        return;
    }
    if (k.code == K_ENTER) {
        if (sel < nitems)
            run_item(&items[sel]);
        return;
    }
    if (popup == POP_MENU)
        return;

    if (popup == POP_KICKOFF && qlen == 0 && (k.code == K_LEFT || k.code == K_RIGHT)) {
        int n = ARRAY_LEN(kick_tabs);
        kick_tab = (kick_tab + (k.code == K_RIGHT ? 1 : n - 1)) % n;
        sel = 0;
    } else if (k.code == K_BACKSPACE) {
        if (qlen > 0)
            query[--qlen] = 0;
        sel = 0;
    } else if (k.code >= 32 && k.code < 127 && !(k.mods & (MOD_CTRL | MOD_ALT)) &&
               qlen < (int)sizeof query - 1) {
        query[qlen++] = (char)k.code;
        query[qlen] = 0;
        sel = 0;
    }
    if (popup == POP_KICKOFF)
        kickoff_build();
    else
        krunner_build();
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
    kick_tab = 0; /* always open on Favorites */
    popup = which;
    if (which == POP_KICKOFF)
        kickoff_build();
    else if (which == POP_KRUNNER)
        krunner_build();
}

static int global_shortcut(struct key k)
{
    int alt = k.mods & MOD_ALT, ctrl = k.mods & MOD_CTRL;
    if (alt && k.code == K_F1) {
        open_popup(POP_KICKOFF);
    } else if (alt && k.code == K_F2) {
        open_popup(POP_KRUNNER);
    } else if (alt && k.code == K_F12) {
        if (popup == POP_MENU)
            close_popup();
        else
            open_toolbox();
    } else if (ctrl && k.code == K_ESC) {
        close_popup();
        svc_launch(APP_SYSMON, 0);
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
            svc_notify("Welcome", "Alt+F1 opens the launcher, Alt+F2 KRunner.");
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
    gfx_center(x, y + 1, w, "Welcome to Plasmix", t->menu_head);
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
        gfx_center(0, 11, SCR_W, "Plasmix has shut down.", ATTR(WHITE, BLACK));
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
     * the dashboard shows the widgets), the panel, then floating popups. */
    draw_wallpaper();
    draw_widgets();
    wm_draw();
    draw_panel();
    if (g_ws.move_mode)
        gfx_text(SCR_W / 2 - 18, PANEL_Y - 1, " Moving: arrows, Enter to finish ",
                 g_theme->panel_hi);
    draw_toast();
    if (popup == POP_KICKOFF)
        draw_kickoff();
    else if (popup == POP_KRUNNER)
        draw_krunner();
    else if (popup == POP_MENU)
        draw_menu();
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
    login_pw[0] = 0;
    last_second_of_day = -1;
    g_theme = &g_themes[0];
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
    a->widgets[0].y = 2;
    ws_place_widget(a, PL_CLOCK);
    ws_place_widget(a, PL_NOTES);
    a->widgets[2].x = 2;
    a->widgets[2].y = 11;
    a->focus = -1;

    new_activity("Play");
    a = &g_ws.activities[1];
    ws_place_widget(a, PL_FIFTEEN);
    ws_place_widget(a, PL_SYSMON);
    a->focus = -1;

    g_ws.phase = PHASE_LOGIN;
    g_ws.need_redraw = 1;
}
