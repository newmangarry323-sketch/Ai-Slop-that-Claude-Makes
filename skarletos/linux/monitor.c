/* monitor.c - Skarlet Monitor on Linux: the real processes, from /proc.
 *
 * Linux describes every running process as a folder in /proc (the "proc
 * file system"): /proc/1234/stat has its name, state and CPU time,
 * /proc/1234/status its owner and memory.  /proc/stat and /proc/meminfo
 * hold the totals for the whole machine.  Skarlet Monitor reads them every
 * two seconds; CPU use is the change in CPU time between two readings.
 */
#define _GNU_SOURCE
#include <dirent.h>
#include <errno.h>
#include <pwd.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/statvfs.h>
#include <time.h>
#include <unistd.h>

#include "../src/desktop.h"
#include "../src/gfx.h"
#include "../src/lib.h"
#include "linux.h"

#define MAX_PROCS 2048

struct proc {
    int pid;
    char name[32];
    char user[24];
    unsigned long long ticks; /* CPU time so far (user + system) */
    long rss_kib;
    int cpu10;                /* CPU use in tenths of a percent */
};

static struct proc procs[MAX_PROCS], before[MAX_PROCS];
static int nprocs, nbefore;
static unsigned long long total_ticks, total_before, idle_ticks, idle_before;
static long mem_total, mem_avail; /* KiB */
static int cpu_pct;
static time_t last_read;

static void read_totals(void)
{
    FILE *f = fopen("/proc/stat", "r");
    if (f) {
        unsigned long long v[8] = { 0 };
        if (fscanf(f, "cpu %llu %llu %llu %llu %llu %llu %llu %llu", &v[0], &v[1], &v[2], &v[3],
                   &v[4], &v[5], &v[6], &v[7]) >= 4) {
            total_before = total_ticks, idle_before = idle_ticks;
            total_ticks = v[0] + v[1] + v[2] + v[3] + v[4] + v[5] + v[6] + v[7];
            idle_ticks = v[3] + v[4];
        }
        fclose(f);
    }
    unsigned long long dt = total_ticks - total_before, di = idle_ticks - idle_before;
    cpu_pct = dt ? (int)(100 - di * 100 / dt) : 0;
    f = fopen("/proc/meminfo", "r");
    if (f) {
        char key[64];
        long val;
        while (fscanf(f, "%63s %ld kB\n", key, &val) == 2) {
            if (!strcmp(key, "MemTotal:"))
                mem_total = val;
            else if (!strcmp(key, "MemAvailable:"))
                mem_avail = val;
        }
        fclose(f);
    }
}

static void user_name(uid_t uid, char *out, int size)
{
    static uid_t last_uid = (uid_t)-1;
    static char last[24];
    if (uid != last_uid) {
        struct passwd *pw = getpwuid(uid);
        if (pw)
            k_strlcpy(last, pw->pw_name, sizeof last);
        else
            snprintf(last, sizeof last, "%u", (unsigned)uid);
        last_uid = uid;
    }
    k_strlcpy(out, last, size);
}

static int by_cpu_then_memory(const void *a, const void *b)
{
    const struct proc *x = a, *y = b;
    if (x->cpu10 != y->cpu10)
        return y->cpu10 - x->cpu10;
    return y->rss_kib > x->rss_kib ? 1 : y->rss_kib < x->rss_kib ? -1 : 0;
}

