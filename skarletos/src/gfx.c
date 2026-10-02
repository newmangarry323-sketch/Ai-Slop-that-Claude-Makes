/* gfx.c - the pixel drawing library: shapes, text, icons, wallpaper, themes.
 *
 * Anti-aliasing works by measuring, for each pixel near an edge, how far its
 * centre is from the ideal shape, in sixteenths of a pixel.  A pixel whose
 * centre sits exactly on the edge is half covered; one 8/16 inside is fully
 * covered.  That fraction scales the colour's opacity, which is what makes
 * curves look smooth instead of jagged.
 */
#include "gfx.h"
#include "lib.h"

static uint32_t pixels[GFX_MAX_W * GFX_MAX_H];
static uint32_t wall[GFX_MAX_W * GFX_MAX_H];

uint32_t *g_px = pixels;
int g_w = 1024, g_h = 768;

static int cx0, cy0, cx1, cy1; /* clip rectangle, exclusive right/bottom */

void gfx_init(int w, int h)
{
    g_w = MAX(320, MIN(w, GFX_MAX_W));
    g_h = MAX(200, MIN(h, GFX_MAX_H));
    gfx_noclip();
}

void gfx_clip(int x, int y, int w, int h)
{
    cx0 = MAX(x, 0);
    cy0 = MAX(y, 0);
    cx1 = MIN(x + w, g_w);
    cy1 = MIN(y + h, g_h);
}

void gfx_noclip(void)
{
    cx0 = cy0 = 0;
    cx1 = g_w;
    cy1 = g_h;
}

/* ---- colour ------------------------------------------------------------ */

/* Blend s over d with weight a (0..256).  Red and blue are done together in
 * one multiply, green in another; the 32-bit products cannot overflow because
 * the two weights always add up to 256. */
static inline uint32_t blend(uint32_t d, uint32_t s, uint32_t a)
{
    uint32_t rb = ((s & 0xFF00FF) * a + (d & 0xFF00FF) * (256 - a)) >> 8;
    uint32_t g = ((s & 0x00FF00) * a + (d & 0x00FF00) * (256 - a)) >> 8;
    return (rb & 0xFF00FF) | (g & 0x00FF00);
}

static inline uint32_t a256(int a) { return a <= 0 ? 0 : a >= 255 ? 256 : (uint32_t)(a + (a >> 7)); }

uint32_t gfx_mix(uint32_t a, uint32_t b, int t) { return blend(a, b, a256(t)); }

uint32_t gfx_get(int x, int y)
{
    if (x < 0 || y < 0 || x >= g_w || y >= g_h)
        return 0;
    return g_px[y * g_w + x];
}

static inline void plot(int x, int y, uint32_t c, uint32_t a)
{
    if (x < cx0 || x >= cx1 || y < cy0 || y >= cy1 || a == 0)
        return;
    uint32_t *p = &g_px[y * g_w + x];
    *p = a >= 256 ? c : blend(*p, c, a);
}

/* Integer square root (rounded down). */
static uint32_t isqrt(uint32_t v)
{
    uint32_t r = 0, bit = 1u << 30;
    while (bit > v)
        bit >>= 2;
    while (bit) {
        if (v >= r + bit) {
            v -= r + bit;
            r = (r >> 1) + bit;
        } else {
            r >>= 1;
        }
        bit >>= 2;
    }
    return r;
}

/* Coverage (0..256) of a pixel whose centre is d16/16 px from an edge,
 * positive meaning inside. */
static inline uint32_t edge_cov(int d16)
{
    d16 += 8;
    if (d16 <= 0)
        return 0;
    if (d16 >= 16)
        return 256;
    return (uint32_t)d16 * 16;
}

/* ---- rectangles --------------------------------------------------------- */

void gfx_rect(int x, int y, int w, int h, uint32_t c, int a)
{
    int x0 = MAX(x, cx0), y0 = MAX(y, cy0), x1 = MIN(x + w, cx1), y1 = MIN(y + h, cy1);
    uint32_t al = a256(a);
    if (al == 0)
        return;
    for (int j = y0; j < y1; j++) {
        uint32_t *p = &g_px[j * g_w];
        if (al >= 256)
            for (int i = x0; i < x1; i++)
                p[i] = c;
        else
            for (int i = x0; i < x1; i++)
                p[i] = blend(p[i], c, al);
    }
}

