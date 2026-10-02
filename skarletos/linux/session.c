/* session.c - SkarletOS as a desktop session on Linux (X11).
 *
 * On Linux, SkarletOS is the X window manager and the whole desktop at once,
 * as KDE's KWin and plasmashell are together.  The portable desktop in src/
 * still draws every frame into one back buffer (g_px); this file shows that
 * buffer through real X windows, so that other programs' windows can sit in
 * the right places in the stack:
 *
 *   top      layers: the panel, popups and notifications (ws_layers())
 *            frames: one X window per SkarletOS window, in its stacking
 *                    order.  A frame shows the window's part of the buffer
 *                    (title bar, border, and for Skarlet apps their
 *                    content); another program's window is placed inside
 *                    its frame, below the title bar ("reparenting")
 *   bottom   desktop: the whole buffer (wallpaper, widgets, shadows)
 *
 * Each X window simply shows the matching rectangle of the buffer, so the
 * only pixels SkarletOS does not draw are the other programs' own.
 *
 * Input: keys reach SkarletOS when its desktop has the keyboard focus, and
 * global shortcuts (Alt+F1, Alt+Tab...) are grabbed so they work over other
 * programs too.  Clicks on SkarletOS's own windows become ws_mouse() events;
 * a click on another program's window first activates it, then goes on to
 * the program.
 *
 * The rules a window manager follows are in the ICCCM and the Extended
 * Window Manager Hints (EWMH) specifications; see the README.
 */
#define _GNU_SOURCE
#include <errno.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ipc.h>
#include <sys/select.h>
#include <sys/shm.h>
#include <sys/sysinfo.h>
#include <sys/time.h>
#include <time.h>
#include <unistd.h>

#include <X11/Xatom.h>
#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <X11/cursorfont.h>
#include <X11/extensions/XShm.h>
#include <X11/extensions/shape.h>
#include <X11/keysym.h>

#include "../src/desktop.h"
#include "../src/gfx.h"
#include "../src/lib.h"
#include "linux.h"

static Display *dpy;
static int screen;
static Window root, desk_win, check_win;
static GC gc;
static Cursor cursor;
static int scr_w, scr_h;

/* ---- atoms ------------------------------------------------------------------- */

enum {
    WM_PROTOCOLS, WM_DELETE_WINDOW, WM_TAKE_FOCUS, WM_STATE, WM_CHANGE_STATE, UTF8_STRING,
    NET_SUPPORTED, NET_SUPPORTING_WM_CHECK, NET_WM_NAME, NET_CLIENT_LIST, NET_ACTIVE_WINDOW,
    NET_CLOSE_WINDOW, NET_NUMBER_OF_DESKTOPS, NET_CURRENT_DESKTOP, NET_WORKAREA,
    NET_WM_STATE, NET_WM_STATE_FULLSCREEN, NET_WM_STATE_MAXIMIZED_VERT,
    NET_WM_STATE_MAXIMIZED_HORZ, NET_WM_STATE_HIDDEN, NET_WM_WINDOW_TYPE,
    NET_WM_WINDOW_TYPE_DESKTOP, NET_WM_WINDOW_TYPE_DOCK, NET_WM_WINDOW_TYPE_SPLASH,
    NET_WM_WINDOW_TYPE_NOTIFICATION, NET_WM_WINDOW_TYPE_DIALOG, NET_FRAME_EXTENTS,
    NET_WM_DESKTOP, ATOM_COUNT
};
static const char *const atom_names[ATOM_COUNT] = {
    "WM_PROTOCOLS", "WM_DELETE_WINDOW", "WM_TAKE_FOCUS", "WM_STATE", "WM_CHANGE_STATE",
    "UTF8_STRING", "_NET_SUPPORTED", "_NET_SUPPORTING_WM_CHECK", "_NET_WM_NAME",
    "_NET_CLIENT_LIST", "_NET_ACTIVE_WINDOW", "_NET_CLOSE_WINDOW", "_NET_NUMBER_OF_DESKTOPS",
    "_NET_CURRENT_DESKTOP", "_NET_WORKAREA", "_NET_WM_STATE", "_NET_WM_STATE_FULLSCREEN",
    "_NET_WM_STATE_MAXIMIZED_VERT", "_NET_WM_STATE_MAXIMIZED_HORZ", "_NET_WM_STATE_HIDDEN",
    "_NET_WM_WINDOW_TYPE", "_NET_WM_WINDOW_TYPE_DESKTOP", "_NET_WM_WINDOW_TYPE_DOCK",
    "_NET_WM_WINDOW_TYPE_SPLASH", "_NET_WM_WINDOW_TYPE_NOTIFICATION",
    "_NET_WM_WINDOW_TYPE_DIALOG", "_NET_FRAME_EXTENTS", "_NET_WM_DESKTOP",
};
static Atom atoms[ATOM_COUNT];

/* ---- errors ------------------------------------------------------------------ */

static int another_wm;

/* Programs' windows can vanish at any moment, so requests about them may
 * fail with BadWindow and the like; that is normal for a window manager. */
static int on_x_error(Display *d, XErrorEvent *e)
{
    (void)d;
    if (e->error_code == BadAccess && e->request_code == 2 /* X_ChangeWindowAttributes */)
        another_wm = 1;
    return 0;
}

/* ---- the picture: a shared-memory image of the back buffer -------------------- */

static XImage *img;
static XShmSegmentInfo shm;
static int use_shm;
static uint32_t *shown; /* what the X windows show now (the image's pixels) */

static void image_init(void)
{
    Visual *vis = DefaultVisual(dpy, screen);
    if (DefaultDepth(dpy, screen) < 24 || vis->red_mask != 0xff0000 || vis->blue_mask != 0xff) {
        fprintf(stderr, "skarlet-session: needs a 24-bit TrueColor display\n");
        exit(1);
    }
    if (XShmQueryExtension(dpy)) {
        img = XShmCreateImage(dpy, vis, DefaultDepth(dpy, screen), ZPixmap, 0, &shm, g_w, g_h);
        if (img) {
            shm.shmid = shmget(IPC_PRIVATE, (size_t)img->bytes_per_line * img->height,
                               IPC_CREAT | 0600);
            shm.shmaddr = img->data = shmat(shm.shmid, 0, 0);
            shm.readOnly = False;
            XShmAttach(dpy, &shm);
            XSync(dpy, False);
            shmctl(shm.shmid, IPC_RMID, 0); /* freed when we exit */
            use_shm = 1;
        }
    }
    if (!use_shm) { /* a remote display: plain XPutImage */
        char *data = calloc((size_t)g_w * g_h, 4);
        img = XCreateImage(dpy, vis, DefaultDepth(dpy, screen), ZPixmap, 0, data, g_w, g_h, 32,
                           g_w * 4);
    }
    shown = (uint32_t *)img->data;
}