static void read_procs(void)
{
    memcpy(before, procs, sizeof procs[0] * (size_t)nprocs);
    nbefore = nprocs;
    unsigned long long prev_total = total_ticks;
    read_totals();
    unsigned long long dt = total_ticks - prev_total;
    long ncpu = sysconf(_SC_NPROCESSORS_ONLN);
    nprocs = 0;
    DIR *d = opendir("/proc");
    if (!d)
        return;
    struct dirent *de;
    while ((de = readdir(d)) && nprocs < MAX_PROCS) {
        int pid = atoi(de->d_name);
        if (pid <= 0)
            continue;
        char path[64], buf[1024];
        snprintf(path, sizeof path, "/proc/%d/stat", pid);
        FILE *f = fopen(path, "r");
        if (!f)
            continue;
        size_t n = fread(buf, 1, sizeof buf - 1, f);
        fclose(f);
        buf[n] = 0;
        /* "pid (name) state ...": the name may contain spaces and ')'. */
        char *open = strchr(buf, '('), *close = strrchr(buf, ')');
        if (!open || !close)
            continue;
        struct proc *p = &procs[nprocs];
        memset(p, 0, sizeof *p);
        p->pid = pid;
        snprintf(p->name, sizeof p->name, "%.*s", (int)(close - open - 1), open + 1);
        unsigned long utime = 0, stime = 0;
        long rss_pages = 0;
        /* Fields after the name: state(3) ... utime(14) stime(15) ... rss(24). */
        sscanf(close + 2, "%*c %*d %*d %*d %*d %*d %*u %*u %*u %*u %*u %lu %lu %*d %*d %*d %*d %*d %*d %*u %*u %ld",
               &utime, &stime, &rss_pages);
        p->ticks = utime + stime;
        p->rss_kib = rss_pages * (sysconf(_SC_PAGESIZE) / 1024);
        struct stat_owner {
            uid_t uid;
        } owner = { 0 };
        snprintf(path, sizeof path, "/proc/%d/status", pid);
        f = fopen(path, "r");
        if (f) {
            while (fgets(buf, sizeof buf, f))
                if (sscanf(buf, "Uid: %u", &owner.uid) == 1)
                    break;
            fclose(f);
        }
        user_name(owner.uid, p->user, sizeof p->user);
        for (int i = 0; i < nbefore; i++)
            if (before[i].pid == pid && dt) {
                p->cpu10 = (int)((p->ticks - before[i].ticks) * 1000 * (unsigned long long)ncpu / dt);
                break;
            }
        nprocs++;
    }
    closedir(d);
    qsort(procs, (size_t)nprocs, sizeof procs[0], by_cpu_then_memory);
}

static void refresh_if_due(void)
{
    time_t now = time(0);
    if (now - last_read >= 2) {
        last_read = now;
        read_procs();
        g_ws.need_redraw = 1;
    }
}

/* ---- the window ---------------------------------------------------------------- */

#define ROW 30
#define TOP 124

static void lm_init(struct window *w, const char *arg)
{
    (void)arg;
    k_strlcpy(w->title, "Skarlet Monitor", sizeof w->title);
    last_read = 0;
    read_procs();
    w->s.files.top = 0; /* first row shown */
    w->s.files.sel = 0;
}

static void lm_idle(struct window *w)
{
    (void)w;
    refresh_if_due();
}

static void stat_card(int x, int y, int w, const char *label, const char *value)
{
    gfx_rrect(x, y, w, 62, 10, g_theme->input, 255);
    gfx_text(&font_small, x + 14, y + 10, label, g_theme->text_dim, 255);
    gfx_text(&font_title, x + 14, y + 28, value, g_theme->text, 255);
}

static int visible_rows(int ch) { return MAX(1, (ch - TOP - 36) / ROW); }

static void lm_draw(struct window *w, int x, int y, int cw, int ch, int focused)
{
    const struct theme *t = g_theme;
    char v[48];
    int cardw = (cw - 48 - 3 * 12) / 4;
    long up = 0;
    FILE *f = fopen("/proc/uptime", "r");
    if (f) {
        double secs = 0;
        if (fscanf(f, "%lf", &secs) == 1)
            up = (long)secs;
        fclose(f);
    }
    k_snprintf(v, sizeof v, "%d:%02d", (int)(up / 3600), (int)(up / 60 % 60));
    stat_card(x + 24, y + 16, cardw, "Up (hours:minutes)", v);
    k_snprintf(v, sizeof v, "%d%%", cpu_pct);
    stat_card(x + 24 + cardw + 12, y + 16, cardw, "CPU", v);
    k_snprintf(v, sizeof v, "%d / %d MiB", (int)((mem_total - mem_avail) / 1024),
               (int)(mem_total / 1024));
    stat_card(x + 24 + 2 * (cardw + 12), y + 16, cardw, "Memory", v);
    k_snprintf(v, sizeof v, "%d", nprocs);
    stat_card(x + 24 + 3 * (cardw + 12), y + 16, cardw, "Processes", v);

    int hy = y + 96;
    int cpux = x + cw - 220, memx = x + cw - 130;
    gfx_text(&font_small, x + 28, hy, "PID", t->text_dim, 255);
    gfx_text(&font_small, x + 110, hy, "Name", t->text_dim, 255);
    gfx_text(&font_small, x + 330, hy, "User", t->text_dim, 255);
    gfx_text_right(&font_small, cpux + 60, hy, "CPU", t->text_dim, 255);
    gfx_text_right(&font_small, memx + 100, hy, "Memory", t->text_dim, 255);
    gfx_rect(x + 20, hy + 20, cw - 40, 1, t->divider, 255);

    int *sel = &w->s.files.sel, *top = &w->s.files.top, rows = visible_rows(ch);
    if (*sel >= nprocs)
        *sel = MAX(nprocs - 1, 0);
    if (*sel < *top)
        *top = *sel;
    if (*sel >= *top + rows)
        *top = *sel - rows + 1;
    for (int r = 0; r < rows && *top + r < nprocs; r++) {
        struct proc *p = &procs[*top + r];
        int ry = y + TOP + r * ROW, on = *top + r == *sel;
        if (on)
            gfx_rrect(x + 12, ry, cw - 24, ROW - 2, 7, focused ? t->accent : t->hover, 255);
        uint32_t tc = on && focused ? t->text_on_accent : t->text;
        char num[24];
        k_snprintf(num, sizeof num, "%d", p->pid);
        gfx_text(&font_ui, x + 28, ry + 5, num, tc, 255);
        gfx_text_fit(&font_ui, x + 110, ry + 5, 210, p->name, tc, 255);
        gfx_text_fit(&font_ui, x + 330, ry + 5, cpux - x - 340, p->user, tc, 255);
        k_snprintf(num, sizeof num, "%d.%d%%", p->cpu10 / 10, p->cpu10 % 10);
        gfx_text_right(&font_ui, cpux + 60, ry + 5, num, tc, 255);
        k_snprintf(num, sizeof num, "%d MiB", (int)(p->rss_kib / 1024));
        gfx_text_right(&font_ui, memx + 100, ry + 5, num, tc, 255);
    }
    gfx_text(&font_small, x + 28, y + ch - 24,
             "Up/Down: choose    Delete: end the selected process (it may ask to save)",
             t->text_dim, 255);
}

