/* wm.c - the window manager: stacking, focus, frames and virtual desktops.
 *
 * Windows are kept in a stacking order (z_order[0] is the bottom).  The
 * focused window is simply the top-most window that is visible on the current
 * virtual desktop and activity, which is how a click-to-focus window manager
 * like KWin behaves when you only ever raise the window you use.
 */
#include "desktop.h"
#include "gfx.h"
#include "lib.h"

static struct window windows[MAX_WIN];
static int z_order[MAX_WIN];
static int nz;
static int next_pid = 100;

/* The empty default for platforms that never open external windows. */
__attribute__((weak)) void plat_window_close(long ext) { (void)ext; }

static int on_this_desk(const struct window *w)
{
    return w->used && w->desk == g_ws.desk && w->activity == g_ws.activity;
}

int wm_visible(const struct window *w) { return on_this_desk(w) && !w->minimized; }

struct window *wm_by_ext(long ext)
{
    for (int i = 0; ext && i < MAX_WIN; i++)
        if (windows[i].used && windows[i].ext == ext)
            return &windows[i];
    return 0;
}

struct window *wm_by_pid(int pid)
{
    for (int i = 0; i < MAX_WIN; i++)
        if (windows[i].used && windows[i].pid == pid)
            return &windows[i];
    return 0;
}

struct window *wm_focused(void)
{
    if (g_ws.dashboard)
        return 0;
    for (int i = nz - 1; i >= 0; i--)
        if (wm_visible(&windows[z_order[i]]))
            return &windows[z_order[i]];
    return 0;
}

int wm_list_visible(struct window **out, int max)
{
    int n = 0;
    for (int i = 0; i < nz && n < max; i++)
        if (wm_visible(&windows[z_order[i]]))
            out[n++] = &windows[z_order[i]];
    return n;
}

int wm_list_desk(struct window **out, int max)
{
    int n = 0;
    for (int i = 0; i < nz && n < max; i++)
        if (on_this_desk(&windows[z_order[i]]))
            out[n++] = &windows[z_order[i]];
    return n;
}

int wm_list_all(struct window **out, int max)
{
    int n = 0;
    for (int i = 0; i < nz && n < max; i++)
        out[n++] = &windows[z_order[i]];
    return n;
}

static int z_index(const struct window *w)
{
    int idx = (int)(w - windows);
    for (int i = 0; i < nz; i++)
        if (z_order[i] == idx)
            return i;
    return -1;
}

void wm_raise(struct window *w)
{
    int i = z_index(w);
    if (i < 0)
        return;
    int idx = z_order[i];
    for (; i < nz - 1; i++)
        z_order[i] = z_order[i + 1];
    z_order[nz - 1] = idx;
    g_ws.dashboard = 0;
}

/* Alt+Tab: send the top window to the bottom so the next one comes up. */
void wm_cycle(void)
{
    struct window *vis[MAX_WIN];
    int n = wm_list_visible(vis, MAX_WIN);
    if (n < 2)
        return;
    struct window *top = vis[n - 1];
    int i = z_index(top);
    int idx = z_order[i];
    for (; i > 0; i--)
        z_order[i] = z_order[i - 1];
    z_order[0] = idx;
}

/* A new window of the given outer size, cascaded so that new windows do not
 * hide each other completely, on top of the stack. */
static struct window *new_window(int app, int ww, int wh)
{
    struct window *w = 0;
    for (int i = 0; i < MAX_WIN; i++)
        if (!windows[i].used) {
            w = &windows[i];
            break;
        }
    if (!w)
        return 0;
    k_memset(w, 0, sizeof *w);
    w->used = 1;
    w->pid = next_pid++;
    w->app = app;
    w->desk = g_ws.desk;
    w->activity = g_ws.activity;
    struct window *vis[MAX_WIN];
    int n = wm_list_visible(vis, MAX_WIN);
    w->w = MIN(ww, g_w - 16);
    w->h = MIN(wh, DESK_BOTTOM - 16);
    w->x = g_w / 2 - w->w / 2 - 180 + (n % 6) * 48;
    w->y = 70 + (n % 5) * 40;
    if (w->x + w->w > g_w - 8)
        w->x = g_w - 8 - w->w;
    if (w->x < 8)
        w->x = 8;
    if (w->y + w->h > DESK_BOTTOM)
        w->y = MAX(8, DESK_BOTTOM - w->h);
    k_strlcpy(w->title, g_apps[app].name, sizeof w->title);
    z_order[nz++] = (int)(w - windows);
    g_ws.dashboard = 0;
    g_ws.need_redraw = 1;
    return w;
}

struct window *wm_open(int app, const char *arg)
{
    if (app < 0 || app >= APP_COUNT)
        return 0;
    const struct app *impl = g_app_impl[app];
    struct window *w = new_window(app, impl->w, impl->h);
    if (w)
        impl->init(w, arg);
    return w;
}

struct window *wm_open_external(long ext, const char *title, int cw, int ch)
{
    struct window *w = new_window(APP_EXTERNAL, cw + 2, ch + TITLE_H + 2);
    if (!w)
        return 0;
    w->ext = ext;
    k_strlcpy(w->title, title && *title ? title : "Application", sizeof w->title);
    return w;
}