/* Show the buffer's rectangle (x, y, w, h), in screen coordinates, in window
 * win whose top-left corner is at (wx, wy) on the screen. */
static void put(Window win, int wx, int wy, int x, int y, int w, int h)
{
    if (x < 0)
        w += x, x = 0;
    if (y < 0)
        h += y, y = 0;
    if (x + w > g_w)
        w = g_w - x;
    if (y + h > g_h)
        h = g_h - y;
    if (w <= 0 || h <= 0)
        return;
    if (use_shm)
        XShmPutImage(dpy, win, gc, img, x, y, x - wx, y - wy, (unsigned)w, (unsigned)h, False);
    else
        XPutImage(dpy, win, gc, img, x, y, x - wx, y - wy, (unsigned)w, (unsigned)h);
}

static unsigned isqrt_u(int v)
{
    unsigned r = 0;
    while ((r + 1) * (r + 1) <= (unsigned)MAX(v, 0))
        r++;
    return r;
}

/* Round a window's corners with the X Shape extension, so that what is
 * behind (another program's window, say) shows around them. */
static void shape_round(Window win, int w, int h, int rtop, int rbottom)
{
    XRectangle rects[64];
    int n = 0;
    for (int y = 0; y < rtop && n < 30; y++) {
        int d = rtop - y; /* distance from the corner circle's centre row */
        int in = rtop - (int)(isqrt_u(rtop * rtop * 4 - (2 * d - 1) * (2 * d - 1)) / 2);
        rects[n++] = (XRectangle){ (short)in, (short)y, (unsigned short)MAX(w - 2 * in, 0), 1 };
    }
    rects[n++] = (XRectangle){ 0, (short)rtop, (unsigned short)w,
                               (unsigned short)MAX(h - rtop - rbottom, 0) };
    for (int y = h - rbottom; y < h && n < 63; y++) {
        int d = y - (h - rbottom) + 1;
        int in = rbottom - (int)(isqrt_u(rbottom * rbottom * 4 - (2 * d - 1) * (2 * d - 1)) / 2);
        rects[n++] = (XRectangle){ (short)in, (short)y, (unsigned short)MAX(w - 2 * in, 0), 1 };
    }
    XShapeCombineRectangles(dpy, win, ShapeBounding, 0, 0, rects, n, ShapeSet, Unsorted);
}

static Window make_window(int x, int y, int w, int h)
{
    XSetWindowAttributes a = { 0 };
    a.override_redirect = True;
    a.background_pixmap = None; /* we paint everything ourselves */
    a.event_mask = ExposureMask | ButtonPressMask | ButtonReleaseMask | ButtonMotionMask |
                   KeyPressMask | KeyReleaseMask | SubstructureNotifyMask;
    a.cursor = cursor;
    return XCreateWindow(dpy, root, x, y, (unsigned)MAX(w, 1), (unsigned)MAX(h, 1), 0,
                         CopyFromParent, InputOutput, CopyFromParent,
                         CWOverrideRedirect | CWBackPixmap | CWEventMask | CWCursor, &a);
}

/* ---- surfaces: the X windows that show SkarletOS windows ----------------------- */

struct surface {
    int used;
    int pid;            /* the SkarletOS window */
    Window frame;
    Window client;      /* another program's window inside the frame, or 0 */
    int x, y, w, h;     /* frame geometry as last applied */
    int cw, ch;         /* client size as last applied */
    int shape_w, shape_h, shape_r;
    int mapped;
    int fullscreen;
    int protocols_delete, protocols_focus;
    int close_asked;
    int ignore_unmaps;  /* unmap events we caused ourselves */
};

#define MAX_SURFACES 64
static struct surface surfaces[MAX_SURFACES];

/* Programs' windows that SkarletOS does not frame (docks, splash screens,
 * or windows beyond MAX_WIN); they are kept above the desktop. */
#define MAX_PLAIN 32
static Window plain[MAX_PLAIN];
static int nplain;

static struct surface *surface_by_pid(int pid)
{
    for (int i = 0; i < MAX_SURFACES; i++)
        if (surfaces[i].used && surfaces[i].pid == pid)
            return &surfaces[i];
    return 0;
}

static struct surface *surface_by_client(Window c)
{
    for (int i = 0; c && i < MAX_SURFACES; i++)
        if (surfaces[i].used && surfaces[i].client == c)
            return &surfaces[i];
    return 0;
}

static struct surface *surface_by_frame(Window f)
{
    for (int i = 0; f && i < MAX_SURFACES; i++)
        if (surfaces[i].used && surfaces[i].frame == f)
            return &surfaces[i];
    return 0;
}

static struct surface *new_surface(int pid, Window client)
{
    for (int i = 0; i < MAX_SURFACES; i++)
        if (!surfaces[i].used) {
            struct surface *s = &surfaces[i];
            memset(s, 0, sizeof *s);
            s->used = 1;
            s->pid = pid;
            s->client = client;
            s->frame = make_window(0, 0, 1, 1);
            s->x = s->y = -100000; /* force the first geometry update */
            return s;
        }
    return 0;
}

static void free_surface(struct surface *s)
{
    if (s->frame)
        XDestroyWindow(dpy, s->frame);
    s->used = 0;
}

/* ---- layers: the panel, popups and notifications ------------------------------ */

static Window layer_win[MAX_LAYERS_X];
static struct layer layer_now[MAX_LAYERS_X];
static int layer_mapped[MAX_LAYERS_X];

/* ---- programs' windows: managing them ------------------------------------------ */

static void set_wm_state(Window w, long state)
{
    long data[2] = { state, None };
    XChangeProperty(dpy, w, atoms[WM_STATE], atoms[WM_STATE], 32, PropModeReplace,
                    (unsigned char *)data, 2);
}

