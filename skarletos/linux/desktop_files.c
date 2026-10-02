/* desktop_files.c - the programs installed on the system, for the launcher.
 *
 * Linux programs announce themselves with ".desktop" files (the
 * freedesktop.org Desktop Entry Specification): small text files in
 * /usr/share/applications and a few other folders, giving a program's name,
 * a comment, its categories and the command that starts it.  Every desktop
 * environment reads them; so does SkarletOS, which is how a program you
 * install with "sudo apt install" appears in the launcher.  The folders are
 * checked again every few seconds, so new programs show up by themselves.
 */
#define _GNU_SOURCE
#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

#include "../src/desktop.h"
#include "../src/gfx.h"
#include "../src/lib.h"
#include "linux.h"

#define MAX_APPS 512

struct entry {
    struct installed_app pub;
    char exec[512];
    char wmclass[64];   /* StartupWMClass, or the program's file name */
    char id[64];        /* file name without .desktop */
    int terminal;
};

static struct entry apps[MAX_APPS];
static int napps;

static const char *dirs[] = {
    "/usr/share/applications", "/usr/local/share/applications",
    "/var/lib/flatpak/exports/share/applications", "/var/lib/snapd/desktop/applications",
    0, /* ~/.local/share/applications, filled in at start-up */
};
static char user_dir[512];
static time_t dir_mtime[8];

/* The launcher's category, icon and colour for a Categories= line. */
static void classify(struct entry *e, const char *cats)
{
    static const struct { const char *cat, *name; int icon; uint32_t color; } map[] = {
        { "WebBrowser", "Internet", IC_NETWORK, 0x2a6fdb },
        { "Network", "Internet", IC_NETWORK, 0x2a6fdb },
        { "Office", "Office", IC_FILE, 0x2e8540 },
        { "Graphics", "Graphics", IC_STAR, 0xb5487a },
        { "AudioVideo", "Multimedia", IC_MUSIC, 0xd05a22 },
        { "Audio", "Multimedia", IC_MUSIC, 0xd05a22 },
        { "Video", "Multimedia", IC_MUSIC, 0xd05a22 },
        { "Development", "Development", IC_TERMINAL, 0x3a3440 },
        { "Game", "Games", IC_PUZZLE, 0x7b3fa0 },
        { "Education", "Education", IC_HELP, 0x0f8b8d },
        { "Science", "Science", IC_CALC, 0x0f8b8d },
        { "Settings", "Settings", IC_SETTINGS, 0x636772 },
        { "System", "System", IC_MONITOR, 0x228f62 },
        { "Utility", "Utilities", IC_GRID, 0x5d5764 },
    };
    for (int i = 0; i < ARRAY_LEN(map); i++) {
        char want[40];
        snprintf(want, sizeof want, "%s;", map[i].cat);
        /* Categories= is a ;-separated list; match whole words. */
        for (const char *p = cats; (p = strstr(p, want)); p++)
            if (p == cats || p[-1] == ';') {
                k_strlcpy(e->pub.category, map[i].name, sizeof e->pub.category);
                e->pub.icon = map[i].icon;
                e->pub.color = map[i].color;
                return;
            }
    }
    k_strlcpy(e->pub.category, "Utilities", sizeof e->pub.category);
    e->pub.icon = IC_GRID;
    e->pub.color = 0x5d5764;
}

/* Is a program on the PATH (for TryExec=)? */
static int on_path(const char *prog)
{
    if (strchr(prog, '/'))
        return access(prog, X_OK) == 0;
    const char *path = getenv("PATH");
    char buf[1024], full[1100];
    snprintf(buf, sizeof buf, "%s", path ? path : "/usr/local/bin:/usr/bin:/bin");
    for (char *d = strtok(buf, ":"); d; d = strtok(0, ":")) {
        snprintf(full, sizeof full, "%s/%s", d, prog);
        if (access(full, X_OK) == 0)
            return 1;
    }
    return 0;
}

