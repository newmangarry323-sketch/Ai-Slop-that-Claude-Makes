/* shell.c - the command interpreter that runs inside Skarlet Terminal.
 *
 * Each command line is split into words (honouring "double" and 'single'
 * quotes), an optional "> file" or ">> file" redirection is pulled out, and
 * the first word picks a built-in command from the table at the bottom.
 * A real UNIX shell would fork() and exec() a program from /bin; SkarletOS has
 * no processes, so every command is a C function.
 */
#include "shell.h"
#include "lib.h"
#include "platform.h"
#include "services.h"
#include "vfs.h"

#define MAX_ARGS 16

struct ctx {
    struct shell *sh;
    int argc;
    char *argv[MAX_ARGS];
    /* When output is redirected it is collected here, then written to a file. */
    int redirect;
    char out[VFS_FILE_MAX];
    int out_len;
};

static void out(struct ctx *c, const char *s)
{
    if (c->redirect) {
        int n = k_strlen(s);
        if (c->out_len + n > (int)sizeof c->out)
            n = (int)sizeof c->out - c->out_len;
        k_memcpy(c->out + c->out_len, s, n);
        c->out_len += n;
    } else {
        c->sh->write(c->sh->ctx, s);
    }
}

static void outf(struct ctx *c, const char *fmt, ...) __attribute__((format(printf, 2, 3)));
static void outf(struct ctx *c, const char *fmt, ...)
{
    char buf[256];
    va_list ap;
    va_start(ap, fmt);
    k_vsnprintf(buf, sizeof buf, fmt, ap);
    va_end(ap);
    out(c, buf);
}

static int fail(struct ctx *c, const char *what, int err)
{
    outf(c, "%s: %s: %s\n", c->argv[0], what, vfs_strerror(err));
    return 1;
}

/* ---- helpers ------------------------------------------------------------ */

static const char *const month_names[] = {
    "Jan", "Feb", "Mar", "Apr", "May", "Jun", "Jul", "Aug", "Sep", "Oct", "Nov", "Dec",
};

/* Day of week (0 = Sunday) using Sakamoto's method. */
static int weekday(int y, int m, int d)
{
    static const int t[] = { 0, 3, 2, 5, 0, 3, 5, 1, 4, 6, 2, 4 };
    if (m < 3)
        y -= 1;
    return (y + y / 4 - y / 100 + y / 400 + t[m - 1] + d) % 7;
}

static void mode_string(int node, char *s)
{
    int m = vfs_mode(node);
    s[0] = vfs_is_dir(node) ? 'd' : '-';
    const char *rwx = "rwxrwxrwx";
    for (int i = 0; i < 9; i++)
        s[1 + i] = (m & (0400 >> i)) ? rwx[i] : '-';
    if (m & 01000)
        s[9] = 't'; /* sticky bit, as on /tmp */
    s[10] = 0;
}

static const char *owner_of(int node)
{
    char path[VFS_PATH_MAX];
    vfs_path(node, path, sizeof path);
    return k_strncmp(path, "/home/user", 10) == 0 ? "user" : "root";
}

/* ---- commands ----------------------------------------------------------- */

static int cmd_help(struct ctx *c)
{
    out(c, "Built-in commands:\n");
    for (int i = 0; g_shell_commands[i]; i++) {
        outf(c, "  %-10s", g_shell_commands[i]);
        if (i % 6 == 5)
            out(c, "\n");
    }
    out(c, "\nRedirect output with > file or >> file. Apps: skterm, skfiles,\n"
           "skwrite FILE, sksettings, skmonitor.\n");
    return 0;
}

static void ls_one(struct ctx *c, int node, int longfmt)
{
    if (longfmt) {
        char mode[11];
        mode_string(node, mode);
        const char *own = owner_of(node);
        outf(c, "%s %-4s %-4s %5d %s%s\n", mode, own, own, vfs_size(node), vfs_name(node),
             vfs_is_dir(node) ? "/" : "");
    } else {
        outf(c, "%s%s  ", vfs_name(node), vfs_is_dir(node) ? "/" : "");
    }
}