void wm_client_rect(const struct window *w, int *x, int *y, int *cw, int *ch)
{
    *x = w->x + 1;
    *y = w->y + TITLE_H + 1;
    *cw = w->w - 2;
    *ch = w->h - TITLE_H - 2;
}

void wm_remove(struct window *w)
{
    int i = z_index(w);
    if (i < 0)
        return;
    if (g_app_impl[w->app]->close)
        g_app_impl[w->app]->close(w);
    for (; i < nz - 1; i++)
        z_order[i] = z_order[i + 1];
    nz--;
    w->used = 0;
    g_ws.move_mode = 0;
    g_ws.need_redraw = 1;
}

/* Our own windows close at once; another program is asked to close its
 * window, and the platform removes it when the program has done so (or
 * the program may ask "save changes?" first). */
void wm_close(struct window *w)
{
    if (w->ext)
        plat_window_close(w->ext);
    else
        wm_remove(w);
}

void wm_activate(struct window *w)
{
    if (!w || !w->used)
        return;
    w->minimized = 0;
    g_ws.desk = w->desk;
    g_ws.activity = w->activity;
    wm_raise(w);
    g_ws.need_redraw = 1;
}

void wm_minimize(struct window *w)
{
    w->minimized = 1;
    g_ws.move_mode = 0;
    g_ws.need_redraw = 1;
}

/* Maximized windows fill the space above the panel. */
void wm_toggle_maximize(struct window *w)
{
    if (w->maximized) {
        w->x = w->rx, w->y = w->ry, w->w = w->rw, w->h = w->rh;
        w->maximized = 0;
    } else {
        w->rx = w->x, w->ry = w->y, w->rw = w->w, w->rh = w->h;
        w->x = 0, w->y = 0, w->w = g_w, w->h = DESK_BOTTOM;
        w->maximized = 1;
    }
    g_ws.need_redraw = 1;
}

void wm_close_all(void)
{
    for (int i = 0; i < MAX_WIN; i++)
        if (windows[i].used && g_app_impl[windows[i].app]->close)
            g_app_impl[windows[i].app]->close(&windows[i]);
    for (int i = 0; i < MAX_WIN; i++)
        windows[i].used = 0;
    nz = 0;
}

/* An activity was deleted: close its windows and renumber the later ones.
 * Other programs' windows cannot be closed on the spot, so they move to the
 * first activity instead. */
void wm_activity_removed(int activity)
{
    for (int i = 0; i < MAX_WIN; i++) {
        struct window *w = &windows[i];
        if (!w->used)
            continue;
        if (w->activity == activity) {
            if (w->ext)
                w->activity = 0;
            else
                wm_remove(w);
        } else if (w->activity > activity) {
            w->activity--;
        }
    }
}

void wm_idle(void)
{
    for (int i = 0; i < MAX_WIN; i++)
        if (windows[i].used && g_app_impl[windows[i].app]->idle)
            g_app_impl[windows[i].app]->idle(&windows[i]);
}

/* Window decoration in the style of KDE Plasma's current Breeze theme: rounded
 * corners, a soft shadow (deeper on the active window), a title bar in the
 * window's own colours with the title centred and the app icon on the left,
 * and round minimise / maximise / close buttons on the right.  On the active
 * window the close button is filled with the accent colour and a thin accent
 * outline marks it out. */
#define FRAME_R 10 /* corner radius */

static uint32_t frame_outline(int focused, int *alpha)
{
    *alpha = focused ? 200 : 255;
    return focused ? g_theme->accent : g_theme->border;
}

static void draw_frame(struct window *w, int focused)
{
    const struct theme *t = g_theme;
    int r = FRAME_R, oa;
    gfx_shadow(w->x, w->y + (focused ? 6 : 3), w->w, w->h, r, focused ? 30 : 16,
               focused ? t->shadow_a : t->shadow_a / 2);
    gfx_rrect(w->x, w->y, w->w, w->h, r, t->window, 255);
    /* Title bar: the window's top, slightly tinted, with square bottom corners. */
    gfx_rrect(w->x, w->y, w->w, TITLE_H + r, r, t->titlebar, 255);
    gfx_rect(w->x, w->y + TITLE_H, w->w, r, t->window, 255);
    gfx_rect(w->x, w->y + TITLE_H, w->w, 1, t->divider, 255);
    uint32_t oc = frame_outline(focused, &oa);
    gfx_rrect_line(w->x, w->y, w->w, w->h, r, oc, oa);

    uint32_t title = focused ? t->text : t->text_dim;
    gfx_icon(w->ext_icon ? w->ext_icon : g_apps[w->app].icon, w->x + 12, w->y + 9, 18,
             focused ? t->accent_hi : t->text_dim, 255);
    char buf[64];
    if (g_ws.move_mode && focused)
        k_snprintf(buf, sizeof buf, "%s (moving)", w->title);
    else
        k_strlcpy(buf, w->title, sizeof buf);
    int tw = gfx_text_width(&font_ui_bold, buf);
    int avail = w->w - 160;
    if (tw > avail)
        gfx_text_fit(&font_ui_bold, w->x + 80, w->y + 9, avail, buf, title, 255);
    else
        gfx_text(&font_ui_bold, w->x + (w->w - tw) / 2, w->y + 9, buf, title, 255);

    /* Buttons: minimise, maximise, close. */
    int bx = w->x + w->w - 30, by = w->y + TITLE_H / 2;
    if (focused)
        gfx_circle(bx, by, 10, t->accent, 255);
    else
        gfx_circle(bx, by, 10, t->hover, 255);
    gfx_icon(IC_CLOSE, bx - 7, by - 7, 14, focused ? WHITE : t->text_dim, 255);
    gfx_circle(bx - 28, by, 10, t->hover, 255);
    gfx_icon(IC_MAX, bx - 35, by - 7, 14, title, 255);
    gfx_circle(bx - 56, by, 10, t->hover, 255);
    gfx_icon(IC_MIN, bx - 63, by - 7, 14, title, 255);
}