void gfx_vgradient(int x, int y, int w, int h, uint32_t top, uint32_t bottom, int a)
{
    for (int j = 0; j < h; j++)
        gfx_rect(x, y + j, w, 1, gfx_mix(top, bottom, h > 1 ? j * 255 / (h - 1) : 0), a);
}

/* How much of pixel (i, j) of a w x h rounded rectangle with corner radius r
 * is covered, 0..256.  Only pixels in the corner squares are not full. */
static uint32_t rrect_cov(int i, int j, int w, int h, int r)
{
    if (i < 0 || j < 0 || i >= w || j >= h)
        return 0;
    int ccx = i < r ? r : (i >= w - r ? w - r : -1);
    int ccy = j < r ? r : (j >= h - r ? h - r : -1);
    if (ccx < 0 || ccy < 0)
        return 256;
    int dx = i * 16 + 8 - ccx * 16, dy = j * 16 + 8 - ccy * 16;
    int d = (int)isqrt((uint32_t)(dx * dx + dy * dy));
    return edge_cov(r * 16 - d);
}

void gfx_rrect(int x, int y, int w, int h, int r, uint32_t c, int a)
{
    r = MIN(r, MIN(w, h) / 2);
    if (r <= 0) {
        gfx_rect(x, y, w, h, c, a);
        return;
    }
    uint32_t al = a256(a);
    /* The middle band and the strips between the corners are plain rects. */
    gfx_rect(x, y + r, w, h - 2 * r, c, a);
    gfx_rect(x + r, y, w - 2 * r, r, c, a);
    gfx_rect(x + r, y + h - r, w - 2 * r, r, c, a);
    for (int j = 0; j < r; j++)
        for (int i = 0; i < r; i++) {
            /* The same coverage applies to all four mirrored corners. */
            uint32_t cov = rrect_cov(i, j, w, h, r);
            if (!cov)
                continue;
            uint32_t aa = (al * cov) >> 8;
            plot(x + i, y + j, c, aa);
            plot(x + w - 1 - i, y + j, c, aa);
            plot(x + i, y + h - 1 - j, c, aa);
            plot(x + w - 1 - i, y + h - 1 - j, c, aa);
        }
}

/* An outline "thick" pixels wide: outer shape minus the inset inner shape. */
static void rrect_stroke(int x, int y, int w, int h, int r, int thick, uint32_t c, int a)
{
    r = MIN(r, MIN(w, h) / 2);
    uint32_t al = a256(a);
    int ri = MAX(r - thick, 0);
    for (int j = 0; j < h; j++) {
        int band = j < MAX(r, thick) + 1 || j >= h - MAX(r, thick) - 1;
        for (int i = 0; i < w; i++) {
            if (!band && i > MAX(r, thick) && i < w - MAX(r, thick) - 1) {
                i = w - MAX(r, thick) - 2;
                continue;
            }
            uint32_t co = rrect_cov(i, j, w, h, r);
            uint32_t ci = rrect_cov(i - thick, j - thick, w - 2 * thick, h - 2 * thick, ri);
            if (co > ci)
                plot(x + i, y + j, c, (al * (co - ci)) >> 8);
        }
    }
}

void gfx_rrect_line(int x, int y, int w, int h, int r, uint32_t c, int a)
{
    rrect_stroke(x, y, w, h, r, 1, c, a);
}

/* A soft shadow: opacity falls off smoothly over "blur" pixels outside the
 * rounded rectangle.  The inside is skipped: whatever casts the shadow will
 * be drawn on top of it. */