static int get_atom_prop(Window w, Atom prop, Atom *out, int max)
{
    Atom type;
    int format;
    unsigned long n = 0, after;
    unsigned char *data = 0;
    if (XGetWindowProperty(dpy, w, prop, 0, max, False, XA_ATOM, &type, &format, &n, &after,
                           &data) != Success || !data)
        return 0;
    int count = 0;
    for (unsigned long i = 0; i < n && count < max; i++)
        out[count++] = ((Atom *)data)[i];
    XFree(data);
    return count;
}

/* Turn a UTF-8 title into the ASCII our fonts have: dashes and quotes become
 * their ASCII look-alikes, anything else a '?'. */
static void title_ascii(const unsigned char *in, char *out, int size)
{
    int n = 0;
    while (*in && n < size - 1) {
        unsigned c = *in;
        if (c < 0x80) {
            out[n++] = c >= 32 ? (char)c : ' ';
            in++;
            continue;
        }
        int len = c >= 0xf0 ? 4 : c >= 0xe0 ? 3 : 2;
        unsigned cp = c & (0x3f >> (len - 1));
        for (int i = 1; i < len && in[i]; i++)
            cp = (cp << 6) | (in[i] & 0x3f);
        for (int i = 0; i < len && *in; i++)
            in++;
        char r = '?';
        if (cp == 0x2013 || cp == 0x2014 || cp == 0x2212)
            r = '-';
        else if (cp == 0x2018 || cp == 0x2019)
            r = '\'';
        else if (cp == 0x201c || cp == 0x201d)
            r = '"';
        else if (cp == 0x2026 && n < size - 3) {
            out[n++] = '.', out[n++] = '.';
            r = '.';
        } else if (cp == 0xa0)
            r = ' ';
        out[n++] = r;
    }
    out[n] = 0;
}

static void read_title(Window w, char *out, int size)
{
    Atom type;
    int format;
    unsigned long n = 0, after;
    unsigned char *data = 0;
    out[0] = 0;
    if (XGetWindowProperty(dpy, w, atoms[NET_WM_NAME], 0, 256, False, atoms[UTF8_STRING], &type,
                           &format, &n, &after, &data) == Success && data && n) {
        title_ascii(data, out, size);
        XFree(data);
        return;
    }
    if (data)
        XFree(data);
    char *name = 0;
    if (XFetchName(dpy, w, &name) && name) {
        title_ascii((unsigned char *)name, out, size);
        XFree(name);
    }
}

static void read_protocols(struct surface *s)
{
    Atom *protos;
    int n;
    s->protocols_delete = s->protocols_focus = 0;
    if (XGetWMProtocols(dpy, s->client, &protos, &n)) {
        for (int i = 0; i < n; i++) {
            if (protos[i] == atoms[WM_DELETE_WINDOW])
                s->protocols_delete = 1;
            if (protos[i] == atoms[WM_TAKE_FOCUS])
                s->protocols_focus = 1;
        }
        XFree(protos);
    }
}

static void send_protocol(Window w, Atom proto)
{
    XEvent e = { 0 };
    e.xclient.type = ClientMessage;
    e.xclient.window = w;
    e.xclient.message_type = atoms[WM_PROTOCOLS];
    e.xclient.format = 32;
    e.xclient.data.l[0] = (long)proto;
    e.xclient.data.l[1] = CurrentTime;
    XSendEvent(dpy, w, False, NoEventMask, &e);
}

/* Tell a program where its window is (ICCCM 4.1.5: after we move it). */
static void send_configure(struct surface *s, int x, int y, int w, int h)
{
    XEvent e = { 0 };
    e.xconfigure.type = ConfigureNotify;
    e.xconfigure.event = s->client;
    e.xconfigure.window = s->client;
    e.xconfigure.x = x;
    e.xconfigure.y = y;
    e.xconfigure.width = w;
    e.xconfigure.height = h;
    e.xconfigure.border_width = 0;
    e.xconfigure.above = None;
    e.xconfigure.override_redirect = False;
    XSendEvent(dpy, s->client, False, StructureNotifyMask, &e);
}

static void keep_plain(Window w)
{
    for (int i = 0; i < nplain; i++)
        if (plain[i] == w)
            return;
    if (nplain < MAX_PLAIN)
        plain[nplain++] = w;
}

static void forget_plain(Window w)
{
    for (int i = 0; i < nplain; i++)
        if (plain[i] == w) {
            plain[i] = plain[--nplain];
            return;
        }
}

/* A program wants to show a window: frame it. */
static void manage(Window c)
{
    XWindowAttributes wa;
    if (!XGetWindowAttributes(dpy, c, &wa) || wa.override_redirect || surface_by_client(c))
        return;

    /* Docks, desktops, splash screens and notifications are shown as they are. */
    Atom types[8];
    int ntypes = get_atom_prop(c, atoms[NET_WM_WINDOW_TYPE], types, 8);
    for (int i = 0; i < ntypes; i++)
        if (types[i] == atoms[NET_WM_WINDOW_TYPE_DOCK] ||
            types[i] == atoms[NET_WM_WINDOW_TYPE_DESKTOP] ||
            types[i] == atoms[NET_WM_WINDOW_TYPE_SPLASH] ||
            types[i] == atoms[NET_WM_WINDOW_TYPE_NOTIFICATION]) {
            keep_plain(c);
            XMapWindow(dpy, c);
            return;
        }

    char title[48];
    read_title(c, title, sizeof title);
    int cw = MIN(wa.width, g_w - 2), ch = MIN(wa.height, DESK_BOTTOM - TITLE_H - 2);
    struct window *w = wm_open_external((long)c, title, cw, ch);
    if (!w) {
        keep_plain(c);
        XMapWindow(dpy, c);
        return;
    }
    struct surface *s = new_surface(w->pid, c);
    if (!s) {
        wm_remove(w);
        keep_plain(c);
        XMapWindow(dpy, c);
        return;
    }

    /* Dialogs open in the middle of the window they belong to; windows that
     * ask for a position (and fit on the screen) get it. */
    Window parent = None;
    XSizeHints hints;
    long supplied;
    if (XGetTransientForHint(dpy, c, &parent) && parent) {
        struct surface *ps = surface_by_client(parent);
        struct window *pw = ps ? wm_by_pid(ps->pid) : 0;
        if (pw) {
            w->x = pw->x + (pw->w - w->w) / 2;
            w->y = pw->y + (pw->h - w->h) / 2;
        }
    } else if (XGetWMNormalHints(dpy, c, &hints, &supplied) && (hints.flags & USPosition) &&
               wa.x > 0 && wa.y > 0) {
        w->x = wa.x - 1;
        w->y = wa.y - TITLE_H - 1;
    }
    w->x = MAX(0, MIN(w->x, g_w - w->w));
    w->y = MAX(0, MIN(w->y, DESK_BOTTOM - w->h));

    read_protocols(s);
    if (wa.map_state == IsViewable)
        s->ignore_unmaps++; /* reparenting a shown window unmaps it once */
    XSelectInput(dpy, c, PropertyChangeMask | StructureNotifyMask);
    XAddToSaveSet(dpy, c); /* if we crash, the program's window survives */
    XSetWindowBorderWidth(dpy, c, 0);
    XReparentWindow(dpy, c, s->frame, 1, TITLE_H + 1);
    /* Clicks on an inactive window first activate it (see on_button). */
    XGrabButton(dpy, AnyButton, AnyModifier, c, False, ButtonPressMask, GrabModeSync,
                GrabModeAsync, None, None);
    long extents[4] = { 1, 1, TITLE_H + 1, 1 };
    XChangeProperty(dpy, c, atoms[NET_FRAME_EXTENTS], XA_CARDINAL, 32, PropModeReplace,
                    (unsigned char *)extents, 4);
    XMapWindow(dpy, c);
    set_wm_state(c, NormalState);
    wm_activate(w);
}

