/* font.h - anti-aliased bitmap fonts, pre-rendered by tools/mkfont.c. */
#ifndef SKARLET_FONT_H
#define SKARLET_FONT_H

struct glyph {
    short w, h;       /* bitmap size */
    short xoff, yoff; /* left bearing; height above the baseline */
    short adv;        /* how far to move the pen */
    unsigned off;     /* index of the first coverage byte */
};

struct font {
    int size;
    int ascent, descent, line; /* pixels above/below the baseline; line height */
    const struct glyph *glyphs; /* characters 32..126 */
    const unsigned char *bits;  /* coverage bytes, 0..255 */
};

extern const struct font font_ui, font_ui_bold, font_small, font_title, font_mono, font_big,
    font_huge;

#endif
