/* gfx.c - back buffer drawing primitives and the two desktop themes. */
#include "gfx.h"
#include "lib.h"

uint16_t g_screen[SCR_H * SCR_W];

static int clip_x0 = 0, clip_y0 = 0, clip_x1 = SCR_W, clip_y1 = SCR_H;

void gfx_clip(int x, int y, int w, int h)
{
    clip_x0 = MAX(x, 0);
    clip_y0 = MAX(y, 0);
    clip_x1 = MIN(x + w, SCR_W);
    clip_y1 = MIN(y + h, SCR_H);
}

void gfx_noclip(void)
{
    clip_x0 = 0;
    clip_y0 = 0;
    clip_x1 = SCR_W;
    clip_y1 = SCR_H;
}

void gfx_put(int x, int y, int ch, uint8_t attr)
{
    if (x < clip_x0 || x >= clip_x1 || y < clip_y0 || y >= clip_y1)
        return;
    g_screen[y * SCR_W + x] = (uint16_t)((uint8_t)ch | (attr << 8));
}

uint8_t gfx_attr_at(int x, int y)
{
    if (x < 0 || x >= SCR_W || y < 0 || y >= SCR_H)
        return 0;
    return (uint8_t)(g_screen[y * SCR_W + x] >> 8);
}

void gfx_fill(int x, int y, int w, int h, int ch, uint8_t attr)
{
    for (int j = 0; j < h; j++)
        for (int i = 0; i < w; i++)
            gfx_put(x + i, y + j, ch, attr);
}

void gfx_text(int x, int y, const char *s, uint8_t attr)
{
    for (; *s; s++, x++)
        gfx_put(x, y, (unsigned char)*s, attr);
}

void gfx_textw(int x, int y, const char *s, int maxw, uint8_t attr)
{
    int i = 0;
    for (; i < maxw && s[i]; i++)
        gfx_put(x + i, y, (unsigned char)s[i], attr);
    for (; i < maxw; i++)
        gfx_put(x + i, y, ' ', attr);
}

void gfx_center(int x, int y, int w, const char *s, uint8_t attr)
{
    int n = MIN(k_strlen(s), w);
    gfx_textw(x + (w - n) / 2, y, s, n, attr);
}

void gfx_hline(int x, int y, int w, uint8_t attr)
{
    gfx_fill(x, y, w, 1, CH_H, attr);
}

void gfx_box(int x, int y, int w, int h, uint8_t attr, int dbl)
{
    int hz = dbl ? CH_DH : CH_H, vt = dbl ? CH_DV : CH_V;
    gfx_fill(x + 1, y, w - 2, 1, hz, attr);
    gfx_fill(x + 1, y + h - 1, w - 2, 1, hz, attr);
    gfx_fill(x, y + 1, 1, h - 2, vt, attr);
    gfx_fill(x + w - 1, y + 1, 1, h - 2, vt, attr);
    gfx_put(x, y, dbl ? CH_DTL : CH_TL, attr);
    gfx_put(x + w - 1, y, dbl ? CH_DTR : CH_TR, attr);
    gfx_put(x, y + h - 1, dbl ? CH_DBL : CH_BL, attr);
    gfx_put(x + w - 1, y + h - 1, dbl ? CH_DBR : CH_BR, attr);
}

/* A shadow keeps the character underneath but draws it dark grey on black,
 * which reads as "this popup floats above the desktop". */
static void shade_cell(int x, int y)
{
    if (x < clip_x0 || x >= clip_x1 || y < clip_y0 || y >= clip_y1)
        return;
    uint16_t *c = &g_screen[y * SCR_W + x];
    *c = (uint16_t)((*c & 0xFF) | (ATTR(DGRAY, BLACK) << 8));
}

void gfx_shadow(int x, int y, int w, int h)
{
    for (int j = 1; j <= h; j++)
        shade_cell(x + w, y + j);
    for (int i = 1; i <= w; i++)
        shade_cell(x + i, y + h);
}

