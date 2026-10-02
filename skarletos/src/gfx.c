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

const char *const g_theme_names[] = { "Skarlet Light", "Skarlet Dark" };
const int g_theme_count = ARRAY_LEN(g_theme_names);

/* Maroon comes first, so it is the default. */
const struct accent g_accents[] = {
    { "Maroon", MAROON, LRED },
    { "Blue", BLUE, LBLUE },
    { "Teal", CYAN, LCYAN },
    { "Green", GREEN, LGREEN },
    { "Purple", MAGENTA, LMAGENTA },
};
const int g_accent_count = ARRAY_LEN(g_accents);

int g_theme_index, g_accent_index;
static struct theme current;
const struct theme *g_theme = &current;

void theme_apply(int theme, int accent)
{
    g_theme_index = (theme % g_theme_count + g_theme_count) % g_theme_count;
    g_accent_index = (accent % g_accent_count + g_accent_count) % g_accent_count;
    const int A = g_accents[g_accent_index].dark, L = g_accents[g_accent_index].light;
    struct theme *t = &current;

    /* Shared by both themes: the wallpaper, accent blocks, terminal, notes. */
    t->name = g_theme_names[g_theme_index];
    t->desk_ch = CH_SHADE1;
    t->accent = ATTR(WHITE, A);
    t->accent_alt = ATTR(BLACK, L);
    t->sel = ATTR(WHITE, A);
    t->menu_hi = ATTR(WHITE, A);
    t->panel_hi = ATTR(WHITE, A);
    t->term = ATTR(LGRAY, BLACK);
    t->term_bold = ATTR(L, BLACK);
    t->notes = ATTR(BLACK, YELLOW);

    if (g_theme_index == 0) {
        /* Skarlet Light: light windows, menus and panel. */
        t->desk = ATTR(A, BLACK);
        t->desk_hi = ATTR(L, A);
        t->panel = ATTR(BLACK, LGRAY);
        t->panel_dim = ATTR(DGRAY, LGRAY);
        t->panel_sep = ATTR(WHITE, LGRAY);
        t->win = ATTR(BLACK, LGRAY);
        t->win_dim = ATTR(DGRAY, LGRAY);
        t->win_border = ATTR(A, LGRAY);
        t->win_border_off = ATTR(DGRAY, LGRAY);
        /* Oxygen-style: the title bar is window-coloured; the active window is
         * marked by darker title text and an accent "glow" round its frame. */
        t->title_on = ATTR(BLACK, LGRAY);
        t->title_off = ATTR(DGRAY, LGRAY);
        t->title_btn = ATTR(A, LGRAY);
        t->panel_btn = ATTR(BLACK, WHITE);
        t->menu = ATTR(BLACK, WHITE);
        t->menu_head = ATTR(A, WHITE);
        t->menu_dim = ATTR(DGRAY, WHITE);
        t->menu_border = ATTR(DGRAY, WHITE);
        t->widget = ATTR(BLACK, WHITE);
        t->widget_head = ATTR(A, WHITE);
        t->widget_focus = ATTR(L, WHITE);
        t->widget_border = ATTR(LGRAY, WHITE);
        t->input = ATTR(BLACK, LGRAY);
        t->toast = ATTR(BLACK, WHITE);
    } else {
        /* Skarlet Dark: dark grey windows, black panel and menus. */
        t->desk = ATTR(DGRAY, BLACK);
        t->desk_hi = ATTR(A, BLACK);
        t->panel = ATTR(LGRAY, BLACK);
        t->panel_dim = ATTR(DGRAY, BLACK);
        t->panel_sep = ATTR(DGRAY, BLACK);
        t->win = ATTR(WHITE, DGRAY);
        t->win_dim = ATTR(LGRAY, DGRAY);
        t->win_border = ATTR(L, DGRAY);
        t->win_border_off = ATTR(LGRAY, DGRAY);
        t->title_on = ATTR(WHITE, DGRAY);
        t->title_off = ATTR(LGRAY, DGRAY);
        t->title_btn = ATTR(L, DGRAY);
        t->panel_btn = ATTR(LGRAY, DGRAY);
        t->menu = ATTR(LGRAY, BLACK);
        t->menu_head = ATTR(L, BLACK);
        t->menu_dim = ATTR(DGRAY, BLACK);
        t->menu_border = ATTR(DGRAY, BLACK);
        t->widget = ATTR(LGRAY, BLACK);
        t->widget_head = ATTR(L, BLACK);
        t->widget_focus = ATTR(L, BLACK);
        t->widget_border = ATTR(DGRAY, BLACK);
        t->input = ATTR(WHITE, DGRAY);
        t->toast = ATTR(WHITE, DGRAY);
    }
}