void gfx_shadow(int x, int y, int w, int h, int r, int blur, int a)
{
    if (blur <= 0)
        return;
    int b16 = blur * 16;
    int hw = w * 8, hh = h * 8; /* half sizes, 1/16 px */
    int mx = x * 16 + hw, my = y * 16 + hh;
    for (int j = y - blur; j < y + h + blur; j++) {
        if (j < cy0 || j >= cy1)
            continue;
        for (int i = x - blur; i < x + w + blur; i++) {
            if (j >= y + r && j < y + h - r && i == x) {
                i = x + w - 1; /* skip the interior of the straight part */
                continue;
            }
            int px = i * 16 + 8 - mx, py = j * 16 + 8 - my;
            int qx = (px < 0 ? -px : px) - (hw - r * 16);
            int qy = (py < 0 ? -py : py) - (hh - r * 16);
            int d;
            if (qx > 0 && qy > 0)
                d = (int)isqrt((uint32_t)(qx * qx + qy * qy)) - r * 16;
            else
                d = MAX(qx, qy) - r * 16;
            if (d <= 0 || d >= b16)
                continue;
            int k = b16 - d;
            uint32_t al = (uint32_t)((int64_t)a * k * k / ((int64_t)b16 * b16));
            plot(i, j, BLACK, a256((int)al));
        }
    }
}

/* ---- circles and lines --------------------------------------------------- */

static void circle16(int cx, int cy, int r16, int inner16, uint32_t c, int a)
{
    uint32_t al = a256(a);
    int x0 = (cx - r16) / 16 - 1, x1 = (cx + r16) / 16 + 1;
    int y0 = (cy - r16) / 16 - 1, y1 = (cy + r16) / 16 + 1;
    for (int j = y0; j <= y1; j++)
        for (int i = x0; i <= x1; i++) {
            int dx = i * 16 + 8 - cx, dy = j * 16 + 8 - cy;
            int d = (int)isqrt((uint32_t)(dx * dx + dy * dy));
            uint32_t cov = edge_cov(r16 - d);
            if (inner16 > 0) {
                uint32_t ci = edge_cov(inner16 - d);
                cov = cov > ci ? cov - ci : 0;
            }
            if (cov)
                plot(i, j, c, (al * cov) >> 8);
        }
}

void gfx_circle(int cx, int cy, int r, uint32_t c, int a)
{
    circle16(cx * 16, cy * 16, r * 16, 0, c, a);
}

void gfx_ring(int cx, int cy, int r, int thick, uint32_t c, int a)
{
    circle16(cx * 16, cy * 16, r * 16, (r - thick) * 16, c, a);
}

void gfx_line16(int x0, int y0, int x1, int y1, int thick16, uint32_t c, int a)
{
    uint32_t al = a256(a);
    int half = thick16 / 2;
    int bx0 = (MIN(x0, x1) - half) / 16 - 1, bx1 = (MAX(x0, x1) + half) / 16 + 1;
    int by0 = (MIN(y0, y1) - half) / 16 - 1, by1 = (MAX(y0, y1) + half) / 16 + 1;
    int64_t vx = x1 - x0, vy = y1 - y0, len2 = vx * vx + vy * vy;
    for (int j = by0; j <= by1; j++)
        for (int i = bx0; i <= bx1; i++) {
            int64_t px = i * 16 + 8 - x0, py = j * 16 + 8 - y0;
            int64_t t = len2 ? px * vx + py * vy : 0;
            /* Closest point on the segment, as a fraction t/len2 along it. */
            int64_t ex, ey;
            if (t <= 0 || len2 == 0) {
                ex = px;
                ey = py;
            } else if (t >= len2) {
                ex = px - vx;
                ey = py - vy;
            } else {
                ex = px - vx * t / len2;
                ey = py - vy * t / len2;
            }
            int64_t d2 = ex * ex + ey * ey;
            if (d2 > (int64_t)(half + 16) * (half + 16))
                continue;
            uint32_t cov = edge_cov(half - (int)isqrt((uint32_t)d2));
            if (cov)
                plot(i, j, c, (al * cov) >> 8);
        }
}

void gfx_line(int x0, int y0, int x1, int y1, int thick, uint32_t c, int a)
{
    gfx_line16(x0 * 16 + 8, y0 * 16 + 8, x1 * 16 + 8, y1 * 16 + 8, thick * 16, c, a);
}

/* ---- text ------------------------------------------------------------- */

#define TEXTLOG_SIZE 65536
static char textlog[TEXTLOG_SIZE];
static int textlog_len;