/* A program's window went away (or was withdrawn): forget it. */
static void unmanage(struct surface *s, int destroyed)
{
    struct window *w = wm_by_pid(s->pid);
    if (w)
        wm_remove(w);
    if (!destroyed) {
        XUngrabButton(dpy, AnyButton, AnyModifier, s->client);
        XReparentWindow(dpy, s->client, root, s->x + 1, s->y + TITLE_H + 1);
        XRemoveFromSaveSet(dpy, s->client);
        set_wm_state(s->client, WithdrawnState);
    }
    free_surface(s);
    g_ws.need_redraw = 1;
}

void plat_window_close(long ext)
{
    struct surface *s = surface_by_client((Window)ext);
    if (!s)
        return;
    if (s->protocols_delete)
        send_protocol(s->client, atoms[WM_DELETE_WINDOW]);
    else
        XKillClient(dpy, s->client);
    XFlush(dpy);
}

/* ---- keeping the X windows in step with the desktop ---------------------------- */

static uint32_t *prev;        /* the frame before (to find what changed) */
static Window last_stack[MAX_SURFACES + MAX_LAYERS_X + MAX_PLAIN + 1];
static int last_nstack;
static Window last_focus;
static long last_active = -1;
static int last_desk = -1;

static int window_shown(const struct window *w)
{
    return g_ws.phase == PHASE_DESKTOP && !g_ws.dashboard && wm_visible(w);
}

static void sync_surfaces(void)
{
    /* Windows that SkarletOS no longer has. */
    for (int i = 0; i < MAX_SURFACES; i++) {
        struct surface *s = &surfaces[i];
        if (!s->used || wm_by_pid(s->pid))
            continue;
        if (!s->client) {
            free_surface(s);
        } else if (!s->close_asked) {
            /* Logging out closed the windows; ask the program to quit. */
            s->close_asked = 1;
            XUnmapWindow(dpy, s->frame);
            s->mapped = 0;
            plat_window_close((long)s->client);
        }
    }
    struct window *all[MAX_WIN];
    int n = wm_list_all(all, MAX_WIN);
    for (int i = 0; i < n; i++) {
        struct window *w = all[i];
        struct surface *s = surface_by_pid(w->pid);
        if (!s && !w->ext)
            s = new_surface(w->pid, 0);
        if (!s)
            continue;
        int shown_now = window_shown(w);
        int fx = w->x, fy = w->y, fw = w->w, fh = w->h;
        int cx = 1, cy = TITLE_H + 1, cw = w->w - 2, ch = w->h - TITLE_H - 2;
        if (s->fullscreen && shown_now) {
            fx = fy = 0, fw = g_w, fh = g_h;
            cx = cy = 0, cw = g_w, ch = g_h;
        }
        int moved = fx != s->x || fy != s->y || fw != s->w || fh != s->h;
        if (moved) {
            XMoveResizeWindow(dpy, s->frame, fx, fy, (unsigned)MAX(fw, 1), (unsigned)MAX(fh, 1));
            if (s->client) {
                XMoveResizeWindow(dpy, s->client, cx, cy, (unsigned)MAX(cw, 1),
                                  (unsigned)MAX(ch, 1));
                send_configure(s, fx + cx, fy + cy, cw, ch);
            }
            s->x = fx, s->y = fy, s->w = fw, s->h = fh, s->cw = cw, s->ch = ch;
        }
        /* Rounded corners, except when maximized or full screen; another
         * program's window has square bottom corners (it fills them). */
        int r = (w->maximized || s->fullscreen) ? 0 : 10;
        if (fw != s->shape_w || fh != s->shape_h || r != s->shape_r) {
            shape_round(s->frame, fw, fh, r, s->client ? 0 : r);
            s->shape_w = fw, s->shape_h = fh, s->shape_r = r;
        }
        if (shown_now && !s->mapped) {
            XMapWindow(dpy, s->frame);
            s->mapped = 1;
            if (s->client)
                set_wm_state(s->client, NormalState);
            moved = 1;
        } else if (!shown_now && s->mapped) {
            XUnmapWindow(dpy, s->frame);
            s->mapped = 0;
            if (s->client)
                set_wm_state(s->client, w->minimized ? IconicState : NormalState);
        }
        if (moved && s->mapped)
            put(s->frame, fx, fy, fx, fy, fw, fh);
        if (s->client) {
            /* Keep the program's idea of its desktop and title up to date. */
            long d = w->desk;
            XChangeProperty(dpy, s->client, atoms[NET_WM_DESKTOP], XA_CARDINAL, 32,
                            PropModeReplace, (unsigned char *)&d, 1);
        }
    }
}