/* Exec= may contain "field codes" (%f, %U...) for files to open; we start
 * programs without files, so they are dropped ("%%" is a plain %). */
static void clean_exec(const char *in, char *out, int size)
{
    int n = 0;
    for (; *in && n < size - 1; in++) {
        if (*in == '%' && in[1]) {
            if (in[1] == '%')
                out[n++] = '%';
            in++;
            continue;
        }
        out[n++] = *in;
    }
    out[n] = 0;
}

static int list_has(const char *list, const char *word)
{
    char want[40];
    snprintf(want, sizeof want, "%s;", word);
    return strstr(list, want) != 0;
}

static void read_file(const char *path, const char *id)
{
    FILE *f = fopen(path, "r");
    if (!f)
        return;
    struct entry e;
    memset(&e, 0, sizeof e);
    char line[1024], cats[512] = "", type[32] = "", tryexec[256] = "", only[256] = "",
         notin[256] = "", generic[96] = "";
    int in_main = 0, hidden = 0;
    while (fgets(line, sizeof line, f)) {
        line[strcspn(line, "\r\n")] = 0;
        if (line[0] == '[') {
            in_main = !strcmp(line, "[Desktop Entry]");
            continue;
        }
        char *eq = strchr(line, '=');
        if (!in_main || !eq)
            continue;
        *eq = 0;
        const char *key = line, *val = eq + 1;
        /* Only the untranslated keys (Name=, not Name[de]=). */
        if (!strcmp(key, "Name"))
            k_strlcpy(e.pub.name, val, sizeof e.pub.name);
        else if (!strcmp(key, "GenericName"))
            k_strlcpy(generic, val, sizeof generic);
        else if (!strcmp(key, "Comment"))
            k_strlcpy(e.pub.comment, val, sizeof e.pub.comment);
        else if (!strcmp(key, "Exec"))
            clean_exec(val, e.exec, sizeof e.exec);
        else if (!strcmp(key, "TryExec"))
            k_strlcpy(tryexec, val, sizeof tryexec);
        else if (!strcmp(key, "Type"))
            k_strlcpy(type, val, sizeof type);
        else if (!strcmp(key, "Categories"))
            k_strlcpy(cats, val, sizeof cats);
        else if (!strcmp(key, "Terminal"))
            e.terminal = !strcmp(val, "true");
        else if (!strcmp(key, "NoDisplay") || !strcmp(key, "Hidden"))
            hidden |= !strcmp(val, "true");
        else if (!strcmp(key, "OnlyShowIn"))
            k_strlcpy(only, val, sizeof only);
        else if (!strcmp(key, "NotShowIn"))
            k_strlcpy(notin, val, sizeof notin);
        else if (!strcmp(key, "StartupWMClass"))
            k_strlcpy(e.wmclass, val, sizeof e.wmclass);
    }
    fclose(f);
    /* Lists are ;-separated and should end with ';', but not all do. */
    char *lists[] = { cats, only, notin };
    for (int i = 0; i < 3; i++) {
        size_t n = strlen(lists[i]);
        if (n && lists[i][n - 1] != ';' && n < 255)
            strcpy(lists[i] + n, ";");
    }
    /* Entries meant for other desktops (OnlyShowIn=GNOME;...) are skipped. */
    if (hidden || strcmp(type, "Application") || !e.pub.name[0] || !e.exec[0] ||
        (only[0] && !list_has(only, "SkarletOS")) || list_has(notin, "SkarletOS") ||
        (tryexec[0] && !on_path(tryexec)))
        return;
    if (!e.pub.comment[0])
        k_strlcpy(e.pub.comment, generic[0] ? generic : e.pub.name, sizeof e.pub.comment);
    k_strlcpy(e.id, id, sizeof e.id);
    if (!e.wmclass[0]) {
        /* Without StartupWMClass, windows usually carry the program's name. */
        char prog[sizeof e.exec];
        snprintf(prog, sizeof prog, "%s", e.exec);
        prog[strcspn(prog, " ")] = 0;
        const char *base = strrchr(prog, '/');
        k_strlcpy(e.wmclass, base ? base + 1 : prog, sizeof e.wmclass);
    }
    classify(&e, cats);
    /* A web browser is a favourite, as in most desktops' default setup. */
    e.pub.favorite = list_has(cats, "WebBrowser");
    /* A later folder (the user's own) overrides an earlier one with the same id. */
    for (int i = 0; i < napps; i++)
        if (!strcmp(apps[i].id, e.id)) {
            apps[i] = e;
            return;
        }
    if (napps < MAX_APPS)
        apps[napps++] = e;
}