void gfx_textlog_reset(void)
{
    textlog_len = 0;
    textlog[0] = 0;
}

const char *gfx_textlog(void) { return textlog; }

static void log_text(int x, int y, const char *s)
{
    char head[24];
    int n = k_snprintf(head, sizeof head, "%d %d ", x, y);
    int len = k_strlen(s);
    if (textlog_len + n + len + 2 >= TEXTLOG_SIZE)
        return;
    k_memcpy(textlog + textlog_len, head, n);
    k_memcpy(textlog + textlog_len + n, s, len);
    textlog_len += n + len;
    textlog[textlog_len++] = '\n';
    textlog[textlog_len] = 0;
}

int gfx_text_visible_in(int y0, int y1, const char *needle)
{
    int nlen = k_strlen(needle);
    const char *p = textlog;
    while (*p) {
        const char *end = k_strchr(p, '\n');
        if (!end)
            break;
        int x, y;
        const char *q = p;
        x = k_atoi(q);
        (void)x;
        q = k_strchr(q, ' ') + 1;
        y = k_atoi(q);
        q = k_strchr(q, ' ') + 1;
        if (y >= y0 && y < y1)
            for (const char *s = q; s + nlen <= end; s++)
                if (k_strncmp(s, needle, nlen) == 0)
                    return 1;
        p = end + 1;
    }
    return 0;
}

int gfx_text_visible(const char *needle) { return gfx_text_visible_in(-100000, 100000, needle); }

static void draw_glyph(const struct font *f, const struct glyph *g, int x, int y, uint32_t c,
                       uint32_t al)
{
    const unsigned char *b = f->bits + g->off;
    int gx = x + g->xoff, gy = y + f->ascent - g->yoff;
    for (int j = 0; j < g->h; j++) {
        int yy = gy + j;
        if (yy < cy0 || yy >= cy1)
            continue;
        for (int i = 0; i < g->w; i++) {
            unsigned v = b[j * g->w + i];
            if (v)
                plot(gx + i, yy, c, (al * (v + (v >> 7))) >> 8);
        }
    }
}

int gfx_text_width(const struct font *f, const char *s)
{
    int w = 0;
    for (; *s; s++) {
        unsigned ch = (unsigned char)*s;
        if (ch < 32 || ch > 126)
            ch = '?';
        w += f->glyphs[ch - 32].adv;
    }
    return w;
}

int gfx_text(const struct font *f, int x, int y, const char *s, uint32_t c, int a)
{
    uint32_t al = a256(a);
    int x0 = x;
    if (x < cx1 && y < cy1 && *s)
        log_text(x, y, s);
    for (; *s; s++) {
        unsigned ch = (unsigned char)*s;
        if (ch < 32 || ch > 126)
            ch = '?';
        const struct glyph *g = &f->glyphs[ch - 32];
        if (g->w && x < cx1)
            draw_glyph(f, g, x, y, c, al);
        x += g->adv;
    }
    return x - x0;
}

void gfx_text_center(const struct font *f, int x, int y, int w, const char *s, uint32_t c, int a)
{
    gfx_text(f, x + (w - gfx_text_width(f, s)) / 2, y, s, c, a);
}

void gfx_text_right(const struct font *f, int right, int y, const char *s, uint32_t c, int a)
{
    gfx_text(f, right - gfx_text_width(f, s), y, s, c, a);
}

void gfx_text_fit(const struct font *f, int x, int y, int maxw, const char *s, uint32_t c, int a)
{
    if (gfx_text_width(f, s) <= maxw) {
        gfx_text(f, x, y, s, c, a);
        return;
    }
    char buf[160];
    int dots = gfx_text_width(f, "..."), n = 0, w = 0;
    while (s[n] && n < (int)sizeof buf - 4) {
        unsigned ch = (unsigned char)s[n];
        int adv = f->glyphs[(ch < 32 || ch > 126 ? '?' : ch) - 32].adv;
        if (w + adv + dots > maxw)
            break;
        w += adv;
        n++;
    }
    k_memcpy(buf, s, n);
    k_strlcpy(buf + n, "...", 4);
    gfx_text(f, x, y, buf, c, a);
}

