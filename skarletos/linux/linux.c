/* linux.c - starting programs, and the login check, for the Linux session. */
#define _GNU_SOURCE
#include <fcntl.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include <pwd.h>
#include <stdio.h>
#ifdef HAVE_PAM
#include <security/pam_appl.h>
#endif

#include "../src/desktop.h"
#include "../src/lib.h"
#include "linux.h"

void linux_init(void)
{
    /* Swap in the Linux versions of the apps that need the real system. */
    g_app_impl[APP_TERMINAL] = &linux_terminal_app;
    g_app_impl[APP_MONITOR] = &linux_monitor_app;
    desktop_files_init();
}

void linux_tick(void)
{
    static time_t last;
    time_t now = time(0);
    if (now - last >= 2) {
        last = now;
        desktop_files_poll(); /* newly installed programs */
        config_save_if_changed();
    }
}

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

int linux_add_fds(fd_set *fds, int maxfd) { return term_add_fds(fds, maxfd); }

void linux_read_fds(fd_set *fds) { term_read_fds(fds); }

/* ---- the login screen ------------------------------------------------------
 * The SkarletOS login screen checks the real password of the user who is
 * logged in, through PAM ("Pluggable Authentication Modules", how Linux
 * programs such as login and sudo check passwords).  The PAM service is
 * /etc/pam.d/skarletos.  Built without PAM, any password is accepted. */

#ifdef HAVE_PAM
static int pam_answer(int n, const struct pam_message **msg, struct pam_response **resp,
                      void *data)
{
    struct pam_response *r = calloc((size_t)n, sizeof *r);
    if (!r)
        return PAM_BUF_ERR;
    for (int i = 0; i < n; i++)
        if (msg[i]->msg_style == PAM_PROMPT_ECHO_OFF || msg[i]->msg_style == PAM_PROMPT_ECHO_ON)
            r[i].resp = strdup((const char *)data);
    *resp = r;
    return PAM_SUCCESS;
}
#endif

int plat_login(const char *password)
{
#ifdef HAVE_PAM
    struct passwd *pw = getpwuid(getuid());
    if (!pw)
        return 0;
    struct pam_conv conv = { pam_answer, (void *)password };
    pam_handle_t *h = 0;
    int ok = pam_start("skarletos", pw->pw_name, &conv, &h) == PAM_SUCCESS &&
             pam_authenticate(h, 0) == PAM_SUCCESS && pam_acct_mgmt(h, 0) == PAM_SUCCESS;
    if (h)
        pam_end(h, ok ? PAM_SUCCESS : PAM_AUTH_ERR);
    return ok;
#else
    (void)password;
    return 1;
#endif
}

/* The hint under the password field: /etc/skarletos/login-hint if there is
 * one (the live system says what its password is there). */
const char *plat_login_hint(void)
{
    static char hint[96];
    FILE *f = fopen("/etc/skarletos/login-hint", "r");
    if (f) {
        if (fgets(hint, sizeof hint, f))
            hint[strcspn(hint, "\n")] = 0;
        fclose(f);
        if (hint[0])
            return hint;
    }
#ifdef HAVE_PAM
    return "Enter your password and press Enter";
#else
    return "Press Enter to log in (any password)";
#endif
}