static int cmd_ls(struct ctx *c)
{
    int longfmt = 0, all = 0, status = 0;
    const char *paths[MAX_ARGS];
    int npaths = 0;
    for (int i = 1; i < c->argc; i++) {
        if (c->argv[i][0] == '-') {
            for (const char *f = c->argv[i] + 1; *f; f++) {
                if (*f == 'l')
                    longfmt = 1;
                else if (*f == 'a')
                    all = 1;
            }
        } else {
            paths[npaths++] = c->argv[i];
        }
    }
    if (npaths == 0)
        paths[npaths++] = ".";

    for (int i = 0; i < npaths; i++) {
        const char *p = paths[i];
        int n = vfs_lookup(c->sh->cwd, p);
        if (n < 0) {
            status = fail(c, p, n);
            continue;
        }
        if (npaths > 1)
            outf(c, "%s:\n", p);
        if (!vfs_is_dir(n)) {
            ls_one(c, n, longfmt);
        } else {
            int kids[VFS_MAX_NODES];
            int count = vfs_list(n, kids, VFS_MAX_NODES);
            for (int k = 0; k < count; k++)
                if (all || vfs_name(kids[k])[0] != '.')
                    ls_one(c, kids[k], longfmt);
        }
        if (!longfmt)
            out(c, "\n");
    }
    return status;
}

static int cmd_cd(struct ctx *c)
{
    const char *p = c->argc > 1 ? c->argv[1] : "~";
    int n = vfs_lookup(c->sh->cwd, p);
    if (n < 0)
        return fail(c, p, n);
    if (!vfs_is_dir(n))
        return fail(c, p, VFS_ENOTDIR);
    c->sh->cwd = n;
    return 0;
}

static int cmd_pwd(struct ctx *c)
{
    char path[VFS_PATH_MAX];
    vfs_path(c->sh->cwd, path, sizeof path);
    outf(c, "%s\n", path);
    return 0;
}

static int cmd_cat(struct ctx *c)
{
    int status = 0;
    static char buf[VFS_FILE_MAX + 1];
    for (int i = 1; i < c->argc; i++) {
        int n = vfs_lookup(c->sh->cwd, c->argv[i]);
        int len = n < 0 ? n : vfs_read(n, buf, VFS_FILE_MAX);
        if (len < 0) {
            status = fail(c, c->argv[i], len);
            continue;
        }
        buf[len] = 0;
        out(c, buf);
    }
    return status;
}

static int cmd_echo(struct ctx *c)
{
    for (int i = 1; i < c->argc; i++) {
        out(c, c->argv[i]);
        if (i + 1 < c->argc)
            out(c, " ");
    }
    out(c, "\n");
    return 0;
}

static int cmd_mkdir(struct ctx *c)
{
    int status = 0;
    for (int i = 1; i < c->argc; i++) {
        int n = vfs_create(c->sh->cwd, c->argv[i], 1);
        if (n < 0)
            status = fail(c, c->argv[i], n);
    }
    return status;
}

static int cmd_touch(struct ctx *c)
{
    int status = 0;
    for (int i = 1; i < c->argc; i++) {
        if (vfs_lookup(c->sh->cwd, c->argv[i]) >= 0)
            continue; /* exists: nothing to do (we keep no timestamps) */
        int n = vfs_create(c->sh->cwd, c->argv[i], 0);
        if (n < 0)
            status = fail(c, c->argv[i], n);
    }
    return status;
}

static int remove_tree(int node)
{
    if (vfs_is_dir(node)) {
        int kids[VFS_MAX_NODES];
        int count = vfs_list(node, kids, VFS_MAX_NODES);
        for (int i = 0; i < count; i++) {
            int err = remove_tree(kids[i]);
            if (err < 0)
                return err;
        }
    }
    return vfs_remove(node);
}