/* ---- icons -------------------------------------------------------------
 * Icons are designed on a 24 x 24 grid and drawn with round-ended lines,
 * circles and rounded rectangles, scaled to the requested size. */

static int ix, iy, isz; /* current icon origin (1/16 px) and size */
static uint32_t icol;
static int ialpha;

#define S(v) ((v) * isz * 16 / 24)
static void L(int x0, int y0, int x1, int y1, int t2)
{
    gfx_line16(ix + S(x0), iy + S(y0), ix + S(x1), iy + S(y1), S(t2) / 2, icol, ialpha);
}
static void C(int x, int y, int r2) { circle16(ix + S(x), iy + S(y), S(r2) / 2, 0, icol, ialpha); }
static void O(int x, int y, int r2, int t2)
{
    circle16(ix + S(x), iy + S(y), S(r2) / 2, S(r2 - 2 * t2) / 2, icol, ialpha);
}
static void R(int x, int y, int w, int h, int r)
{
    gfx_rrect(ix / 16 + S(x) / 16, iy / 16 + S(y) / 16, S(w) / 16, S(h) / 16,
              S(r) / 16, icol, ialpha);
}
static void RO(int x, int y, int w, int h, int r)
{
    int t = MAX(1, S(2) / 16);
    rrect_stroke(ix / 16 + S(x) / 16, iy / 16 + S(y) / 16, S(w) / 16, S(h) / 16, S(r) / 16, t,
                 icol, ialpha);
}

/* Unit vectors every 30 degrees, times 1000. */
static const int cos30[12] = { 1000, 866, 500, 0, -500, -866, -1000, -866, -500, 0, 500, 866 };
static const int sin30[12] = { 0, 500, 866, 1000, 866, 500, 0, -500, -866, -1000, -866, -500 };

/* An arc of radius r (grid units) round (cx, cy), between two 30-degree steps. */
static void arc(int cx, int cy, int r, int from, int to)
{
    for (int k = from; k < to; k++) {
        int a = k % 12, b = (k + 1) % 12;
        gfx_line16(ix + S(cx) + S(r) * cos30[a] / 1000, iy + S(cy) + S(r) * sin30[a] / 1000,
                   ix + S(cx) + S(r) * cos30[b] / 1000, iy + S(cy) + S(r) * sin30[b] / 1000,
                   S(2), icol, ialpha);
    }
}