static void sync_layers(void)
{
    struct layer l[MAX_LAYERS_X];
    int n = ws_layers(l, MAX_LAYERS_X);
    for (int i = 0; i < MAX_LAYERS_X; i++) {
        if (i >= n) {
            if (layer_mapped[i]) {
                XUnmapWindow(dpy, layer_win[i]);
                layer_mapped[i] = 0;
            }
            continue;
        }
        struct layer *o = &layer_now[i];
        int changed = !layer_mapped[i] || o->x != l[i].x || o->y != l[i].y || o->w != l[i].w ||
                      o->h != l[i].h || o->r != l[i].r;
        if (changed) {
            XMoveResizeWindow(dpy, layer_win[i], l[i].x, l[i].y, (unsigned)MAX(l[i].w, 1),
                              (unsigned)MAX(l[i].h, 1));
            shape_round(layer_win[i], l[i].w, l[i].h, l[i].r, l[i].r);
            *o = l[i];
            if (!layer_mapped[i]) {
                XMapWindow(dpy, layer_win[i]);
                layer_mapped[i] = 1;
            }
            put(layer_win[i], l[i].x, l[i].y, l[i].x, l[i].y, l[i].w, l[i].h);
        }
    }
}

/* Stack everything: full-screen windows, layers, frames (top first), the
 * programs' own unframed windows, and the desktop at the bottom. */
static void sync_stack(void)
{
    Window order[MAX_SURFACES + MAX_LAYERS_X + MAX_PLAIN + 1];
    int n = 0;
    struct window *all[MAX_WIN];
    int nw = wm_list_all(all, MAX_WIN);
    for (int i = nw - 1; i >= 0; i--) {
        struct surface *s = surface_by_pid(all[i]->pid);
        if (s && s->mapped && s->fullscreen)
            order[n++] = s->frame;
    }
    for (int i = MAX_LAYERS_X - 1; i >= 0; i--)
        if (layer_mapped[i])
            order[n++] = layer_win[i];
    for (int i = 0; i < nplain; i++)
        order[n++] = plain[i];
    for (int i = nw - 1; i >= 0; i--) {
        struct surface *s = surface_by_pid(all[i]->pid);
        if (s && s->mapped && !s->fullscreen)
            order[n++] = s->frame;
    }
    order[n++] = desk_win;
    if (n != last_nstack || memcmp(order, last_stack, sizeof order[0] * n) != 0) {
        XRestackWindows(dpy, order, n);
        memcpy(last_stack, order, sizeof order[0] * n);
        last_nstack = n;
    }
}

/* Give the keyboard to the active program's window, or to SkarletOS. */
static void sync_focus(void)
{
    struct window *w = g_ws.phase == PHASE_DESKTOP && !ws_popup_open() ? wm_focused() : 0;
    struct surface *s = w && w->ext ? surface_by_pid(w->pid) : 0;
    Window target = s && s->mapped ? s->client : desk_win;
    if (target != last_focus) {
        XSetInputFocus(dpy, target, RevertToPointerRoot, CurrentTime);
        if (s && s->protocols_focus)
            send_protocol(s->client, atoms[WM_TAKE_FOCUS]);
        last_focus = target;
    }
    long active = s ? (long)s->client : (long)None;
    if (active != last_active) {
        XChangeProperty(dpy, root, atoms[NET_ACTIVE_WINDOW], XA_WINDOW, 32, PropModeReplace,
                        (unsigned char *)&active, 1);
        last_active = active;
    }
    if (g_ws.desk != last_desk) {
        long d = g_ws.desk;
        XChangeProperty(dpy, root, atoms[NET_CURRENT_DESKTOP], XA_CARDINAL, 32, PropModeReplace,
                        (unsigned char *)&d, 1);
        last_desk = g_ws.desk;
    }
    /* _NET_CLIENT_LIST: the programs' windows we manage, oldest first. */
    Window list[MAX_SURFACES];
    int n = 0;
    for (int i = 0; i < MAX_SURFACES; i++)
        if (surfaces[i].used && surfaces[i].client)
            list[n++] = surfaces[i].client;
    XChangeProperty(dpy, root, atoms[NET_CLIENT_LIST], XA_WINDOW, 32, PropModeReplace,
                    (unsigned char *)list, n);
}

/* Copy what changed in the back buffer to the X windows that show it. */
static void push_changes(const uint32_t *px)
{
    int x0 = g_w, y0 = g_h, x1 = -1, y1 = -1;
    for (int y = 0; y < g_h; y++) {
        const uint32_t *a = px + (size_t)y * g_w, *b = prev + (size_t)y * g_w;
        if (memcmp(a, b, (size_t)g_w * 4) == 0)
            continue;
        int l = 0, r = g_w - 1;
        while (a[l] == b[l])
            l++;
        while (a[r] == b[r])
            r--;
        x0 = MIN(x0, l), x1 = MAX(x1, r), y0 = MIN(y0, y), y1 = MAX(y1, y);
    }
    if (x1 < 0)
        return;
    for (int y = y0; y <= y1; y++) {
        memcpy(prev + (size_t)y * g_w + x0, px + (size_t)y * g_w + x0, (size_t)(x1 - x0 + 1) * 4);
        memcpy(shown + (size_t)y * g_w + x0, px + (size_t)y * g_w + x0, (size_t)(x1 - x0 + 1) * 4);
    }
    int w = x1 - x0 + 1, h = y1 - y0 + 1;
    put(desk_win, 0, 0, x0, y0, w, h);
    for (int i = 0; i < MAX_SURFACES; i++) {
        struct surface *s = &surfaces[i];
        if (s->used && s->mapped && !s->fullscreen)
            put(s->frame, s->x, s->y, MAX(x0, s->x), MAX(y0, s->y),
                MIN(x1 + 1, s->x + s->w) - MAX(x0, s->x), MIN(y1 + 1, s->y + s->h) - MAX(y0, s->y));
    }
    for (int i = 0; i < MAX_LAYERS_X; i++) {
        struct layer *l = &layer_now[i];
        if (layer_mapped[i])
            put(layer_win[i], l->x, l->y, MAX(x0, l->x), MAX(y0, l->y),
                MIN(x1 + 1, l->x + l->w) - MAX(x0, l->x), MIN(y1 + 1, l->y + l->h) - MAX(y0, l->y));
    }
}