static int cmd_rm(struct ctx *c)
{
    int recursive = 0, status = 0;
    for (int i = 1; i < c->argc; i++) {
        if (k_strcmp(c->argv[i], "-r") == 0 || k_strcmp(c->argv[i], "-rf") == 0) {
            recursive = 1;
            continue;
        }
        int n = vfs_lookup(c->sh->cwd, c->argv[i]);
        if (n < 0) {
            status = fail(c, c->argv[i], n);
            continue;
        }
        if (vfs_is_dir(n) && !recursive) {
            status = fail(c, c->argv[i], VFS_EISDIR);
            continue;
        }
        int err = recursive ? remove_tree(n) : vfs_remove(n);
        if (err < 0)
            status = fail(c, c->argv[i], err);
    }
    return status;
}

static int cmd_rmdir(struct ctx *c)
{
    int status = 0;
    for (int i = 1; i < c->argc; i++) {
        int n = vfs_lookup(c->sh->cwd, c->argv[i]);
        int err = n < 0 ? n : (!vfs_is_dir(n) ? VFS_ENOTDIR : vfs_remove(n));
        if (err < 0)
            status = fail(c, c->argv[i], err);
    }
    return status;
}

/* Work out where "cp/mv SRC DST" should put things: into DST if it is a
 * directory, otherwise DST is the new name. */
static int target(struct ctx *c, int src, const char *dst, int *parent, char *name)
{
    int d = vfs_lookup(c->sh->cwd, dst);
    if (d >= 0 && vfs_is_dir(d)) {
        *parent = d;
        k_strlcpy(name, vfs_name(src), VFS_NAME_MAX);
        return 0;
    }
    char tmp[VFS_PATH_MAX];
    k_strlcpy(tmp, dst, sizeof tmp);
    char *slash = 0;
    for (char *p = tmp; *p; p++)
        if (*p == '/')
            slash = p;
    if (!slash) {
        *parent = c->sh->cwd;
        k_strlcpy(name, tmp, VFS_NAME_MAX);
        return 0;
    }
    *slash = 0;
    k_strlcpy(name, slash + 1, VFS_NAME_MAX);
    *parent = vfs_lookup(c->sh->cwd, tmp[0] ? tmp : "/");
    return *parent < 0 ? *parent : 0;
}

static int cmd_cp(struct ctx *c)
{
    if (c->argc != 3) {
        out(c, "usage: cp SOURCE DEST\n");
        return 1;
    }
    int src = vfs_lookup(c->sh->cwd, c->argv[1]);
    if (src < 0)
        return fail(c, c->argv[1], src);
    if (vfs_is_dir(src))
        return fail(c, c->argv[1], VFS_EISDIR);
    int parent;
    char name[VFS_NAME_MAX], path[VFS_PATH_MAX];
    int err = target(c, src, c->argv[2], &parent, name);
    if (err < 0)
        return fail(c, c->argv[2], err);
    vfs_path(parent, path, sizeof path);
    k_strlcat(path, path[1] ? "/" : "", sizeof path);
    k_strlcat(path, name, sizeof path);
    int dst = vfs_lookup(VFS_ROOT, path);
    if (dst < 0)
        dst = vfs_create(VFS_ROOT, path, 0);
    if (dst < 0)
        return fail(c, c->argv[2], dst);
    static char buf[VFS_FILE_MAX];
    int len = vfs_read(src, buf, sizeof buf);
    vfs_write(dst, buf, len, 0);
    return 0;
}

static int cmd_mv(struct ctx *c)
{
    if (c->argc != 3) {
        out(c, "usage: mv SOURCE DEST\n");
        return 1;
    }
    int src = vfs_lookup(c->sh->cwd, c->argv[1]);
    if (src < 0)
        return fail(c, c->argv[1], src);
    int parent;
    char name[VFS_NAME_MAX];
    int err = target(c, src, c->argv[2], &parent, name);
    if (err == 0)
        err = vfs_rename(src, parent, name);
    return err < 0 ? fail(c, c->argv[2], err) : 0;
}