void gfx_icon(int id, int x, int y, int size, uint32_t c, int a)
{
    ix = x * 16;
    iy = y * 16;
    isz = size;
    icol = c;
    ialpha = a;
    switch (id) {
    case IC_TERMINAL:
        RO(2, 4, 20, 16, 3);
        L(7, 9, 10, 12, 2);
        L(10, 12, 7, 15, 2);
        L(12, 15, 17, 15, 2);
        break;
    case IC_FOLDER:
        R(2, 4, 9, 5, 2);
        R(2, 7, 20, 13, 2);
        break;
    case IC_FILE:
        RO(5, 2, 14, 20, 2);
        L(8, 9, 16, 9, 2);
        L(8, 13, 16, 13, 2);
        L(8, 17, 13, 17, 2);
        break;
    case IC_SETTINGS:
        O(12, 12, 12, 3);
        for (int k = 0; k < 12; k += 2)
            gfx_line16(ix + S(12) + S(7) * cos30[k] / 1000, iy + S(12) + S(7) * sin30[k] / 1000,
                       ix + S(12) + S(10) * cos30[k] / 1000, iy + S(12) + S(10) * sin30[k] / 1000,
                       S(4), icol, ialpha);
        break;
    case IC_MONITOR:
        L(6, 18, 6, 13, 3);
        L(10, 18, 10, 8, 3);
        L(14, 18, 14, 11, 3);
        L(18, 18, 18, 5, 3);
        L(3, 21, 21, 21, 2);
        break;
    case IC_LOGO:
        /* A stylised S: two arcs. */
        arc(12, 8, 5, 3, 12);
        arc(12, 16, 5, 9, 18);
        break;
    case IC_SEARCH:
        O(10, 10, 13, 2);
        L(15, 15, 20, 20, 3);
        break;
    case IC_POWER:
        arc(12, 13, 8, 10, 20);
        L(12, 3, 12, 11, 2);
        break;
    case IC_RESTART:
        arc(12, 12, 8, 1, 11);
        L(20, 12, 17, 9, 2);
        L(20, 12, 22, 8, 2);
        break;
    case IC_LOGOUT:
        L(11, 4, 5, 4, 2);
        L(5, 4, 5, 20, 2);
        L(5, 20, 11, 20, 2);
        L(10, 12, 20, 12, 2);
        L(16, 8, 20, 12, 2);
        L(16, 16, 20, 12, 2);
        break;
    case IC_VOLUME:
        R(4, 9, 5, 6, 1);
        L(8, 10, 13, 5, 2);
        L(13, 5, 13, 19, 2);
        L(13, 19, 8, 14, 2);
        arc(13, 12, 5, 10, 14);
        arc(13, 12, 9, 10, 14);
        break;
    case IC_NETWORK:
        L(5, 20, 5, 17, 3);
        L(10, 20, 10, 13, 3);
        L(15, 20, 15, 9, 3);
        L(20, 20, 20, 4, 3);
        break;
    case IC_BELL:
        C(12, 10, 12);
        R(6, 10, 12, 7, 0);
        L(4, 17, 20, 17, 2);
        C(12, 20, 4);
        break;
    case IC_HOME:
        L(3, 12, 12, 4, 2);
        L(12, 4, 21, 12, 2);
        L(6, 10, 6, 20, 2);
        L(6, 20, 18, 20, 2);
        L(18, 20, 18, 10, 2);
        L(10, 20, 10, 15, 2);
        L(10, 15, 14, 15, 2);
        L(14, 15, 14, 20, 2);
        break;
    case IC_DESKTOP:
        RO(3, 4, 18, 13, 2);
        L(12, 17, 12, 20, 2);
        L(8, 20, 16, 20, 2);
        break;
    case IC_CLOCK:
    case IC_RECENT:
        O(12, 12, 19, 2);
        L(12, 12, 12, 7, 2);
        L(12, 12, 16, 14, 2);
        break;
    case IC_NOTES:
        RO(4, 3, 16, 18, 2);
        L(8, 8, 16, 8, 2);
        L(8, 12, 16, 12, 2);
        L(8, 16, 13, 16, 2);
        break;
    case IC_PUZZLE:
    case IC_GRID:
        R(3, 3, 8, 8, 2);
        R(13, 3, 8, 8, 2);
        R(3, 13, 8, 8, 2);
        R(13, 13, 8, 8, 2);
        break;
    case IC_PLUS:
        L(12, 5, 12, 19, 2);
        L(5, 12, 19, 12, 2);
        break;
    case IC_CLOSE:
        L(6, 6, 18, 18, 2);
        L(18, 6, 6, 18, 2);
        break;
    case IC_MIN:
        L(6, 12, 18, 12, 2);
        break;
    case IC_MAX:
        RO(6, 6, 12, 12, 2);
        break;
    case IC_ACTIVITY:
        C(12, 5, 6);
        C(5, 12, 6);
        C(19, 12, 6);
        C(12, 19, 6);
        break;
    case IC_LOCK:
        arc(12, 9, 5, 6, 12);
        L(7, 9, 7, 11, 2);
        L(17, 9, 17, 11, 2);
        R(5, 11, 14, 10, 2);
        break;
    case IC_KEYBOARD:
        RO(2, 6, 20, 12, 2);
        L(6, 10, 7, 10, 2);
        L(10, 10, 11, 10, 2);
        L(14, 10, 15, 10, 2);
        L(18, 10, 18, 10, 2);
        L(7, 14, 17, 14, 2);
        break;
    case IC_CALC:
        RO(5, 2, 14, 20, 2);
        L(8, 6, 16, 6, 2);
        C(9, 11, 2);
        C(15, 11, 2);
        C(9, 16, 2);
        C(15, 16, 2);
        break;
    case IC_RUN:
    case IC_CHEVRON_R:
        L(9, 5, 16, 12, 2);
        L(16, 12, 9, 19, 2);
        break;
    case IC_CHEVRON_L:
        L(15, 5, 8, 12, 2);
        L(8, 12, 15, 19, 2);
        break;
    case IC_STAR: {
        static const int pts[10][2] = { { 12, 3 }, { 14, 9 }, { 21, 9 }, { 16, 14 }, { 18, 20 },
                                        { 12, 16 }, { 6, 20 }, { 8, 14 }, { 3, 9 }, { 10, 9 } };
        for (int k = 0; k < 10; k++)
            L(pts[k][0], pts[k][1], pts[(k + 1) % 10][0], pts[(k + 1) % 10][1], 2);
        break;
    }
    case IC_DRIVE:
        RO(3, 7, 18, 10, 2);
        C(17, 12, 3);
        break;
    case IC_MUSIC:
        C(8, 17, 7);
        L(10, 17, 10, 5, 2);
        L(10, 5, 18, 3, 2);
        break;
    case IC_HELP:
        O(12, 12, 19, 2);
        arc(12, 9, 3, 6, 15);
        L(12, 12, 12, 14, 2);
        C(12, 17, 2);
        break;
    case IC_USER:
        C(12, 8, 9);
        L(6, 19, 18, 19, 6);
        break;
    }
}

