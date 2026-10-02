/* linux.c - starting programs, and the login check, for the Linux session. */
#define _GNU_SOURCE
#include <fcntl.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "../src/desktop.h"
#include "linux.h"

void linux_init(void) {}

int linux_run(const char *cmd)
{
    pid_t pid = fork();
    if (pid < 0)
        return -1;
    if (pid == 0) {
        setsid(); /* its own session: it lives on if SkarletOS restarts */
        int null = open("/dev/null", O_RDWR);
        if (null >= 0) {
            dup2(null, 0);
            dup2(null, 1);
            if (null > 2)
                close(null);
        }
        execl("/bin/sh", "sh", "-c", cmd, (char *)0);
        _exit(127);
    }
    return (int)pid;
}

int linux_add_fds(fd_set *fds, int maxfd)
{
    (void)fds;
    return maxfd;
}

void linux_read_fds(fd_set *fds) { (void)fds; }
