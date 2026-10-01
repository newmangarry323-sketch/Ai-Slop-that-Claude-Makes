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

int wm_visible(const struct window *w)
{
    return w->used && w->desk == g_ws.desk && w->activity == g_ws.activity;
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

struct window *wm_open(int app, const char *arg)
{
    if (app < 0 || app >= APP_COUNT)
        return 0;
    struct window *w = 0;
    for (int i = 0; i < MAX_WIN; i++)
        if (!windows[i].used) {
            w = &windows[i];
            break;
        }
    if (!w)
        return 0;

    const struct app *impl = g_app_impl[app];
    k_memset(w, 0, sizeof *w);
    w->used = 1;
    w->pid = next_pid++;
    w->app = app;
    w->w = impl->w;
    w->h = impl->h;
    w->desk = g_ws.desk;
    w->activity = g_ws.activity;

    /* Cascade new windows so they do not hide each other completely. */
    struct window *vis[MAX_WIN];
    int n = wm_list_visible(vis, MAX_WIN);
    w->x = 3 + (n % 6) * 3;
    w->y = 1 + (n % 5) * 1;
    if (w->x + w->w > SCR_W)
        w->x = SCR_W - w->w;
    if (w->y + w->h > DESK_H)
        w->y = DESK_H - w->h;
    k_strlcpy(w->title, g_apps[app].name, sizeof w->title);

    z_order[nz++] = (int)(w - windows);
    impl->init(w, arg);
    g_ws.dashboard = 0;
    g_ws.need_redraw = 1;
    return w;
}

void wm_close(struct window *w)
{
    int i = z_index(w);
    if (i < 0)
        return;
    for (; i < nz - 1; i++)
        z_order[i] = z_order[i + 1];
    nz--;
    w->used = 0;
    g_ws.move_mode = 0;
    g_ws.need_redraw = 1;
}

void wm_close_all(void)
{
    for (int i = 0; i < MAX_WIN; i++)
        windows[i].used = 0;
    nz = 0;
}

/* An activity was deleted: close its windows and renumber the later ones. */
void wm_activity_removed(int activity)
{
    for (int i = 0; i < MAX_WIN; i++) {
        if (!windows[i].used)
            continue;
        if (windows[i].activity == activity)
            wm_close(&windows[i]);
        else if (windows[i].activity > activity)
            windows[i].activity--;
    }
}

void wm_idle(void)
{
    for (int i = 0; i < MAX_WIN; i++)
        if (windows[i].used && g_app_impl[windows[i].app]->idle)
            g_app_impl[windows[i].app]->idle(&windows[i]);
}

/* Window decoration, loosely after KDE 4's Oxygen style: title centred in the
 * top border, menu glyph on the left, minimise/maximise/close on the right. */
static void draw_frame(struct window *w, int focused)
{
    const struct theme *t = g_theme;
    uint8_t border = focused ? t->win_border : t->win_border_off;
    uint8_t title = focused ? t->title_on : t->title_off;

    gfx_fill(w->x, w->y, w->w, w->h, ' ', t->win);
    gfx_box(w->x, w->y, w->w, w->h, border, 0);
    gfx_fill(w->x, w->y, w->w, 1, ' ', title);

    gfx_put(w->x + 1, w->y, CH_MENU, title);
    char buf[64];
    if (g_ws.move_mode && focused)
        k_snprintf(buf, sizeof buf, "%s (moving)", w->title);
    else
        k_strlcpy(buf, w->title, sizeof buf);
    gfx_center(w->x + 3, w->y, w->w - 12, buf, title);

    uint8_t btn = focused ? t->title_btn : title;
    gfx_text(w->x + w->w - 8, w->y, " _ ", btn);
    gfx_put(w->x + w->w - 5, w->y, CH_UTRI, btn);
    gfx_text(w->x + w->w - 4, w->y, " x ", btn);
    if (focused)
        gfx_shadow(w->x, w->y, w->w, w->h);
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
        gfx_clip(w->x + 1, w->y + 1, w->w - 2, w->h - 2);
        g_app_impl[w->app]->draw(w, w->x + 1, w->y + 1, w->w - 2, w->h - 2, w == focused);
        gfx_noclip();
    }
}

/* ---- services used by the shell ----------------------------------------- */

int svc_launch(int app, const char *arg)
{
    struct window *w = wm_open(app, arg);
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