void gfx_app_icon(int id, int x, int y, int size, uint32_t bg)
{
    gfx_rrect(x, y, size, size, size / 4, bg, 255);
    /* A soft highlight on the top half gives the tile some depth. */
    gfx_rrect(x, y, size, size / 2, size / 4, WHITE, 28);
    int pad = size / 5;
    gfx_icon(id, x + pad, y + pad, size - 2 * pad, WHITE, 255);
}

/* ---- themes ------------------------------------------------------------- */

const char *const g_theme_names[] = { "Skarlet Dark", "Skarlet Light" };
const int g_theme_count = ARRAY_LEN(g_theme_names);

/* Maroon comes first, so it is the default. */
const struct accent g_accents[] = {
    { "Maroon", MAROON },
    { "Blue", RGB(0x2a, 0x6f, 0xdb) },
    { "Teal", RGB(0x0f, 0x8b, 0x8d) },
    { "Green", RGB(0x2e, 0x85, 0x40) },
    { "Purple", RGB(0x7b, 0x3f, 0xa0) },
};
const int g_accent_count = ARRAY_LEN(g_accents);

int g_theme_index, g_accent_index;
static struct theme current;
const struct theme *g_theme = &current;
static int wall_valid;

void theme_apply(int theme, int accent)
{
    g_theme_index = (theme % g_theme_count + g_theme_count) % g_theme_count;
    g_accent_index = (accent % g_accent_count + g_accent_count) % g_accent_count;
    uint32_t A = g_accents[g_accent_index].color;
    struct theme *t = &current;
    t->name = g_theme_names[g_theme_index];
    t->accent = A;
    t->accent_hi = gfx_mix(A, WHITE, 110);
    t->text_on_accent = WHITE;
    t->term_bg = RGB(0x14, 0x11, 0x16);
    t->term_fg = RGB(0xe6, 0xe0, 0xe8);
    t->term_prompt = gfx_mix(A, WHITE, 120);
    t->wall_glow = A;
    if (g_theme_index == 0) {
        t->dark = 1;
        t->window = RGB(0x22, 0x1f, 0x26);
        t->titlebar = RGB(0x1b, 0x18, 0x1f);
        t->border = RGB(0x3c, 0x36, 0x42);
        t->card = RGB(0x24, 0x20, 0x29);
        t->panel = RGB(0x16, 0x13, 0x18);
        t->card_a = 236;
        t->panel_a = 220;
        t->text = RGB(0xf3, 0xef, 0xf4);
        t->text_dim = RGB(0xa6, 0x9d, 0xaa);
        t->input = RGB(0x31, 0x2c, 0x37);
        t->hover = RGB(0x2e, 0x29, 0x33);
        t->divider = RGB(0x38, 0x32, 0x3e);
        t->wall_a = RGB(0x12, 0x05, 0x08);
        t->wall_b = RGB(0x2e, 0x0a, 0x16);
        t->shadow_a = 150;
    } else {
        t->dark = 0;
        t->window = RGB(0xf8, 0xf5, 0xf7);
        t->titlebar = RGB(0xee, 0xe8, 0xec);
        t->border = RGB(0xd6, 0xcd, 0xd3);
        t->card = RGB(0xff, 0xff, 0xff);
        t->panel = RGB(0xf6, 0xf2, 0xf5);
        t->card_a = 236;
        t->panel_a = 224;
        t->text = RGB(0x1e, 0x1a, 0x20);
        t->text_dim = RGB(0x6c, 0x63, 0x70);
        t->input = RGB(0xec, 0xe5, 0xea);
        t->hover = RGB(0xef, 0xe8, 0xed);
        t->divider = RGB(0xe0, 0xd7, 0xdd);
        t->wall_a = RGB(0xf6, 0xec, 0xf0);
        t->wall_b = RGB(0xd9, 0xbd, 0xc7);
        t->shadow_a = 80;
    }
    wall_valid = 0;
}