void gfx_row_text(int y, char *out)
{
    for (int x = 0; x < SCR_W; x++) {
        int ch = g_screen[y * SCR_W + x] & 0xFF;
        out[x] = (ch >= 32 && ch < 127) ? (char)ch : ' ';
    }
    out[SCR_W] = 0;
}

/* ---- themes ------------------------------------------------------------ */

const struct theme g_themes[] = {
    {
        /* "Air": the light, airy look KDE shipped as the default Plasma
         * theme in the KDE 4 era. */
        .name = "Air",
        .desk = ATTR(LBLUE, BLUE), .desk_ch = CH_SHADE1, .desk_hi = ATTR(LCYAN, BLUE),
        .panel = ATTR(BLACK, LGRAY), .panel_hi = ATTR(WHITE, BLUE),
        .panel_dim = ATTR(DGRAY, LGRAY), .panel_sep = ATTR(WHITE, LGRAY),
        .win = ATTR(BLACK, LGRAY), .win_dim = ATTR(DGRAY, LGRAY),
        .win_border = ATTR(BLUE, LGRAY), .win_border_off = ATTR(DGRAY, LGRAY),
        .title_on = ATTR(BLACK, LCYAN), .title_off = ATTR(DGRAY, LGRAY),
        .title_btn = ATTR(BLUE, LCYAN),
        .menu = ATTR(BLACK, WHITE), .menu_hi = ATTR(WHITE, BLUE),
        .menu_head = ATTR(BLUE, WHITE), .menu_dim = ATTR(DGRAY, WHITE),
        .menu_border = ATTR(DGRAY, WHITE),
        .widget = ATTR(BLACK, WHITE), .widget_head = ATTR(BLUE, WHITE),
        .widget_focus = ATTR(LBLUE, WHITE), .widget_border = ATTR(LGRAY, WHITE),
        .input = ATTR(BLACK, LGRAY), .sel = ATTR(WHITE, BLUE),
        .term = ATTR(LGRAY, BLACK), .term_bold = ATTR(LGREEN, BLACK),
        .notes = ATTR(BLACK, YELLOW), .toast = ATTR(BLACK, WHITE),
    },
    {
        /* "Oxygen": the darker sibling theme of the same era. */
        .name = "Oxygen",
        .desk = ATTR(BLUE, BLACK), .desk_ch = CH_SHADE1, .desk_hi = ATTR(LBLUE, BLACK),
        .panel = ATTR(LGRAY, BLACK), .panel_hi = ATTR(WHITE, CYAN),
        .panel_dim = ATTR(DGRAY, BLACK), .panel_sep = ATTR(DGRAY, BLACK),
        .win = ATTR(WHITE, DGRAY), .win_dim = ATTR(LGRAY, DGRAY),
        .win_border = ATTR(LCYAN, DGRAY), .win_border_off = ATTR(LGRAY, DGRAY),
        .title_on = ATTR(WHITE, CYAN), .title_off = ATTR(LGRAY, DGRAY),
        .title_btn = ATTR(YELLOW, CYAN),
        .menu = ATTR(LGRAY, BLACK), .menu_hi = ATTR(WHITE, CYAN),
        .menu_head = ATTR(LCYAN, BLACK), .menu_dim = ATTR(DGRAY, BLACK),
        .menu_border = ATTR(DGRAY, BLACK),
        .widget = ATTR(LGRAY, BLACK), .widget_head = ATTR(LCYAN, BLACK),
        .widget_focus = ATTR(LCYAN, BLACK), .widget_border = ATTR(DGRAY, BLACK),
        .input = ATTR(WHITE, DGRAY), .sel = ATTR(WHITE, CYAN),
        .term = ATTR(LGRAY, BLACK), .term_bold = ATTR(LGREEN, BLACK),
        .notes = ATTR(BLACK, YELLOW), .toast = ATTR(WHITE, DGRAY),
    },
};

const int g_theme_count = ARRAY_LEN(g_themes);
const struct theme *g_theme = &g_themes[0];
