/* services.h - what the desktop offers to programs (the shell, apps, widgets).
 *
 * In a real KDE system programs talk to the workspace over D-Bus.  SkarletOS
 * has no processes or IPC, so these are plain function calls implemented in
 * workspace.c and wm.c. */
#ifndef SKARLET_SERVICES_H
#define SKARLET_SERVICES_H

#include <stdint.h>

/* Apps that can be launched. The ids double as shell command names. */
enum app_id { APP_TERMINAL, APP_FILES, APP_WRITE, APP_SETTINGS, APP_MONITOR, APP_COUNT };
/* Windows of other programs (wm_open_external) use this pseudo-app; it is
 * not listed in the launcher. g_apps and g_app_impl have APP_SLOTS entries. */
#define APP_EXTERNAL APP_COUNT
#define APP_SLOTS (APP_COUNT + 1)

struct app_info {
    const char *id;      /* "skterm" */
    const char *name;    /* "Skarlet Terminal" */
    const char *generic; /* "Terminal" */
    const char *category;
    int icon;            /* IC_* icon used in the panel and launcher */
    uint32_t color;      /* its app-icon tile colour */
};
extern const struct app_info g_apps[APP_SLOTS];

/* Launch an app. arg is app specific: a directory for Skarlet Files, a file for
 * Skarlet Write, a command line to run for Skarlet Terminal. Returns the window pid or -1. */
int  svc_launch(int app, const char *arg);
int  svc_app_by_name(const char *name); /* "skterm" -> APP_TERMINAL, else -1 */
void svc_notify(const char *title, const char *text);
int  svc_kill(int pid);
/* Calls fn once per window: pid, title, app, virtual desktop. */
void svc_each_window(void (*fn)(void *ctx, int pid, const char *title, int app, int desk),
                     void *ctx);
uint32_t svc_uptime(void); /* seconds since boot */
void svc_reboot(void);
void svc_poweroff(void);

#endif