/* ---- wallpaper ------------------------------------------------------------
 * A diagonal gradient, soft glows in the accent colour and a sweeping band of
 * light: the kind of abstract wallpaper current desktops ship with.  It is
 * computed once and cached, because it is the most expensive thing drawn. */

static int wall_style = -1, wall_w, wall_h;

static void render_wallpaper(int style)
{
    const struct theme *t = g_theme;
    int w = g_w, h = g_h;
    /* Glow centres and radii, relative to the screen. */
    int gx[3] = { w * 3 / 4, w / 6, w / 2 }, gy[3] = { h / 4, h * 4 / 5, h + h / 3 };
    int gr[3] = { w / 2, w * 2 / 5, w * 2 / 3 };
    int gs[3] = { 150, 90, 110 }; /* strengths */
    uint32_t gc[3] = { t->wall_glow, gfx_mix(t->wall_glow, RGB(0x50, 0x10, 0x60), 140),
                       gfx_mix(t->wall_glow, WHITE, 60) };
    if (!t->dark)
        for (int k = 0; k < 3; k++)
            gs[k] = gs[k] * 2 / 5;
    for (int y = 0; y < h; y++) {
        for (int x = 0; x < w; x++) {
            int tt = (x * 255 / w + y * 255 / h) / 2;
            uint32_t c = gfx_mix(t->wall_a, t->wall_b, tt);
            if (style != 2) {
                for (int k = 0; k < 3; k++) {
                    int dx = x - gx[k], dy = y - gy[k];
                    int64_t d2 = (int64_t)dx * dx + (int64_t)dy * dy;
                    int64_t r2 = (int64_t)gr[k] * gr[k];
                    if (d2 >= r2)
                        continue;
                    int f = (int)(256 - d2 * 256 / r2); /* 0..256 */
                    c = gfx_mix(c, gc[k], f * f / 256 * gs[k] / 256);
                }
                /* A soft sweeping band of light across the lower half. */
                int yc = h * 3 / 5 + (int)((int64_t)(x - w / 2) * (x - w / 2) / (w * 2)) - x / 5;
                int d = y - yc;
                if (d < 0)
                    d = -d;
                if (d < 90) {
                    int f = (90 - d) * 256 / 90;
                    c = gfx_mix(c, t->dark ? gfx_mix(t->wall_glow, WHITE, 90) : WHITE,
                                f * f / 256 * (t->dark ? 50 : 90) / 256);
                }
            }
            /* 2x2 dots on a 24-pixel grid: lighter on dark, darker on light. */
            if (style == 1 && (x % 24) / 2 == 6 && (y % 24) / 2 == 6)
                c = t->dark ? gfx_mix(c, WHITE, 60) : gfx_mix(c, BLACK, 36);
            wall[y * w + x] = c;
        }
    }
    wall_style = style;
    wall_w = w;
    wall_h = h;
    wall_valid = 1;
}

void gfx_wallpaper(int style)
{
    if (!wall_valid || style != wall_style || wall_w != g_w || wall_h != g_h)
        render_wallpaper(style);
    uint32_t *d = g_px;
    const uint32_t *s = wall;
    for (int n = g_w * g_h; n > 0; n--)
        *d++ = *s++;
}
