/* linux.h - pieces shared by the Linux session's files. */
#ifndef SKARLET_LINUX_H
#define SKARLET_LINUX_H

#include <stdint.h>
#include <sys/select.h>

#define MAX_LAYERS_X 8 /* layers the session can show at once */

void linux_init(void);
/* Start a command with /bin/sh in the background; returns its pid or -1. */
int  linux_run(const char *cmd);
/* The main loop waits on these file descriptors as well (terminals). */
int  linux_add_fds(fd_set *fds, int maxfd);
void linux_read_fds(fd_set *fds);

/* Called on every pass of the main loop. */
void linux_tick(void);

/* config.c: the settings and widgets, kept in ~/.config/skarletos. */
void config_load(void);
void config_save_if_changed(void);

/* desktop_files.c: installed programs, for the launcher. */
void desktop_files_init(void);
void desktop_files_poll(void);
int  desktop_files_match(const char *instance, const char *klass, int *icon, uint32_t *color);

/* term.c: Skarlet Terminal with a real shell. */
struct app;
extern const struct app linux_terminal_app;
extern const struct app linux_monitor_app; /* monitor.c: real processes */
int  term_add_fds(fd_set *fds, int maxfd);
void term_read_fds(fd_set *fds);

#endif
