/* sh_repl.c - just the Plasmix shell and file system, as a normal command-line
 * program.  Handy for practising the commands, and for recording examples:
 *
 *     ./build/plasmix-sh                 interactive
 *     ./build/plasmix-sh < script.txt    each line is echoed after the prompt
 *
 * It runs the same src/shell.c and src/vfs.c as the kernel.  Commands that open
 * windows (konsole, dolphin, kwrite...) still work: the windows exist (see
 * "ps"), you just cannot see them here.
 */
#include <stdio.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include "../src/desktop.h"

int plat_key_poll(struct key *k) { (void)k; return 0; }
void plat_present(const uint16_t *cells) { (void)cells; }
uint32_t plat_mem_kib(void) { return 0; }
void plat_reboot(void) { puts("(the system would reboot now)"); }
void plat_poweroff(void) { puts("(the system would power off now)"); }

void plat_time(struct datetime *t)
{
    time_t now = time(NULL);
    struct tm tm;
    localtime_r(&now, &tm);
    *t = (struct datetime){ tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday,
                            tm.tm_hour, tm.tm_min, tm.tm_sec };
}

static void write_out(void *ctx, const char *s)
{
    (void)ctx;
    fputs(s, stdout);
}

int main(void)
{
    int interactive = isatty(STDIN_FILENO);
    ws_init(); /* builds the file system and the desktop state */
    g_ws.phase = PHASE_DESKTOP;

    struct shell sh;
    shell_init(&sh, write_out, NULL);
    char line[SH_LINE], prompt[VFS_PATH_MAX + 32];
    for (;;) {
        shell_prompt(&sh, prompt, sizeof prompt);
        fputs(prompt, stdout);
        fflush(stdout);
        if (!fgets(line, sizeof line, stdin))
            break;
        line[strcspn(line, "\n")] = 0;
        if (!interactive)
            puts(line); /* show the command, like a terminal transcript */
        shell_exec(&sh, line);
        if (sh.want_clear) {
            sh.want_clear = 0;
            fputs("\033[H\033[2J", stdout);
        }
        if (sh.want_exit)
            break;
    }
    putchar('\n');
    return 0;
}
