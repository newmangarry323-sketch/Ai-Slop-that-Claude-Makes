/* config.c - remembering the desktop between sessions.
 *
 * The kernel forgets everything when it stops.  On Linux there is a disk, so
 * the session keeps the settings (theme, accent, wallpaper, clock, widget
 * lock) and each activity's widgets (where they are, and the notes' text)
 * in ~/.config/skarletos/desktop.conf, a small text file:
 *
 *   theme 0
 *   accent 0
 *   activity Desktop
 *   widget 0 24 24
 *   widget 1 24 304 Buy milk\nCall Sam
 *
 * It is written whenever something changes (checked every two seconds).
 */
#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#include "../src/desktop.h"
#include "../src/gfx.h"
#include "../src/lib.h"
#include "linux.h"

static char path[512];
static char *saved; /* what the file holds now */

static void escape(const char *in, char *out, int size)
{
    int n = 0;
    for (; *in && n < size - 2; in++) {
        if (*in == '\n' || *in == '\\') {
            out[n++] = '\\';
            out[n++] = *in == '\n' ? 'n' : '\\';
        } else {
            out[n++] = *in;
        }
    }
    out[n] = 0;
}

static void unescape(const char *in, char *out, int size)
{
    int n = 0;
    for (; *in && n < size - 1; in++) {
        if (*in == '\\' && in[1]) {
            in++;
            out[n++] = *in == 'n' ? '\n' : *in;
        } else {
            out[n++] = *in;
        }
    }
    out[n] = 0;
}

/* The desktop's state as the file's text (malloc'd). */
static char *serialise(void)
{
    size_t cap = 16384, len = 0;
    char *buf = malloc(cap);
    if (!buf)
        return 0;
    len += (size_t)snprintf(buf + len, cap - len,
                            "# SkarletOS desktop settings, written by skarlet-session\n"
                            "theme %d\naccent %d\nwallpaper %d\nclock24 %d\nseconds %d\n"
                            "locked %d\n",
                            g_theme_index, g_accent_index, g_ws.wallpaper, g_ws.clock24,
                            g_ws.clock_seconds, g_ws.locked);
    for (int a = 0; a < g_ws.nactivities && len < cap - 1024; a++) {
        struct activity *act = &g_ws.activities[a];
        len += (size_t)snprintf(buf + len, cap - len, "activity %s\n", act->name);
        for (int i = 0; i < MAX_WIDGETS && len < cap - 600; i++) {
            struct plasmoid *p = &act->widgets[i];
            if (!p->used)
                continue;
            char text[sizeof p->text * 2 + 1];
            escape(p->type == PL_NOTES ? p->text : "", text, sizeof text);
            len += (size_t)snprintf(buf + len, cap - len, "widget %d %d %d%s%s\n", p->type, p->x,
                                    p->y, text[0] ? " " : "", text);
        }
    }
    return buf;
}

void config_load(void)
{
    const char *home = getenv("HOME");
    if (!home)
        return;
    snprintf(path, sizeof path, "%s/.config/skarletos/desktop.conf", home);
    FILE *f = fopen(path, "r");
    if (!f) {
        saved = serialise(); /* the defaults: nothing to write yet */
        return;
    }
    char line[1024];
    int theme = g_theme_index, accent = g_accent_index, have_activity = 0;
    struct activity *act = 0;
    while (fgets(line, sizeof line, f)) {
        line[strcspn(line, "\n")] = 0;
        int v, type, x, y, off = 0;
        if (sscanf(line, "theme %d", &v) == 1)
            theme = v;
        else if (sscanf(line, "accent %d", &v) == 1)
            accent = v;
        else if (sscanf(line, "wallpaper %d", &v) == 1)
            g_ws.wallpaper = MAX(0, MIN(v, 2));
        else if (sscanf(line, "clock24 %d", &v) == 1)
            g_ws.clock24 = !!v;
        else if (sscanf(line, "seconds %d", &v) == 1)
            g_ws.clock_seconds = !!v;
        else if (sscanf(line, "locked %d", &v) == 1)
            g_ws.locked = !!v;
        else if (!strncmp(line, "activity ", 9) && g_ws.nactivities < MAX_ACTIVITIES + 1) {
            if (!have_activity) { /* replace the default activities */
                g_ws.nactivities = 0;
                have_activity = 1;
            }
            if (g_ws.nactivities >= MAX_ACTIVITIES) {
                act = 0;
                continue;
            }
            act = &g_ws.activities[g_ws.nactivities++];
            memset(act, 0, sizeof *act);
            k_strlcpy(act->name, line + 9, sizeof act->name);
            act->focus = -1;
        } else if (act && sscanf(line, "widget %d %d %d%n", &type, &x, &y, &off) == 3 &&
                   type >= 0 && type < PL_COUNT) {
            ws_place_widget(act, type);
            if (act->focus >= 0) {
                struct plasmoid *p = &act->widgets[act->focus];
                p->x = MAX(0, MIN(x, g_w - p->w));
                p->y = MAX(0, MIN(y, DESK_BOTTOM - p->h));
                if (type == PL_NOTES && line[off] == ' ')
                    unescape(line + off + 1, p->text, sizeof p->text);
            }
            act->focus = -1;
        }
    }
    fclose(f);
    if (have_activity && g_ws.nactivities == 0) { /* a broken file: keep one activity */
        g_ws.nactivities = 1;
        k_strlcpy(g_ws.activities[0].name, "Desktop", sizeof g_ws.activities[0].name);
        g_ws.activities[0].focus = -1;
    }
    g_ws.activity = 0;
    theme_apply(theme, accent);
    saved = serialise();
    g_ws.need_redraw = 1;
}

/* Write the file if anything changed since it was last written. */
void config_save_if_changed(void)
{
    if (!path[0] || g_ws.phase != PHASE_DESKTOP)
        return;
    char *now = serialise();
    if (!now || (saved && !strcmp(now, saved))) {
        free(now);
        return;
    }
    char dir[512];
    snprintf(dir, sizeof dir, "%s", path);
    char *slash = strrchr(dir, '/');
    if (slash) {
        *slash = 0;
        char *parent = strrchr(dir, '/');
        if (parent) {
            *parent = 0;
            mkdir(dir, 0755); /* ~/.config */
            *parent = '/';
        }
        mkdir(dir, 0755);     /* ~/.config/skarletos */
    }
    /* Write a new file and rename it over the old one, so a crash halfway
     * never leaves a half-written file behind. */
    char tmp[600];
    snprintf(tmp, sizeof tmp, "%s.new", path);
    FILE *f = fopen(tmp, "w");
    if (f) {
        fputs(now, f);
        if (fclose(f) == 0 && rename(tmp, path) == 0) {
            free(saved);
            saved = now;
            return;
        }
    }
    free(now);
}
