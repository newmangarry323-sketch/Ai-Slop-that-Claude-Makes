/* shell.h - a small Bourne-style command shell. */
#ifndef SKARLET_SHELL_H
#define SKARLET_SHELL_H

#define SH_LINE 160
#define SH_HIST 16

struct shell {
    int cwd; /* current directory (a vfs node) */
    char hist[SH_HIST][SH_LINE];
    int nhist;
    void (*write)(void *ctx, const char *s); /* where output goes (Skarlet Terminal) */
    void *ctx;
    int want_exit;  /* set by "exit" */
    int want_clear; /* set by "clear" */
    int self_pid;   /* window pid of the terminal running us */
};

void shell_init(struct shell *sh, void (*write)(void *, const char *), void *ctx);
void shell_exec(struct shell *sh, const char *line);
void shell_prompt(struct shell *sh, char *out, int size);
/* Names of all built-in commands (NULL terminated), for /bin and "help". */
extern const char *const g_shell_commands[];

#endif