int wm_count_on_desk(int desk)
{
    int n = 0;
    for (int i = 0; i < MAX_WIN; i++)
        if (windows[i].used && windows[i].desk == desk && windows[i].activity == g_ws.activity)
            n++;
    return n;
}

void wm_draw(void)
{
    if (g_ws.dashboard)
        return;
    struct window *focused = wm_focused();
    for (int i = 0; i < nz; i++) {
        struct window *w = &windows[z_order[i]];
        if (!wm_visible(w))
            continue;
        draw_frame(w, w == focused);
        /* Clickable parts, top-most last: the whole window, its title bar,
         * the three buttons and (unless maximized) a resize grip. */
        int bx = w->x + w->w - 30, bmy = w->y + TITLE_H / 2;
        ws_hit(w->x, w->y, w->w, w->h, HIT_WIN_CLIENT, w->pid);
        ws_hit(w->x, w->y, w->w, TITLE_H, HIT_WIN_TITLE, w->pid);
        ws_hit(bx - 12, bmy - 12, 24, 24, HIT_WIN_CLOSE, w->pid);
        ws_hit(bx - 40, bmy - 12, 24, 24, HIT_WIN_MAX, w->pid);
        ws_hit(bx - 68, bmy - 12, 24, 24, HIT_WIN_MIN, w->pid);
        if (!w->maximized)
            ws_hit(w->x + w->w - 16, w->y + w->h - 16, 16, 16, HIT_WIN_RESIZE, w->pid);
        if (w->ext)
            continue; /* the program fills the client area itself */
        /* The client area fills the window below the title bar.  Its square
         * bottom corners would poke out of the rounded frame, so we save the
         * two corner squares first, and afterwards put them back and refill
         * the rounded part with the colour the app drew next to them. */
        enum { R = FRAME_R };
        uint32_t saved[2][R * R];
        int by = w->y + w->h - R, sx[2] = { w->x, w->x + w->w - R };
        for (int k = 0; k < 2; k++)
            for (int y = 0; y < R; y++)
                for (int x = 0; x < R; x++)
                    saved[k][y * R + x] = gfx_get(sx[k] + x, by + y);
        int cx = w->x + 1, cy = w->y + TITLE_H + 1, cw = w->w - 2, ch = w->h - TITLE_H - 2;
        gfx_clip(cx, cy, cw, ch);
        g_app_impl[w->app]->draw(w, cx, cy, cw, ch, w == focused);
        gfx_noclip();
        int oa;
        uint32_t oc = frame_outline(w == focused, &oa);
        for (int k = 0; k < 2; k++) {
            uint32_t fill = gfx_get(k ? sx[k] - 1 : sx[k] + R, w->y + w->h - 2);
            for (int y = 0; y < R; y++)
                for (int x = 0; x < R; x++)
                    gfx_rect(sx[k] + x, by + y, 1, 1, saved[k][y * R + x], 255);
            gfx_clip(sx[k], by, R, R);
            gfx_rrect(w->x, w->y, w->w, w->h, R, fill, 255);
            gfx_rrect_line(w->x, w->y, w->w, w->h, R, oc, oa);
            gfx_noclip();
        }
    }
}

/* ---- services used by the shell ----------------------------------------- */

int svc_launch(int app, const char *arg)
{
    struct window *w = wm_open(app, arg);
    if (w && app == APP_WRITE && arg)
        ws_note_document(arg);
    if (!w) {
        svc_notify("Window manager", "Too many windows are open.");
        return -1;
    }
    return w->pid;
}

int svc_kill(int pid)
{
    struct window *w = wm_by_pid(pid);
    if (!w)
        return -1;
    wm_close(w);
    return 0;
}

void svc_each_window(void (*fn)(void *, int, const char *, int, int), void *ctx)
{
    for (int i = 0; i < nz; i++) {
        struct window *w = &windows[z_order[i]];
        fn(ctx, w->pid, w->title, w->app, w->desk);
    }
}