static void end_selected(struct window *w)
{
    int sel = w->s.files.sel;
    if (sel >= nprocs)
        return;
    char msg[96];
    if (kill(procs[sel].pid, SIGTERM) == 0)
        k_snprintf(msg, sizeof msg, "Asked %s (%d) to end.", procs[sel].name, procs[sel].pid);
    else
        k_snprintf(msg, sizeof msg, "Could not end %s: %s", procs[sel].name, strerror(errno));
    svc_notify("Skarlet Monitor", msg);
    last_read = 0;
}

static void lm_key(struct window *w, struct key k)
{
    int *sel = &w->s.files.sel;
    if (k.code == K_UP && *sel > 0)
        (*sel)--;
    else if (k.code == K_DOWN && *sel < nprocs - 1)
        (*sel)++;
    else if (k.code == K_PGUP)
        *sel = MAX(*sel - 10, 0);
    else if (k.code == K_PGDN)
        *sel = MIN(*sel + 10, MAX(nprocs - 1, 0));
    else if (k.code == K_HOME)
        *sel = 0;
    else if (k.code == K_DELETE)
        end_selected(w);
}

static void lm_mouse(struct window *w, struct mouse m, int cw, int ch)
{
    (void)cw;
    if (m.type == MOUSE_WHEEL) {
        for (int i = 0; i < 3; i++)
            lm_key(w, (struct key){ m.button == 4 ? K_UP : K_DOWN, 0 });
        return;
    }
    int r = (m.y - TOP) / ROW;
    if (m.type == MOUSE_DOWN && m.button == 1 && m.y >= TOP && r < visible_rows(ch) &&
        w->s.files.top + r < nprocs)
        w->s.files.sel = w->s.files.top + r;
}

const struct app linux_monitor_app = {
    .w = 820, .h = 560, .init = lm_init, .draw = lm_draw, .key = lm_key, .idle = lm_idle,
    .mouse = lm_mouse,
};

/* ---- the System Monitor widget's bars ------------------------------------------- */

int plat_usage(struct usage_bar *out, int max)
{
    refresh_if_due();
    int n = 0;
    if (n < max) {
        k_strlcpy(out[n].label, "CPU", sizeof out[n].label);
        out[n++].pct = cpu_pct;
    }
    if (n < max) {
        k_strlcpy(out[n].label, "Memory", sizeof out[n].label);
        out[n++].pct = mem_total ? (int)((mem_total - mem_avail) * 100 / mem_total) : 0;
    }
    struct statvfs fs;
    if (n < max && statvfs("/", &fs) == 0 && fs.f_blocks) {
        k_strlcpy(out[n].label, "Disk", sizeof out[n].label);
        out[n++].pct = (int)((fs.f_blocks - fs.f_bfree) * 100 / fs.f_blocks);
    }
    return n;
}