void plat_present(const uint32_t *px, int w, int h)
{
    (void)w, (void)h;
    /* The buffer first, so windows mapped below show the new picture. */
    push_changes(px);
    sync_surfaces();
    sync_layers();
    sync_stack();
    sync_focus();
    if (use_shm)
        XSync(dpy, False); /* the server has read the image before we change it */
    else
        XFlush(dpy);
}

/* ---- input ----------------------------------------------------------------------- */

#define KEYQ 128
static struct key keyq[KEYQ];
static int kq_head, kq_tail;

int plat_key_poll(struct key *k)
{
    if (kq_head == kq_tail)
        return 0;
    *k = keyq[kq_head];
    kq_head = (kq_head + 1) % KEYQ;
    return 1;
}

static void push_key(int code, int mods)
{
    int next = (kq_tail + 1) % KEYQ;
    if (next == kq_head)
        return;
    keyq[kq_tail] = (struct key){ code, mods };
    kq_tail = next;
}

static int key_code(KeySym ks)
{
    switch (ks) {
    case XK_Return: case XK_KP_Enter: return K_ENTER;
    case XK_Escape: return K_ESC;
    case XK_Tab: case XK_ISO_Left_Tab: return K_TAB;
    case XK_BackSpace: return K_BACKSPACE;
    case XK_Up: case XK_KP_Up: return K_UP;
    case XK_Down: case XK_KP_Down: return K_DOWN;
    case XK_Left: case XK_KP_Left: return K_LEFT;
    case XK_Right: case XK_KP_Right: return K_RIGHT;
    case XK_Home: case XK_KP_Home: return K_HOME;
    case XK_End: case XK_KP_End: return K_END;
    case XK_Page_Up: case XK_KP_Page_Up: return K_PGUP;
    case XK_Page_Down: case XK_KP_Page_Down: return K_PGDN;
    case XK_Insert: case XK_KP_Insert: return K_INSERT;
    case XK_Delete: case XK_KP_Delete: return K_DELETE;
    case XK_Super_L: case XK_Super_R: return K_META;
    }
    if (ks >= XK_F1 && ks <= XK_F12)
        return K_F1 + (int)(ks - XK_F1);
    return 0;
}

static void on_key(XKeyEvent *e)
{
    int mods = (e->state & ShiftMask ? MOD_SHIFT : 0) | (e->state & ControlMask ? MOD_CTRL : 0) |
               (e->state & Mod1Mask ? MOD_ALT : 0);
    char buf[8];
    KeySym ks;
    int n = XLookupString(e, buf, sizeof buf, &ks, 0);
    int code = key_code(ks);
    if (!code) {
        /* With Ctrl or Alt held, SkarletOS wants the plain letter ("c", not ^C). */
        KeySym base = XLookupKeysym(e, 0);
        if ((mods & (MOD_CTRL | MOD_ALT)) && base >= 0x20 && base < 0x7f)
            code = (int)base;
        else if (n == 1 && (unsigned char)buf[0] >= 32 && (unsigned char)buf[0] < 127)
            code = (unsigned char)buf[0];
    }
    if (code == K_META && mods)
        return;
    if (code)
        push_key(code, mods);
}

/* Shortcuts SkarletOS needs even while another program has the keyboard. */
static const struct { KeySym ks; unsigned mods; } grabs[] = {
    { XK_F1, Mod1Mask }, { XK_F2, Mod1Mask }, { XK_F4, Mod1Mask }, { XK_F7, Mod1Mask },
    { XK_F12, Mod1Mask }, { XK_Tab, Mod1Mask }, { XK_F1, ControlMask }, { XK_F2, ControlMask },
    { XK_F3, ControlMask }, { XK_F4, ControlMask }, { XK_F12, ControlMask },
    { XK_Escape, ControlMask }, { XK_Super_L, 0 }, { XK_Super_R, 0 },
};

static void grab_keys(void)
{
    /* The same shortcut with Num Lock and/or Caps Lock on is another grab. */
    static const unsigned extra[] = { 0, Mod2Mask, LockMask, Mod2Mask | LockMask };
    for (int i = 0; i < ARRAY_LEN(grabs); i++) {
        KeyCode kc = XKeysymToKeycode(dpy, grabs[i].ks);
        if (!kc)
            continue;
        for (int j = 0; j < ARRAY_LEN(extra); j++)
            XGrabKey(dpy, kc, grabs[i].mods | extra[j], root, True, GrabModeAsync, GrabModeAsync);
    }
}

static Time last_press_time;
static int last_press_x, last_press_y;
static unsigned last_press_button;

static void on_button(XButtonEvent *e)
{
    /* A click on another program's window: activate it, then let the
     * program have the click too ("replay" it). */
    struct surface *s = surface_by_client(e->window);
    if (s) {
        if (e->type == ButtonPress) {
            struct window *w = wm_by_pid(s->pid);
            if (w && w != wm_focused())
                wm_activate(w);
            if (ws_popup_open())
                ws_key((struct key){ K_ESC, 0 }); /* closes the popup */
            g_ws.need_redraw = 1;
        }
        XAllowEvents(dpy, ReplayPointer, e->time);
        return;
    }
    struct mouse m = { 0 };
    m.x = e->x_root;
    m.y = e->y_root;
    m.button = (int)e->button;
    m.clicks = 1;
    if (e->type == ButtonPress) {
        if (e->button == 4 || e->button == 5) {
            m.type = MOUSE_WHEEL;
        } else {
            m.type = MOUSE_DOWN;
            if (e->button == last_press_button && e->time - last_press_time < 400 &&
                abs(e->x_root - last_press_x) < 5 && abs(e->y_root - last_press_y) < 5)
                m.clicks = 2;
            last_press_time = m.clicks == 2 ? 0 : e->time;
            last_press_x = e->x_root, last_press_y = e->y_root;
            last_press_button = e->button;
        }
    } else {
        if (e->button == 4 || e->button == 5)
            return;
        m.type = MOUSE_UP;
    }
    ws_mouse(m);
}