static int cmd_wc(struct ctx *c)
{
    static char buf[VFS_FILE_MAX];
    for (int i = 1; i < c->argc; i++) {
        int n = vfs_lookup(c->sh->cwd, c->argv[i]);
        int len = n < 0 ? n : vfs_read(n, buf, sizeof buf);
        if (len < 0)
            return fail(c, c->argv[i], len);
        int lines = 0, words = 0, in_word = 0;
        for (int k = 0; k < len; k++) {
            if (buf[k] == '\n')
                lines++;
            if (k_isspace(buf[k])) {
                in_word = 0;
            } else if (!in_word) {
                in_word = 1;
                words++;
            }
        }
        outf(c, "%4d %4d %5d %s\n", lines, words, len, c->argv[i]);
    }
    return 0;
}

static int cmd_uname(struct ctx *c)
{
    if (c->argc > 1 && k_strcmp(c->argv[1], "-a") == 0)
        out(c, "SkarletOS skarlet 0.1 #1 x86_64 SkarletOS\n");
    else
        out(c, "SkarletOS\n");
    return 0;
}

static int cmd_whoami(struct ctx *c)
{
    out(c, "user\n");
    return 0;
}

static int cmd_hostname(struct ctx *c)
{
    out(c, "skarlet\n");
    return 0;
}

static int cmd_date(struct ctx *c)
{
    static const char *const days[] = { "Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat" };
    struct datetime t;
    plat_time(&t);
    int m = (t.month >= 1 && t.month <= 12) ? t.month : 1;
    outf(c, "%s %s %2d %02d:%02d:%02d %d\n", days[weekday(t.year, m, t.day)],
         month_names[m - 1], t.day, t.hour, t.minute, t.second, t.year);
    return 0;
}

static int cmd_uptime(struct ctx *c)
{
    uint32_t s = svc_uptime();
    outf(c, "up %u min %u sec\n", s / 60, s % 60);
    return 0;
}

static int cmd_free(struct ctx *c)
{
    uint32_t total = plat_mem_kib();
    out(c, "            total   used\n");
    if (total)
        outf(c, "Mem (KiB): %6u      -\n", total);
    else
        out(c, "Mem (KiB):  unknown\n");
    outf(c, "Inodes:    %6d %6d\n", VFS_MAX_NODES, vfs_used());
    outf(c, "File data: %6d %6d bytes\n", VFS_MAX_NODES * VFS_FILE_MAX, vfs_bytes_used());
    return 0;
}

static void ps_row(void *ctx, int pid, const char *title, int app, int desk)
{
    outf((struct ctx *)ctx, "%5d %-10s %4d  %s\n", pid, g_apps[app].id, desk + 1, title);
}

static int cmd_ps(struct ctx *c)
{
    out(c, "  PID CMD        DESK  TITLE\n");
    outf(c, "%5d %-10s %4s  %s\n", 1, "skdesktop", "-", "SkarletOS Desktop Shell");
    svc_each_window(ps_row, c);
    return 0;
}

static int cmd_kill(struct ctx *c)
{
    if (c->argc < 2) {
        out(c, "usage: kill PID\n");
        return 1;
    }
    int pid = k_atoi(c->argv[1]);
    if (pid == 1) {
        out(c, "kill: refusing to kill the desktop shell\n");
        return 1;
    }
    if (svc_kill(pid) < 0) {
        outf(c, "kill: (%d) - No such process\n", pid);
        return 1;
    }
    return 0;
}

static int cmd_clear(struct ctx *c)
{
    c->sh->want_clear = 1;
    return 0;
}

static int cmd_exit(struct ctx *c)
{
    c->sh->want_exit = 1;
    return 0;
}

static int cmd_history(struct ctx *c)
{
    for (int i = 0; i < c->sh->nhist; i++)
        outf(c, "%4d  %s\n", i + 1, c->sh->hist[i]);
    return 0;
}

static int cmd_calc(struct ctx *c)
{
    char expr[SH_LINE] = "";
    for (int i = 1; i < c->argc; i++)
        k_strlcat(expr, c->argv[i], sizeof expr);
    int32_t v;
    if (k_eval(expr, &v) != 0) {
        out(c, "calc: invalid expression\n");
        return 1;
    }
    outf(c, "%d\n", v);
    return 0;
}

