/* desktop.h - shared state of the Plasma-style workspace.
 *
 * The pieces map onto the Plasma 4 vocabulary:
 *   plasmoid     - a widget (clock, notes, folder view...). Everything you see
 *                  on the desktop and in the panel is one.
 *   containment  - something that holds plasmoids: the desktop and the panel.
 *   activity     - a named desktop containment with its own set of widgets
 *                  (and its own windows), e.g. "Desktop" and "Play".
 *   window       - an application window, managed by wm.c (our "KWin").
 */
#ifndef SKARLET_DESKTOP_H
#define SKARLET_DESKTOP_H

#include "platform.h"
#include "services.h"
#include "shell.h"
#include "vfs.h"

/* The panel is two rows high: a soft top rim (half blocks) and a row of
 * applets, which reads as the taller panel of a Plasma 4 desktop. */
#define PANEL_Y   (SCR_H - 1) /* the panel's applets */
#define PANEL_RIM (SCR_H - 2) /* the panel's top edge */
#define DESK_H    (SCR_H - 2) /* rows available to the desktop and windows */
#define MAX_WIN   10
#define NUM_DESKS 4

/* ---- application windows ------------------------------------------------ */

#define TERM_LINES 150
#define TERM_COLS  78

struct term_state {
    struct shell sh;
    char lines[TERM_LINES][TERM_COLS + 1];
    int nlines;          /* complete lines + the line being written */
    int col;             /* write position on the last line */
    int cols;            /* terminal width (client width) */
    char input[SH_LINE];
    int len;
    int hist_pos;
    int scroll;          /* rows scrolled back with Shift+PgUp */
    char pending[SH_LINE]; /* command to run once the window exists */
};

struct files_state {
    int dir, sel, top;
    int pane;  /* 0 = Places panel, 1 = file list */
    int place;
};

struct edit_state {
    char path[VFS_PATH_MAX];
    char buf[VFS_FILE_MAX];
    int len, cur, top, dirty;
};

struct list_state {
    int sel;
};

struct window {
    int used;
    int pid;
    int app;
    int x, y, w, h; /* outer rectangle including the frame */
    int desk;       /* virtual desktop 0..3 */
    int activity;
    char title[48];
    union {
        struct term_state term;
        struct files_state files;
        struct edit_state ed;
        struct list_state list;
    } s;
};

struct app {
    int w, h; /* default outer size */
    void (*init)(struct window *w, const char *arg);
    void (*draw)(struct window *w, int x, int y, int cw, int ch, int focused);
    void (*key)(struct window *w, struct key k);
    void (*idle)(struct window *w); /* optional, called every tick */
};
extern const struct app *const g_app_impl[APP_COUNT];

/* wm.c - the window manager */
struct window *wm_open(int app, const char *arg);
void wm_close(struct window *w);
struct window *wm_focused(void);
struct window *wm_by_pid(int pid);
void wm_raise(struct window *w);
void wm_cycle(void);
int  wm_visible(const struct window *w);
void wm_draw(void);
void wm_idle(void);
void wm_close_all(void);
void wm_activity_removed(int activity);
int  wm_count_on_desk(int desk); /* windows on a virtual desktop (current activity) */
/* Visible windows of the current desktop, bottom to top. Returns count. */
int  wm_list_visible(struct window **out, int max);

/* ---- plasmoids ---------------------------------------------------------- */

#define MAX_WIDGETS 8
#define MAX_ACTIVITIES 4

struct plasmoid {
    int used;
    int type;
    int x, y, w, h;
    int sel, top;          /* list selection, scroll */
    int tiles[16];         /* fifteen puzzle */
    char text[160];        /* notes */
};

struct plasmoid_type {
    const char *id;
    const char *name;
    const char *desc;
    int icon;   /* glyph shown in the Add Widgets explorer */
    int header; /* 1: the title is drawn inside the widget, under a line */
    int w, h;
    void (*init)(struct plasmoid *p);
    void (*draw)(struct plasmoid *p, int x, int y, int w, int h, int focused);
    int  (*key)(struct plasmoid *p, struct key k); /* 1 if handled */
};

enum { PL_FOLDERVIEW, PL_NOTES, PL_CLOCK, PL_SYSMON, PL_FIFTEEN, PL_COUNT };
extern const struct plasmoid_type g_plasmoid_types[PL_COUNT];

struct activity {
    char name[20];
    struct plasmoid widgets[MAX_WIDGETS];
    int focus; /* focused widget index or -1 */
};

/* ---- workspace ---------------------------------------------------------- */

enum { PHASE_LOGIN, PHASE_DESKTOP, PHASE_OFF };

struct workspace {
    int phase;
    int desk;
    int activity;
    int nactivities;
    struct activity activities[MAX_ACTIVITIES];
    /* settings (changed in Skarlet Settings) */
    int clock24, clock_seconds, locked, wallpaper;
    int dashboard;    /* Ctrl+F12: widgets shown, windows hidden */
    int move_mode;    /* Alt+F7: arrow keys move the focused window */
    struct datetime now;
    uint32_t uptime;
    char toast_title[32], toast_text[96];
    uint32_t toast_until;
    int need_redraw;
    int off_reboot;   /* PHASE_OFF: 1 = rebooting, 0 = shut down */
};
extern struct workspace g_ws;

void ws_init(void);
void ws_tick(void);   /* poll input, update, redraw: call in a loop */
void ws_key(struct key k);
void ws_draw(void);
void ws_place_widget(struct activity *a, int type);
void ws_format_time(char *out, int size, int seconds);
void ws_note_document(const char *path); /* for the launcher's Recently Used tab */

#endif