static void on_motion(XMotionEvent *e)
{
    /* Only the latest position matters. */
    XEvent next;
    while (XCheckTypedWindowEvent(dpy, e->window, MotionNotify, &next))
        e = &next.xmotion;
    ws_mouse((struct mouse){ MOUSE_MOVE, e->x_root, e->y_root, 0, 0 });
}

/* ---- X events -------------------------------------------------------------------- */

static void on_configure_request(XConfigureRequestEvent *e)
{
    struct surface *s = surface_by_client(e->window);
    struct window *w = s ? wm_by_pid(s->pid) : 0;
    if (!w) {
        /* Not ours to decide: do what the program asks. */
        XWindowChanges ch = { e->x, e->y, e->width, e->height, e->border_width, e->above,
                              e->detail };
        XConfigureWindow(dpy, e->window, (unsigned)e->value_mask & ~(CWSibling | CWStackMode),
                         &ch);
        return;
    }
    if (!w->maximized && !s->fullscreen) {
        if (e->value_mask & CWWidth)
            w->w = MIN(e->width, g_w - 2) + 2;
        if (e->value_mask & CWHeight)
            w->h = MIN(e->height, DESK_BOTTOM - TITLE_H - 2) + TITLE_H + 2;
        if (e->value_mask & CWX)
            w->x = MAX(0, MIN(e->x - 1, g_w - w->w));
        if (e->value_mask & CWY)
            w->y = MAX(0, MIN(e->y - TITLE_H - 1, DESK_BOTTOM - w->h));
    }
    /* Always answer, even if nothing changed (ICCCM 4.1.5). */
    s->x = -100000;
    g_ws.need_redraw = 1;
}

static void set_fullscreen(struct surface *s, int on)
{
    s->fullscreen = on;
    s->x = -100000;
    Atom state = atoms[NET_WM_STATE_FULLSCREEN];
    XChangeProperty(dpy, s->client, atoms[NET_WM_STATE], XA_ATOM, 32, PropModeReplace,
                    (unsigned char *)&state, on ? 1 : 0);
    g_ws.need_redraw = 1;
}

static void on_client_message(XClientMessageEvent *e)
{
    struct surface *s = surface_by_client(e->window);
    struct window *w = s ? wm_by_pid(s->pid) : 0;
    if (!w)
        return;
    if (e->message_type == atoms[NET_ACTIVE_WINDOW]) {
        wm_activate(w);
    } else if (e->message_type == atoms[NET_CLOSE_WINDOW]) {
        plat_window_close((long)s->client);
    } else if (e->message_type == atoms[WM_CHANGE_STATE] && e->data.l[0] == IconicState) {
        wm_minimize(w);
    } else if (e->message_type == atoms[NET_WM_STATE]) {
        /* data: action (0 remove, 1 add, 2 toggle), then up to two states. */
        long action = e->data.l[0];
        for (int i = 1; i <= 2; i++) {
            Atom a = (Atom)e->data.l[i];
            if (a == atoms[NET_WM_STATE_FULLSCREEN]) {
                set_fullscreen(s, action == 2 ? !s->fullscreen : action == 1);
            } else if (a == atoms[NET_WM_STATE_MAXIMIZED_VERT] ||
                       a == atoms[NET_WM_STATE_MAXIMIZED_HORZ]) {
                int want = action == 2 ? !w->maximized : action == 1;
                if (want != w->maximized)
                    wm_toggle_maximize(w);
                break; /* both atoms usually come together */
            }
        }
    }
    g_ws.need_redraw = 1;
}

static void on_expose(XExposeEvent *e)
{
    int wx = 0, wy = 0;
    struct surface *s = surface_by_frame(e->window);
    if (s) {
        wx = s->x, wy = s->y;
    } else {
        for (int i = 0; i < MAX_LAYERS_X; i++)
            if (layer_win[i] == e->window)
                wx = layer_now[i].x, wy = layer_now[i].y;
    }
    put(e->window, wx, wy, wx + e->x, wy + e->y, e->width, e->height);
}

static void handle_event(XEvent *e)
{
    struct surface *s;
    switch (e->type) {
    case KeyPress: on_key(&e->xkey); break;
    case ButtonPress: case ButtonRelease: on_button(&e->xbutton); break;
    case MotionNotify: on_motion(&e->xmotion); break;
    case Expose: on_expose(&e->xexpose); break;
    case MapRequest: manage(e->xmaprequest.window); break;
    case ConfigureRequest: on_configure_request(&e->xconfigurerequest); break;
    case ClientMessage: on_client_message(&e->xclient); break;
    case UnmapNotify:
        s = surface_by_client(e->xunmap.window);
        if (s && e->xunmap.event == s->client) {
            if (s->ignore_unmaps > 0)
                s->ignore_unmaps--;
            else
                unmanage(s, 0); /* the program withdrew its window */
        }
        forget_plain(e->xunmap.window);
        break;
    case DestroyNotify:
        s = surface_by_client(e->xdestroywindow.window);
        if (s)
            unmanage(s, 1);
        forget_plain(e->xdestroywindow.window);
        break;
    case PropertyNotify:
        s = surface_by_client(e->xproperty.window);
        if (s && (e->xproperty.atom == XA_WM_NAME || e->xproperty.atom == atoms[NET_WM_NAME])) {
            struct window *w = wm_by_pid(s->pid);
            if (w) {
                read_title(s->client, w->title, sizeof w->title);
                g_ws.need_redraw = 1;
            }
        } else if (s && e->xproperty.atom == atoms[WM_PROTOCOLS]) {
            read_protocols(s);
        }
        break;
    }
}

/* ---- the platform: clock, memory, power -------------------------------------------- */

void plat_display_size(int *w, int *h)
{
    *w = MIN(scr_w, GFX_MAX_W);
    *h = MIN(scr_h, GFX_MAX_H);
}

void plat_time(struct datetime *t)
{
    time_t now = time(0);
    struct tm tm;
    localtime_r(&now, &tm);
    t->year = tm.tm_year + 1900;
    t->month = tm.tm_mon + 1;
    t->day = tm.tm_mday;
    t->hour = tm.tm_hour;
    t->minute = tm.tm_min;
    t->second = tm.tm_sec;
}

uint32_t plat_mem_kib(void)
{
    struct sysinfo si;
    if (sysinfo(&si) != 0)
        return 0;
    return (uint32_t)((unsigned long long)si.totalram * si.mem_unit / 1024);
}

