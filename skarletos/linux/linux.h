/* linux.h - pieces shared by the Linux session's files. */
#ifndef SKARLET_LINUX_H
#define SKARLET_LINUX_H

#include <sys/select.h>

#define MAX_LAYERS_X 8 /* layers the session can show at once */

void linux_init(void);
/* Start a command with /bin/sh in the background; returns its pid or -1. */
int  linux_run(const char *cmd);
/* The main loop waits on these file descriptors as well (terminals). */
int  linux_add_fds(fd_set *fds, int maxfd);
void linux_read_fds(fd_set *fds);

#endif
