/* gfx.h - drawing into an 80x25 text-mode back buffer.
 *
 * The VGA text screen is a grid of 16-bit cells: the low byte is a character
 * from the IBM PC "code page 437" font, the high byte is the colour
 * attribute (4 bits background, 4 bits foreground).  We draw everything into
 * g_screen first and copy the whole thing to the screen once per frame, so
 * the user never sees half-drawn windows.
 */
#ifndef PLASMIX_GFX_H
#define PLASMIX_GFX_H

#include <stdint.h>
#include "platform.h"

enum {
    BLACK, BLUE, GREEN, CYAN, RED, MAGENTA, BROWN, LGRAY,
    DGRAY, LBLUE, LGREEN, LCYAN, LRED, LMAGENTA, YELLOW, WHITE
};
#define ATTR(fg, bg) ((uint8_t)(((bg) << 4) | (fg)))

/* A few code page 437 glyphs we use for "graphics". */
enum {
    CH_SHADE1 = 0xB0, CH_SHADE2 = 0xB1, CH_SHADE3 = 0xB2, CH_FULL = 0xDB,
    CH_UPPER = 0xDF, CH_LOWER = 0xDC,
    CH_H = 0xC4, CH_V = 0xB3, CH_TL = 0xDA, CH_TR = 0xBF, CH_BL = 0xC0, CH_BR = 0xD9,
    CH_LT = 0xC3, CH_RT = 0xB4,
    CH_DH = 0xCD, CH_DV = 0xBA, CH_DTL = 0xC9, CH_DTR = 0xBB, CH_DBL = 0xC8, CH_DBR = 0xBC,
    CH_RTRI = 0x10, CH_LTRI = 0x11, CH_UTRI = 0x1E, CH_DTRI = 0x1F,
    CH_BULLET = 0x07, CH_CIRCLE = 0x09, CH_SQUARE = 0xFE, CH_DOT = 0xFA,
    CH_NOTE = 0x0D, CH_SUN = 0x0F, CH_SMILE = 0x01, CH_DIAMOND = 0x04,
    CH_HOUSE = 0x7F, CH_MENU = 0xF0,
};

extern uint16_t g_screen[SCR_H * SCR_W];

void gfx_clip(int x, int y, int w, int h); /* restrict drawing to a rectangle */
void gfx_noclip(void);
void gfx_put(int x, int y, int ch, uint8_t attr);
void gfx_fill(int x, int y, int w, int h, int ch, uint8_t attr);
void gfx_text(int x, int y, const char *s, uint8_t attr);
/* Draw at most maxw characters, padding with spaces up to maxw. */
void gfx_textw(int x, int y, const char *s, int maxw, uint8_t attr);
void gfx_center(int x, int y, int w, const char *s, uint8_t attr);
void gfx_box(int x, int y, int w, int h, uint8_t attr, int dbl);
void gfx_shadow(int x, int y, int w, int h); /* drop shadow right/below */
void gfx_hline(int x, int y, int w, uint8_t attr);
uint8_t gfx_attr_at(int x, int y);
/* Read the visible text of one screen row into out (81 bytes). Used by tests. */
void gfx_row_text(int y, char *out);

/* ---- themes ------------------------------------------------------------
 * Plasma 4 separated the look ("desktop theme", e.g. Air or Oxygen) from
 * the widgets themselves.  Every widget draws with these named colours, so
 * switching the theme re-skins the whole desktop at once. */
struct theme {
    const char *name;
    uint8_t desk, desk_ch;          /* wallpaper colour and pattern glyph */
    uint8_t desk_hi;                /* wallpaper highlight streak */
    uint8_t panel, panel_hi, panel_dim, panel_sep;
    uint8_t win, win_dim, win_border, win_border_off;
    uint8_t title_on, title_off, title_btn;
    uint8_t menu, menu_hi, menu_head, menu_dim, menu_border;
    uint8_t widget, widget_head, widget_focus, widget_border;
    uint8_t input, sel, term, term_bold;
    uint8_t notes, toast;
};

extern const struct theme *g_theme;
extern const struct theme g_themes[];
extern const int g_theme_count;

#endif