void plat_reboot(void)
{
    if (linux_run("systemctl reboot") < 0)
        svc_notify("SkarletOS", "Could not restart the computer.");
}

void plat_poweroff(void)
{
    if (linux_run("systemctl poweroff") < 0)
        svc_notify("SkarletOS", "Could not turn off the computer.");
}

/* ---- start-up ---------------------------------------------------------------------- */

static void ewmh_init(void)
{
    XInternAtoms(dpy, (char **)atom_names, ATOM_COUNT, False, atoms);
    /* A small window that names the window manager (EWMH "supporting WM check"). */
    check_win = XCreateSimpleWindow(dpy, root, -1, -1, 1, 1, 0, 0, 0);
    long cw = (long)check_win;
    XChangeProperty(dpy, root, atoms[NET_SUPPORTING_WM_CHECK], XA_WINDOW, 32, PropModeReplace,
                    (unsigned char *)&cw, 1);
    XChangeProperty(dpy, check_win, atoms[NET_SUPPORTING_WM_CHECK], XA_WINDOW, 32,
                    PropModeReplace, (unsigned char *)&cw, 1);
    XChangeProperty(dpy, check_win, atoms[NET_WM_NAME], atoms[UTF8_STRING], 8, PropModeReplace,
                    (unsigned char *)"SkarletOS", 9);
    Atom supported[] = {
        atoms[NET_SUPPORTED], atoms[NET_SUPPORTING_WM_CHECK], atoms[NET_WM_NAME],
        atoms[NET_CLIENT_LIST], atoms[NET_ACTIVE_WINDOW], atoms[NET_CLOSE_WINDOW],
        atoms[NET_NUMBER_OF_DESKTOPS], atoms[NET_CURRENT_DESKTOP], atoms[NET_WORKAREA],
        atoms[NET_WM_STATE], atoms[NET_WM_STATE_FULLSCREEN], atoms[NET_WM_STATE_MAXIMIZED_VERT],
        atoms[NET_WM_STATE_MAXIMIZED_HORZ], atoms[NET_WM_WINDOW_TYPE], atoms[NET_FRAME_EXTENTS],
        atoms[NET_WM_DESKTOP],
    };
    XChangeProperty(dpy, root, atoms[NET_SUPPORTED], XA_ATOM, 32, PropModeReplace,
                    (unsigned char *)supported, ARRAY_LEN(supported));
    long desks = NUM_DESKS;
    XChangeProperty(dpy, root, atoms[NET_NUMBER_OF_DESKTOPS], XA_CARDINAL, 32, PropModeReplace,
                    (unsigned char *)&desks, 1);
    long area[4 * NUM_DESKS];
    for (int i = 0; i < NUM_DESKS; i++) {
        area[i * 4] = 0, area[i * 4 + 1] = 0;
        area[i * 4 + 2] = g_w, area[i * 4 + 3] = DESK_BOTTOM;
    }
    XChangeProperty(dpy, root, atoms[NET_WORKAREA], XA_CARDINAL, 32, PropModeReplace,
                    (unsigned char *)area, 4 * NUM_DESKS);
}

/* Windows that were already there when we started (say, after a restart). */
static void adopt_existing(void)
{
    Window r, p, *kids = 0;
    unsigned n = 0;
    if (!XQueryTree(dpy, root, &r, &p, &kids, &n))
        return;
    for (unsigned i = 0; i < n; i++) {
        XWindowAttributes wa;
        if (kids[i] == desk_win || kids[i] == check_win || surface_by_frame(kids[i]))
            continue;
        if (XGetWindowAttributes(dpy, kids[i], &wa) && !wa.override_redirect &&
            wa.map_state == IsViewable)
            manage(kids[i]);
    }
    if (kids)
        XFree(kids);
}

int main(int argc, char **argv)
{
    (void)argc, (void)argv;
    signal(SIGCHLD, SIG_IGN); /* programs we start are reaped automatically */
    signal(SIGPIPE, SIG_IGN);
    dpy = XOpenDisplay(0);
    if (!dpy) {
        fprintf(stderr, "skarlet-session: cannot open the X display (is DISPLAY set?)\n");
        return 1;
    }
    screen = DefaultScreen(dpy);
    root = RootWindow(dpy, screen);
    scr_w = DisplayWidth(dpy, screen);
    scr_h = DisplayHeight(dpy, screen);

    XSetErrorHandler(on_x_error);
    XSelectInput(dpy, root, SubstructureRedirectMask | SubstructureNotifyMask |
                            PropertyChangeMask | KeyPressMask);
    XSync(dpy, False);
    if (another_wm) {
        fprintf(stderr, "skarlet-session: another window manager is running\n");
        return 1;
    }
    cursor = XCreateFontCursor(dpy, XC_left_ptr);
    XDefineCursor(dpy, root, cursor);

    linux_init();
    ws_init(); /* sizes the back buffer from plat_display_size() */
    image_init();
    prev = calloc((size_t)g_w * g_h, 4);
    for (int i = 0; i < g_w * g_h; i++)
        prev[i] = 0xff000000u; /* never a real colour: the first frame is all new */
    gc = XCreateGC(dpy, root, 0, 0);
    ewmh_init();
    desk_win = make_window(0, 0, g_w, g_h);
    for (int i = 0; i < MAX_LAYERS_X; i++)
        layer_win[i] = make_window(0, 0, 1, 1);
    XMapWindow(dpy, desk_win);
    grab_keys();
    adopt_existing();
    if (getenv("SKARLET_AUTOLOGIN"))
        ws_key((struct key){ K_ENTER, 0 });

    int xfd = ConnectionNumber(dpy);
    for (;;) {
        while (XPending(dpy)) {
            XEvent e;
            XNextEvent(dpy, &e);
            handle_event(&e);
        }
        ws_tick();
        XFlush(dpy);
        fd_set fds;
        FD_ZERO(&fds);
        FD_SET(xfd, &fds);
        int maxfd = linux_add_fds(&fds, xfd);
        struct timeval tv = { 0, 50000 }; /* the clock and terminals need ticks */
        if (select(maxfd + 1, &fds, 0, 0, &tv) > 0)
            linux_read_fds(&fds);
    }
}
