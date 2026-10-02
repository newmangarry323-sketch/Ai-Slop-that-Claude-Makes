/* gfx.h - pixel graphics: a 32-bit back buffer and the drawing primitives the
 * desktop is built from.
 *
 * Everything is drawn into a back buffer (g_px, 0x00RRGGBB per pixel) and the
 * finished frame is copied to the screen in one go, so nothing half-drawn is
 * ever visible.  All maths is integer-only: the kernel never turns on the
 * floating point unit.
 */
#ifndef SKARLET_GFX_H
#define SKARLET_GFX_H

#include <stdint.h>
#include "font.h"
#include "platform.h"

/* The largest screen the back buffer can hold.  The kernel keeps this small
 * (the buffers live in its fixed memory); the Linux session raises it. */
#ifndef GFX_MAX_W
#define GFX_MAX_W 1920
#define GFX_MAX_H 1200
#endif

#define RGB(r, g, b) ((uint32_t)(((r) << 16) | ((g) << 8) | (b)))
#define MAROON RGB(0x80, 0x00, 0x00) /* HTML/CSS "maroon" */
#define WHITE RGB(255, 255, 255)
#define BLACK RGB(0, 0, 0)

extern uint32_t *g_px; /* back buffer */
extern int g_w, g_h;   /* screen size in pixels */

void gfx_init(int w, int h);
void gfx_clip(int x, int y, int w, int h); /* intersect with the current clip */
void gfx_noclip(void);

/* Colour helpers.  "t" and alpha values run from 0 (none) to 255 (all). */
uint32_t gfx_mix(uint32_t a, uint32_t b, int t);
uint32_t gfx_get(int x, int y);

/* Filled shapes; "a" is opacity 0..255.  Rounded corners are anti-aliased. */
void gfx_rect(int x, int y, int w, int h, uint32_t c, int a);
void gfx_rrect(int x, int y, int w, int h, int r, uint32_t c, int a);
void gfx_rrect_line(int x, int y, int w, int h, int r, uint32_t c, int a);
void gfx_shadow(int x, int y, int w, int h, int r, int blur, int a);
void gfx_circle(int cx, int cy, int r, uint32_t c, int a);
void gfx_ring(int cx, int cy, int r, int thick, uint32_t c, int a);
/* A thick line with round ends (a "capsule"); coordinates in 1/16 pixel. */
void gfx_line16(int x0, int y0, int x1, int y1, int thick16, uint32_t c, int a);
void gfx_line(int x0, int y0, int x1, int y1, int thick, uint32_t c, int a);
void gfx_vgradient(int x, int y, int w, int h, uint32_t top, uint32_t bottom, int a);

/* Text.  y is the top of the line; returns the width drawn. */
int gfx_text(const struct font *f, int x, int y, const char *s, uint32_t c, int a);
int gfx_text_width(const struct font *f, const char *s);
void gfx_text_center(const struct font *f, int x, int y, int w, const char *s, uint32_t c, int a);
void gfx_text_right(const struct font *f, int right, int y, const char *s, uint32_t c, int a);
/* Draw at most maxw pixels of text, ending with "..." if it had to be cut. */
void gfx_text_fit(const struct font *f, int x, int y, int maxw, const char *s, uint32_t c, int a);

/* Icons, drawn from lines and circles so they scale to any size. */
enum {
    IC_TERMINAL, IC_FOLDER, IC_FILE, IC_SETTINGS, IC_MONITOR, IC_LOGO, IC_SEARCH, IC_POWER,
    IC_RESTART, IC_LOGOUT, IC_VOLUME, IC_NETWORK, IC_BELL, IC_HOME, IC_DESKTOP, IC_CLOCK,
    IC_NOTES, IC_PUZZLE, IC_PLUS, IC_CLOSE, IC_MIN, IC_MAX, IC_ACTIVITY, IC_GRID, IC_LOCK,
    IC_KEYBOARD, IC_CALC, IC_RUN, IC_STAR, IC_RECENT, IC_CHEVRON_R, IC_CHEVRON_L, IC_DRIVE,
    IC_MUSIC, IC_HELP, IC_USER, IC_COUNT
};
void gfx_icon(int id, int x, int y, int size, uint32_t c, int a);
/* A coloured rounded tile with a white icon, like an app icon. */
void gfx_app_icon(int id, int x, int y, int size, uint32_t bg);

/* A record of the text drawn in the current frame ("x y text" per line).
 * Tests use it to ask what is on the screen without reading pixels. */
void gfx_textlog_reset(void);
int  gfx_text_visible(const char *needle);
int  gfx_text_visible_in(int y0, int y1, const char *needle);
const char *gfx_textlog(void);

/* ---- themes ------------------------------------------------------------
 * As in KDE Plasma, the look is a separate theme every widget draws with.
 * SkarletOS has a dark and a light theme, each built around an accent colour
 * (maroon by default).  theme_apply() fills in g_theme. */
struct theme {
    const char *name;
    int dark;
    uint32_t window, titlebar, border;      /* app windows */
    uint32_t card, panel;                   /* popups/widgets, the panel */
    int card_a, panel_a;                    /* their opacity (translucency) */
    uint32_t text, text_dim, text_on_accent;
    uint32_t input, hover, divider;
    uint32_t accent, accent_hi;             /* the accent, and a lighter tint */
    uint32_t term_bg, term_fg, term_prompt; /* the terminal stays dark */
    uint32_t wall_a, wall_b, wall_glow;     /* wallpaper gradient and glow */
    int shadow_a;
};

struct accent {
    const char *name;
    uint32_t color;
};

extern const struct theme *g_theme;
extern const char *const g_theme_names[];
extern const int g_theme_count;
extern const struct accent g_accents[];
extern const int g_accent_count;
extern int g_theme_index, g_accent_index;
void theme_apply(int theme, int accent);

/* The wallpaper is rendered once (per size/theme) and copied each frame. */
void gfx_wallpaper(int style);

#endif