/* skdialog is modelled on KDE's kdialog, the tool shell scripts used to show
 * dialogs; we support its passive popup and message box modes as desktop
 * notifications. */
static int cmd_skdialog(struct ctx *c)
{
    if (c->argc >= 3 &&
        (k_strcmp(c->argv[1], "--msgbox") == 0 || k_strcmp(c->argv[1], "--passivepopup") == 0)) {
        svc_notify("Message", c->argv[2]);
        return 0;
    }
    out(c, "usage: skdialog --msgbox TEXT | --passivepopup TEXT\n");
    return 1;
}

static int launch(struct ctx *c, int app)
{
    const char *arg = c->argc > 1 ? c->argv[1] : 0;
    char path[VFS_PATH_MAX];
    if (arg && app != APP_TERMINAL) {
        /* Make relative paths absolute so the app sees the same file. */
        if (arg[0] != '/' && arg[0] != '~') {
            vfs_path(c->sh->cwd, path, sizeof path);
            k_strlcat(path, path[1] ? "/" : "", sizeof path);
            k_strlcat(path, arg, sizeof path);
            arg = path;
        }
    }
    if (!arg && app == APP_FILES) {
        vfs_path(c->sh->cwd, path, sizeof path);
        arg = path;
    }
    if (svc_launch(app, arg) < 0) {
        out(c, "Too many windows open.\n");
        return 1;
    }
    return 0;
}

static int cmd_skterm(struct ctx *c) { return launch(c, APP_TERMINAL); }
static int cmd_skfiles(struct ctx *c) { return launch(c, APP_FILES); }
static int cmd_skwrite(struct ctx *c) { return launch(c, APP_WRITE); }
static int cmd_settings(struct ctx *c) { return launch(c, APP_SETTINGS); }
static int cmd_skmonitor(struct ctx *c) { return launch(c, APP_MONITOR); }

static int cmd_reboot(struct ctx *c)
{
    (void)c;
    svc_reboot();
    return 0;
}

static int cmd_poweroff(struct ctx *c)
{
    (void)c;
    svc_poweroff();
    return 0;
}

struct command {
    const char *name;
    int (*fn)(struct ctx *c);
};

static const struct command commands[] = {
    { "calc", cmd_calc },       { "cat", cmd_cat },         { "cd", cmd_cd },
    { "clear", cmd_clear },     { "cp", cmd_cp },           { "date", cmd_date },
    { "skfiles", cmd_skfiles }, { "echo", cmd_echo },       { "exit", cmd_exit },
    { "free", cmd_free },       { "help", cmd_help },       { "history", cmd_history },
    { "hostname", cmd_hostname }, { "skdialog", cmd_skdialog }, { "kill", cmd_kill },
    { "skterm", cmd_skterm }, { "skmonitor", cmd_skmonitor }, { "skwrite", cmd_skwrite },
    { "ls", cmd_ls },           { "mkdir", cmd_mkdir },     { "mv", cmd_mv },
    { "poweroff", cmd_poweroff }, { "ps", cmd_ps },         { "pwd", cmd_pwd },
    { "reboot", cmd_reboot },   { "rm", cmd_rm },           { "rmdir", cmd_rmdir },
    { "sksettings", cmd_settings }, { "touch", cmd_touch }, { "uname", cmd_uname },
    { "uptime", cmd_uptime },   { "wc", cmd_wc },           { "whoami", cmd_whoami },
};

const char *const g_shell_commands[] = {
    "calc", "cat", "cd", "clear", "cp", "date", "echo", "exit", "free", "help",
    "history", "hostname", "kill", "ls", "mkdir", "mv", "poweroff", "ps", "pwd",
    "reboot", "rm", "rmdir", "skdialog", "skfiles", "skmonitor", "sksettings", "skterm",
    "skwrite", "touch", "uname", "uptime", "wc", "whoami", 0,
};

/* ---- parsing ------------------------------------------------------------ */

/* Split line into words in place. Quotes group words; > and >> are separate
 * words even when written without spaces ("echo hi>file"). */