static int by_name(const void *a, const void *b)
{
    return strcasecmp(((const struct entry *)a)->pub.name, ((const struct entry *)b)->pub.name);
}

static void scan(void)
{
    napps = 0;
    for (int d = 0; d < ARRAY_LEN(dirs); d++) {
        const char *dir = dirs[d] ? dirs[d] : user_dir; /* the user's own come last */
        DIR *dh = dir[0] ? opendir(dir) : 0;
        if (!dh)
            continue;
        struct dirent *de;
        while ((de = readdir(dh))) {
            size_t len = strlen(de->d_name);
            if (len < 9 || strcmp(de->d_name + len - 8, ".desktop"))
                continue;
            char path[1024], id[64];
            snprintf(path, sizeof path, "%s/%s", dir, de->d_name);
            snprintf(id, sizeof id, "%.*s", (int)(len - 8), de->d_name);
            read_file(path, id);
        }
        closedir(dh);
    }
    qsort(apps, (size_t)napps, sizeof apps[0], by_name);
    /* Only one favourite browser: the first in name order. */
    for (int i = 0, seen = 0; i < napps; i++)
        if (apps[i].pub.favorite)
            apps[i].pub.favorite = !seen++;
}

static int folders_changed(void)
{
    int changed = 0;
    for (int d = 0; d < ARRAY_LEN(dirs); d++) {
        const char *dir = dirs[d] ? dirs[d] : user_dir;
        struct stat st;
        time_t m = dir[0] && stat(dir, &st) == 0 ? st.st_mtime : 0;
        if (m != dir_mtime[d]) {
            dir_mtime[d] = m;
            changed = 1;
        }
    }
    return changed;
}

void desktop_files_init(void)
{
    const char *home = getenv("HOME");
    if (home)
        snprintf(user_dir, sizeof user_dir, "%s/.local/share/applications", home);
    folders_changed();
    scan();
}

/* Called every few seconds: has anything been installed or removed? */
void desktop_files_poll(void)
{
    if (folders_changed()) {
        scan();
        g_ws.need_redraw = 1;
    }
}

int plat_app_count(void) { return napps; }

const struct installed_app *plat_app(int i)
{
    return i >= 0 && i < napps ? &apps[i].pub : 0;
}

void plat_app_start(int i)
{
    if (i < 0 || i >= napps)
        return;
    if (apps[i].terminal) {
        svc_launch(APP_TERMINAL, apps[i].exec);
    } else if (linux_run(apps[i].exec) < 0) {
        svc_notify("SkarletOS", "Could not start the program.");
    } else {
        char msg[96];
        k_snprintf(msg, sizeof msg, "Starting %s...", apps[i].pub.name);
        svc_notify("SkarletOS", msg);
    }
}

/* The icon and colour for a window, from its WM_CLASS (instance, class). */
int desktop_files_match(const char *instance, const char *klass, int *icon, uint32_t *color)
{
    for (int i = 0; i < napps; i++) {
        const char *c = apps[i].wmclass;
        if ((instance && !strcasecmp(c, instance)) || (klass && !strcasecmp(c, klass)) ||
            (klass && !strcasecmp(apps[i].id, klass))) {
            *icon = apps[i].pub.icon;
            *color = apps[i].pub.color;
            return 1;
        }
    }
    return 0;
}
