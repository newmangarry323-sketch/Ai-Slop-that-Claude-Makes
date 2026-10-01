/* services.h - what the desktop offers to programs (the shell, apps, widgets).
 *
 * In a real KDE system programs talk to the workspace over D-Bus.  Plasmix
 * has no processes or IPC, so these are plain function calls implemented in
 * workspace.c and wm.c. */
#ifndef PLASMIX_SERVICES_H
#define PLASMIX_SERVICES_H

#include <stdint.h>

/* Apps that can be launched. The ids double as shell command names. */
enum app_id { APP_KONSOLE, APP_DOLPHIN, APP_KWRITE, APP_SETTINGS, APP_SYSMON, APP_COUNT };

struct app_info {
    const char *id;      /* "konsole" */
    const char *name;    /* "Konsole" */
    const char *generic; /* "Terminal" */
    const char *category;
};
extern const struct app_info g_apps[APP_COUNT];

/* Launch an app. arg is app specific: a directory for Dolphin, a file for
 * KWrite, a command line to run for Konsole. Returns the window pid or -1. */
int  svc_launch(int app, const char *arg);
int  svc_app_by_name(const char *name); /* "konsole" -> APP_KONSOLE, else -1 */
void svc_notify(const char *title, const char *text);
int  svc_kill(int pid);
/* Calls fn once per window: pid, title, app, virtual desktop. */
void svc_each_window(void (*fn)(void *ctx, int pid, const char *title, int app, int desk),
                     void *ctx);
uint32_t svc_uptime(void); /* seconds since boot */
void svc_reboot(void);
void svc_poweroff(void);

#endif