static int tokenize(char *line, char **argv, char *store, int store_size)
{
    int argc = 0, used = 0;
    char *p = line;
    while (*p && argc < MAX_ARGS) {
        while (k_isspace(*p))
            p++;
        if (!*p)
            break;
        char *word = store + used;
        if (*p == '>') {
            word[0] = '>';
            int n = 1;
            if (p[1] == '>')
                word[n++] = '>';
            word[n] = 0;
            p += n;
            used += n + 1;
            argv[argc++] = word;
            continue;
        }
        int n = 0;
        while (*p && !k_isspace(*p) && *p != '>') {
            if (*p == '"' || *p == '\'') {
                char q = *p++;
                while (*p && *p != q && used + n < store_size - 2)
                    word[n++] = *p++;
                if (*p == q)
                    p++;
            } else if (used + n < store_size - 2) {
                word[n++] = *p++;
            } else {
                p++;
            }
        }
        word[n] = 0;
        used += n + 1;
        argv[argc++] = word;
    }
    return argc;
}

void shell_exec(struct shell *sh, const char *line)
{
    static struct ctx c;
    static char store[SH_LINE * 2];
    char copy[SH_LINE];

    while (k_isspace(*line))
        line++;
    if (!*line)
        return;

    /* Remember the line in the history (oldest entries scroll off). */
    if (sh->nhist == SH_HIST) {
        k_memmove(sh->hist[0], sh->hist[1], sizeof sh->hist[0] * (SH_HIST - 1));
        sh->nhist--;
    }
    k_strlcpy(sh->hist[sh->nhist++], line, SH_LINE);

    k_memset(&c, 0, sizeof c);
    c.sh = sh;
    k_strlcpy(copy, line, sizeof copy);
    c.argc = tokenize(copy, c.argv, store, sizeof store);

    /* Pull "> file" / ">> file" off the end of the command. */
    const char *redir_file = 0;
    int append = 0;
    for (int i = 0; i < c.argc; i++) {
        if (c.argv[i][0] == '>') {
            if (i + 1 >= c.argc) {
                sh->write(sh->ctx, "sh: syntax error near '>'\n");
                return;
            }
            append = c.argv[i][1] == '>';
            redir_file = c.argv[i + 1];
            c.argc = i;
            c.redirect = 1;
            break;
        }
    }
    if (c.argc == 0)
        return;

    const struct command *cmd = 0;
    for (int i = 0; i < ARRAY_LEN(commands); i++)
        if (k_strcmp(commands[i].name, c.argv[0]) == 0)
            cmd = &commands[i];
    if (!cmd) {
        char msg[SH_LINE + 32];
        k_snprintf(msg, sizeof msg, "sh: %s: command not found\n", c.argv[0]);
        sh->write(sh->ctx, msg);
        return;
    }
    cmd->fn(&c);

    if (redir_file) {
        int n = vfs_lookup(sh->cwd, redir_file);
        if (n < 0)
            n = vfs_create(sh->cwd, redir_file, 0);
        int err = n < 0 ? n : vfs_write(n, c.out, c.out_len, append);
        if (err < 0) {
            char msg[SH_LINE + 64];
            k_snprintf(msg, sizeof msg, "sh: %s: %s\n", redir_file, vfs_strerror(err));
            sh->write(sh->ctx, msg);
        }
    }
}

void shell_prompt(struct shell *sh, char *out_buf, int size)
{
    char path[VFS_PATH_MAX];
    vfs_path(sh->cwd, path, sizeof path);
    const char *home = "/home/user";
    if (k_strncmp(path, home, 10) == 0 && (path[10] == '/' || path[10] == 0))
        k_snprintf(out_buf, size, "user@skarlet:~%s$ ", path + 10);
    else
        k_snprintf(out_buf, size, "user@skarlet:%s$ ", path);
}

void shell_init(struct shell *sh, void (*write)(void *, const char *), void *ctx)
{
    k_memset(sh, 0, sizeof *sh);
    sh->cwd = vfs_lookup(VFS_ROOT, "/home/user");
    if (sh->cwd < 0)
        sh->cwd = VFS_ROOT;
    sh->write = write;
    sh->ctx = ctx;
}
