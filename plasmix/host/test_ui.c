/* test_ui.c - scripted tests for the whole desktop, run on Linux.
 *
 * This file implements platform.h with a fake keyboard (a queue we fill
 * from the test), a fake clock and a "screen" we can search for text.  The
 * desktop code under test is exactly the code the kernel runs.  Screens are
 * also saved to build/screens/ as plain text and as ANSI colour files
 * (view those with: cat build/screens/desktop.ans).
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#include "../src/desktop.h"
#include "../src/gfx.h"
#include "../src/lib.h"
#include "cp437.h"

/* ---- fake platform ------------------------------------------------------ */

static struct key queue[64];
static int qhead, qtail;
static struct datetime fake_now = { 2012, 8, 1, 10, 30, 0 };
static int reboots, poweroffs, frames;

int plat_key_poll(struct key *k)
{
    if (qhead == qtail)
        return 0;
    *k = queue[qhead];
    qhead = (qhead + 1) % 64;
    return 1;
}

void plat_present(const uint16_t *cells) { (void)cells; frames++; }
void plat_time(struct datetime *t) { *t = fake_now; }
uint32_t plat_mem_kib(void) { return 131072; }
void plat_reboot(void) { reboots++; }
void plat_poweroff(void) { poweroffs++; }

/* ---- helpers -------------------------------------------------------------- */

static int failures, checks;

static void push(int code, int mods)
{
    queue[qtail] = (struct key){ code, mods };
    qtail = (qtail + 1) % 64;
    ws_tick();
}

static void press(int code) { push(code, 0); }
static void alt(int code) { push(code, MOD_ALT); }
static void ctrl(int code) { push(code, MOD_CTRL); }

static void type(const char *s)
{
    for (; *s; s++)
        push(*s, 0);
}

static void cmd(const char *s)
{
    type(s);
    press(K_ENTER);
    ws_tick(); /* let deferred work (e.g. Konsole commands) run */
}

static void advance(int seconds)
{
    for (int i = 0; i < seconds; i++) {
        if (++fake_now.second == 60) {
            fake_now.second = 0;
            if (++fake_now.minute == 60) {
                fake_now.minute = 0;
                fake_now.hour = (fake_now.hour + 1) % 24;
            }
        }
        ws_tick();
    }
}

static int screen_has(const char *s)
{
    char row[SCR_W + 1];
    for (int y = 0; y < SCR_H; y++) {
        gfx_row_text(y, row);
        if (strstr(row, s))
            return 1;
    }
    return 0;
}

static int row_has(int y, const char *s)
{
    char row[SCR_W + 1];
    gfx_row_text(y, row);
    return strstr(row, s) != NULL;
}

static void dump(FILE *f, int color)
{
    for (int y = 0; y < SCR_H; y++) {
        for (int x = 0; x < SCR_W; x++) {
            uint16_t c = g_screen[y * SCR_W + x];
            if (color)
                ansi_attr(f, (uint8_t)(c >> 8));
            cp437_put(f, (uint8_t)c);
        }
        fputs(color ? "\033[0m\n" : "\n", f);
    }
}

static void save(const char *name)
{
    char path[128];
    mkdir("build", 0755);
    mkdir("build/screens", 0755);
    snprintf(path, sizeof path, "build/screens/%s.txt", name);
    FILE *f = fopen(path, "w");
    if (f) {
        dump(f, 0);
        fclose(f);
    }
    snprintf(path, sizeof path, "build/screens/%s.ans", name);
    f = fopen(path, "w");
    if (f) {
        dump(f, 1);
        fclose(f);
    }
}

#define CHECK(cond)                                                              \
    do {                                                                         \
        checks++;                                                                \
        if (!(cond)) {                                                           \
            failures++;                                                          \
            fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);     \
            dump(stderr, 0);                                                     \
        }                                                                        \
    } while (0)

static void boot(void)
{
    qhead = qtail = 0;
    fake_now = (struct datetime){ 2012, 8, 1, 10, 30, 0 };
    ws_init();
    ws_tick();
}

static void login(void)
{
    boot();
    press(K_ENTER);
}

static int file_text(const char *path, char *buf, int size)
{
    int n = vfs_lookup(VFS_ROOT, path);
    if (n < 0)
        return -1;
    int len = vfs_read(n, buf, size - 1);
    buf[len < 0 ? 0 : len] = 0;
    return len;
}

/* ---- unit tests ------------------------------------------------------------ */

static void test_lib(void)
{
    int32_t v;
    CHECK(k_eval("6*7", &v) == 0 && v == 42);
    CHECK(k_eval("(1+2)*-3", &v) == 0 && v == -9);
    CHECK(k_eval("7 % 4 + 10 / 3", &v) == 0 && v == 6);
    CHECK(k_eval("1/0", &v) != 0);
    CHECK(k_eval("2147483647+1", &v) != 0);
    CHECK(k_eval("-2147483647-1", &v) == 0 && v == INT32_MIN);
    CHECK(k_eval("3 +", &v) != 0);
    CHECK(k_eval("abc", &v) != 0);

    char buf[32];
    k_snprintf(buf, sizeof buf, "[%5d|%-4s|%03u|%x]", -42, "ab", 7u, 255u);
    CHECK(strcmp(buf, "[  -42|ab  |007|ff]") == 0);
    k_snprintf(buf, 6, "%s", "truncated");
    CHECK(strcmp(buf, "trunc") == 0);
    CHECK(k_strcasestr("Konsole", "SOL"));
    CHECK(!k_strcasestr("Dolphin", "xyz"));
}

static void test_vfs(void)
{
    vfs_init();
    int home = vfs_lookup(VFS_ROOT, "/home/user");
    CHECK(home > 0 && vfs_is_dir(home));
    CHECK(vfs_lookup(VFS_ROOT, "~") == home);
    CHECK(vfs_lookup(home, "../user/./Desktop/..") == home);
    CHECK(vfs_lookup(VFS_ROOT, "/..") == VFS_ROOT);
    CHECK(vfs_lookup(VFS_ROOT, "/nope") == VFS_ENOENT);
    CHECK(vfs_lookup(VFS_ROOT, "/etc/passwd/x") == VFS_ENOTDIR);

    int d = vfs_create(home, "proj", 1);
    CHECK(d > 0);
    CHECK(vfs_create(home, "proj", 1) == VFS_EEXIST);
    CHECK(vfs_create(home, "missing/file", 0) == VFS_ENOENT);
    CHECK(vfs_create(home, "..", 1) == VFS_EINVAL);
    int f = vfs_create(home, "proj/a.txt", 0);
    CHECK(f > 0);
    CHECK(vfs_write(f, "hello", 5, 0) == 5);
    CHECK(vfs_write(f, " world", 6, 1) == 6);
    char buf[32] = { 0 };
    CHECK(vfs_read(f, buf, sizeof buf) == 11 && memcmp(buf, "hello world", 11) == 0);
    CHECK(vfs_remove(d) == VFS_ENOTEMPTY);
    CHECK(vfs_rename(d, f, "x") == VFS_ENOTDIR);
    int sub = vfs_create(home, "proj/sub", 1);
    CHECK(vfs_rename(d, sub, "loop") == VFS_EINVAL); /* into itself */
    char path[VFS_PATH_MAX];
    vfs_path(f, path, sizeof path);
    CHECK(strcmp(path, "/home/user/proj/a.txt") == 0);
    CHECK(vfs_remove(VFS_ROOT) == VFS_EBUSY);

    static char big[VFS_FILE_MAX + 1];
    CHECK(vfs_write(f, big, VFS_FILE_MAX + 1, 0) == VFS_ENOSPC);

    /* Directory listing: folders first, then by name. */
    int kids[16];
    int n = vfs_list(d, kids, 16);
    CHECK(n == 2 && vfs_is_dir(kids[0]) && strcmp(vfs_name(kids[1]), "a.txt") == 0);

    /* Running out of inodes reports ENOSPC instead of crashing. */
    int err = 0;
    for (int i = 0; i < VFS_MAX_NODES + 5 && err >= 0; i++) {
        char name[16];
        snprintf(name, sizeof name, "f%d", i);
        err = vfs_create(VFS_ROOT, name, 0);
    }
    CHECK(err == VFS_ENOSPC);
}

static char shell_out[8192];
static void shell_sink(void *ctx, const char *s)
{
    (void)ctx;
    strncat(shell_out, s, sizeof shell_out - strlen(shell_out) - 1);
}

static const char *sh_run(struct shell *sh, const char *line)
{
    shell_out[0] = 0;
    shell_exec(sh, line);
    return shell_out;
}

static void test_shell(void)
{
    boot();
    struct shell sh;
    shell_init(&sh, shell_sink, NULL);
    char buf[256];

    CHECK(strcmp(sh_run(&sh, "pwd"), "/home/user\n") == 0);
    CHECK(strstr(sh_run(&sh, "ls /"), "home/"));
    CHECK(strstr(sh_run(&sh, "ls -l /etc"), "-rw-r--r-- root root"));
    CHECK(strstr(sh_run(&sh, "ls -l /"), "drwxrwxrwt"));          /* sticky /tmp */
    CHECK(!strstr(sh_run(&sh, "ls"), ".profile"));
    CHECK(strstr(sh_run(&sh, "ls -a"), ".profile"));
    CHECK(strcmp(sh_run(&sh, "echo \"two  spaces\" 'and quotes'"), "two  spaces and quotes\n") == 0);

    sh_run(&sh, "echo first > /tmp/out.txt");
    sh_run(&sh, "echo second>>/tmp/out.txt");
    file_text("/tmp/out.txt", buf, sizeof buf);
    CHECK(strcmp(buf, "first\nsecond\n") == 0);
    CHECK(strcmp(sh_run(&sh, "cat /tmp/out.txt"), "first\nsecond\n") == 0);
    CHECK(strstr(sh_run(&sh, "wc /tmp/out.txt"), "   2    2    13 /tmp/out.txt"));

    sh_run(&sh, "cd Documents");
    CHECK(strcmp(sh_run(&sh, "pwd"), "/home/user/Documents\n") == 0);
    char prompt[128];
    shell_prompt(&sh, prompt, sizeof prompt);
    CHECK(strcmp(prompt, "user@plasmix:~/Documents$ ") == 0);
    sh_run(&sh, "cd /etc");
    shell_prompt(&sh, prompt, sizeof prompt);
    CHECK(strcmp(prompt, "user@plasmix:/etc$ ") == 0);
    CHECK(strstr(sh_run(&sh, "cd nowhere"), "No such file or directory"));
    CHECK(strstr(sh_run(&sh, "cd passwd"), "Not a directory"));
    sh_run(&sh, "cd");
    CHECK(strcmp(sh_run(&sh, "pwd"), "/home/user\n") == 0);

    sh_run(&sh, "mkdir work");
    sh_run(&sh, "touch work/a");
    sh_run(&sh, "cp /etc/hostname work");
    file_text("/home/user/work/hostname", buf, sizeof buf);
    CHECK(strcmp(buf, "plasmix\n") == 0);
    sh_run(&sh, "mv work/a work/b");
    CHECK(vfs_lookup(VFS_ROOT, "/home/user/work/b") > 0);
    CHECK(vfs_lookup(VFS_ROOT, "/home/user/work/a") < 0);
    CHECK(strstr(sh_run(&sh, "rmdir work"), "Directory not empty"));
    CHECK(strstr(sh_run(&sh, "rm work"), "Is a directory"));
    sh_run(&sh, "rm -r work");
    CHECK(vfs_lookup(VFS_ROOT, "/home/user/work") < 0);

    CHECK(strcmp(sh_run(&sh, "calc (2+3)*4"), "20\n") == 0);
    CHECK(strstr(sh_run(&sh, "calc 1/0"), "invalid"));
    CHECK(strstr(sh_run(&sh, "uname -a"), "x86_64"));
    CHECK(strcmp(sh_run(&sh, "date"), "Wed Aug  1 10:30:00 2012\n") == 0);
    CHECK(strstr(sh_run(&sh, "frobnicate"), "frobnicate: command not found"));
    CHECK(strstr(sh_run(&sh, "echo >"), "syntax error"));
    CHECK(strstr(sh_run(&sh, "help"), "kwrite"));
    CHECK(strstr(sh_run(&sh, "kill 1"), "refusing"));
    CHECK(strstr(sh_run(&sh, "kill 9999"), "No such process"));
    CHECK(strstr(sh_run(&sh, "history"), "kill 9999"));
    /* Every built-in has a file in /bin. */
    CHECK(vfs_lookup(VFS_ROOT, "/bin/systemsettings") > 0);
}

/* ---- desktop tests ------------------------------------------------------- */

static void test_login_and_desktop(void)
{
    boot();
    CHECK(screen_has("Welcome to Plasmix"));
    type("secret");
    CHECK(screen_has("******"));
    save("login");
    press(K_ENTER);
    CHECK(g_ws.phase == PHASE_DESKTOP);
    CHECK(row_has(PANEL_Y, " K "));
    CHECK(row_has(PANEL_Y, "10:30"));
    CHECK(screen_has("Folder View"));
    CHECK(screen_has("README.txt"));
    CHECK(screen_has("Notes"));
    CHECK(screen_has("1 August 2012"));
    CHECK(screen_has("Welcome")); /* notification */
    save("desktop");

    /* Notifications disappear after a few seconds; the clock keeps time. */
    advance(6);
    CHECK(!screen_has("Alt+F1 opens the launcher"));
    CHECK(g_ws.uptime == 6);
    advance(60);
    CHECK(row_has(PANEL_Y, "10:31"));
}

static void test_kickoff_konsole(void)
{
    login();
    alt(K_F1);
    CHECK(screen_has("User user on plasmix"));
    CHECK(screen_has("Favorites"));
    CHECK(screen_has("Konsole"));
    save("kickoff");
    press(K_RIGHT); /* Applications tab */
    CHECK(screen_has("System Activity"));
    press(K_RIGHT);
    press(K_RIGHT);
    press(K_RIGHT); /* Leave tab */
    CHECK(screen_has("Shut down"));
    type("kon"); /* search */
    CHECK(screen_has("Search results"));
    press(K_ENTER);
    CHECK(wm_focused() && wm_focused()->app == APP_KONSOLE);
    CHECK(screen_has("Welcome to Plasmix, a toy UNIX-like"));
    CHECK(screen_has("user@plasmix:~$"));

    cmd("ls /");
    CHECK(screen_has("bin/  etc/  home/"));
    cmd("cd Documents");
    CHECK(screen_has("user@plasmix:~/Documents$"));
    CHECK(strstr(wm_focused()->title, "Documents - Konsole") != NULL);
    cmd("calc 6*7");
    CHECK(screen_has("42"));
    save("konsole");

    /* History: Up brings back the previous command. */
    press(K_UP);
    CHECK(strcmp(wm_focused()->s.kon.input, "calc 6*7") == 0);
    press(K_DOWN);
    CHECK(wm_focused()->s.kon.len == 0);

    /* Lots of output scrolls; Shift+PgUp shows older lines. */
    for (int i = 0; i < 30; i++)
        cmd("echo line");
    push(K_PGUP, MOD_SHIFT);
    CHECK(screen_has("[scrollback]"));
    press('x');
    CHECK(!screen_has("[scrollback]"));
    ctrl('c');
    cmd("clear");
    CHECK(!screen_has("echo line"));

    /* Recently used apps show up in Kickoff's Recent tab. */
    alt(K_F1);
    press(K_RIGHT);
    press(K_RIGHT);
    press(K_RIGHT);
    CHECK(screen_has("Terminal"));
    press(K_ESC);

    cmd("exit");
    CHECK(wm_focused() == NULL);
}

static void test_krunner(void)
{
    login();
    alt(K_F2);
    CHECK(screen_has("KRunner"));
    type("6*7");
    CHECK(screen_has("= 42"));
    save("krunner");
    press(K_ENTER); /* puts the result back in the box */
    CHECK(screen_has("42_"));
    press(K_ESC);
    CHECK(!screen_has(" KRunner ")); /* the popup title */

    alt(K_F2);
    type("/etc");
    CHECK(screen_has("Open /etc"));
    press(K_ENTER);
    CHECK(wm_focused() && wm_focused()->app == APP_DOLPHIN);
    CHECK(screen_has("passwd"));
    CHECK(screen_has("Places"));
    save("dolphin");

    /* Unknown text becomes a command run in Konsole. */
    alt(K_F2);
    type("echo from-krunner");
    CHECK(screen_has("Run \"echo from-krunner\""));
    press(K_UP);   /* moving past either end of the list is harmless */
    press(K_DOWN);
    press(K_DOWN);
    press(K_ENTER);
    ws_tick();
    CHECK(wm_focused() && wm_focused()->app == APP_KONSOLE);
    CHECK(screen_has("from-krunner"));
}

static void test_windows_and_desktops(void)
{
    login();
    alt(K_F1);
    press(K_ENTER); /* Konsole */
    alt(K_F1);
    press(K_DOWN);
    press(K_ENTER); /* Dolphin */
    struct window *vis[MAX_WIN];
    CHECK(wm_list_visible(vis, MAX_WIN) == 2);
    CHECK(wm_focused()->app == APP_DOLPHIN);
    alt(K_TAB);
    CHECK(wm_focused()->app == APP_KONSOLE);
    alt(K_TAB);
    CHECK(wm_focused()->app == APP_DOLPHIN);
    CHECK(row_has(PANEL_Y, "Konsole") || row_has(PANEL_Y, "Konsol"));

    /* Alt+F7 moves the window with the arrow keys. */
    int x0 = wm_focused()->x, y0 = wm_focused()->y;
    alt(K_F7);
    CHECK(screen_has("(moving)"));
    press(K_RIGHT);
    press(K_DOWN);
    press(K_ENTER);
    CHECK(wm_focused()->x == x0 + 2 && wm_focused()->y == y0 + 1);
    CHECK(!g_ws.move_mode);

    /* Virtual desktops: desktop 2 starts empty. */
    ctrl(K_F2);
    CHECK(g_ws.desk == 1);
    CHECK(wm_focused() == NULL);
    CHECK(wm_list_visible(vis, MAX_WIN) == 0);
    ctrl(K_F1);
    CHECK(wm_list_visible(vis, MAX_WIN) == 2);

    /* Dashboard hides windows and shows the widgets. */
    ctrl(K_F12);
    CHECK(wm_focused() == NULL);
    CHECK(screen_has("Folder View"));
    ctrl(K_F12);
    CHECK(wm_focused() != NULL);

    /* Ctrl+Esc: System Activity lists the processes; Delete ends one. */
    ctrl(K_ESC);
    CHECK(wm_focused()->app == APP_SYSMON);
    ws_tick();
    CHECK(screen_has("konsole"));
    save("system-activity");
    press(K_DELETE); /* first row = the oldest window, the Konsole */
    ws_draw();
    CHECK(wm_list_visible(vis, MAX_WIN) == 2);

    alt(K_F4);
    alt(K_F4);
    CHECK(wm_list_visible(vis, MAX_WIN) == 0);
}

static void test_kwrite(void)
{
    login();
    svc_launch(APP_KONSOLE, 0);
    ws_tick();
    cmd("kwrite note.txt");
    CHECK(wm_focused()->app == APP_KWRITE);
    CHECK(strstr(wm_focused()->title, "note.txt - KWrite") != NULL);
    type("hello");
    press(K_ENTER);
    type("world");
    CHECK(strstr(wm_focused()->title, "[modified]") != NULL);
    press(K_UP);
    press(K_END);
    type("!");
    ctrl('s');
    CHECK(screen_has("Saved /home/user/note.txt"));
    char buf[64];
    file_text("/home/user/note.txt", buf, sizeof buf);
    CHECK(strcmp(buf, "hello!\nworld") == 0);
    CHECK(strstr(wm_focused()->title, "[modified]") == NULL);
    save("kwrite");
}

static void test_toolbox_activities_widgets(void)
{
    login();
    alt(K_F12);
    CHECK(screen_has("Desktop Toolbox"));
    save("toolbox");
    press(K_ENTER); /* Add Widgets... */
    CHECK(screen_has("Fifteen Puzzle"));
    press(K_DOWN); /* Notes */
    press(K_ENTER);
    struct activity *a = &g_ws.activities[0];
    int count = 0;
    for (int i = 0; i < MAX_WIDGETS; i++)
        count += a->widgets[i].used;
    CHECK(count == 4);
    CHECK(screen_has("Added Notes"));

    /* The new widget is focused: type into it, then move it with Alt+arrows. */
    struct plasmoid *p = &a->widgets[a->focus];
    CHECK(p->type == PL_NOTES);
    type("!");
    CHECK(p->text[strlen(p->text) - 1] == '!');
    int x0 = p->x;
    alt(K_LEFT);
    CHECK(p->x == x0 - 2);

    /* Remove it again from the toolbox. */
    alt(K_F12);
    press(K_DOWN);
    CHECK(screen_has("Remove Notes"));
    press(K_ENTER);
    count = 0;
    for (int i = 0; i < MAX_WIDGETS; i++)
        count += a->widgets[i].used;
    CHECK(count == 3);

    /* Lock widgets: moving is refused. */
    alt(K_F12);
    press(K_DOWN);
    press(K_DOWN);
    CHECK(screen_has("Lock Widgets"));
    press(K_ENTER);
    CHECK(g_ws.locked);
    press(K_TAB);
    alt(K_RIGHT);
    CHECK(screen_has("Widgets are locked"));

    /* Switch to the "Play" activity: other widgets, the fifteen puzzle. */
    alt(K_F12);
    press(K_DOWN);
    press(K_ENTER); /* Activities... */
    CHECK(screen_has("Play"));
    press(K_DOWN);
    press(K_ENTER);
    CHECK(g_ws.activity == 1);
    CHECK(screen_has("Fifteen Puzzle"));
    CHECK(!screen_has("Folder View"));
    press(K_TAB);
    struct plasmoid *fp = &g_ws.activities[1].widgets[g_ws.activities[1].focus];
    CHECK(fp->type == PL_FIFTEEN);
    int before[16];
    memcpy(before, fp->tiles, sizeof before);
    int moved = 0;
    int keys[4] = { K_LEFT, K_RIGHT, K_UP, K_DOWN };
    for (int i = 0; i < 4 && !moved; i++) {
        press(keys[i]);
        moved = memcmp(before, fp->tiles, sizeof before) != 0;
    }
    CHECK(moved);
    save("activity-play");

    /* Windows belong to an activity too. */
    svc_launch(APP_DOLPHIN, 0);
    ws_tick();
    g_ws.activity = 0;
    struct window *vis[MAX_WIN];
    CHECK(wm_list_visible(vis, MAX_WIN) == 0);
    g_ws.activity = 1;
    CHECK(wm_list_visible(vis, MAX_WIN) == 1);

    /* New and removed activities. */
    alt(K_F12);
    press(K_DOWN);
    press(K_ENTER);
    press(K_DOWN);
    press(K_DOWN);
    press(K_ENTER); /* + New Activity */
    CHECK(g_ws.nactivities == 3 && g_ws.activity == 2);
    alt(K_F12);
    press(K_DOWN);
    press(K_ENTER);
    press(K_DOWN);
    press(K_DOWN);
    press(K_DOWN);
    press(K_DOWN);
    CHECK(screen_has("Remove This Activity"));
    press(K_ENTER);
    CHECK(g_ws.nactivities == 2 && g_ws.activity == 0);
}

static void test_settings_theme(void)
{
    login();
    svc_launch(APP_SETTINGS, 0);
    ws_tick();
    CHECK(screen_has("Desktop theme"));
    press(K_ENTER);
    CHECK(strcmp(g_theme->name, "Oxygen") == 0);
    press(K_DOWN);
    press(K_RIGHT); /* wallpaper */
    press(K_DOWN);
    press(K_ENTER); /* 12-hour clock */
    CHECK(row_has(PANEL_Y, "10:30 AM"));
    save("settings-oxygen");
    alt(K_F4);
    save("desktop-oxygen");
    g_theme = &g_themes[0];
}

static void test_leave(void)
{
    login();
    svc_launch(APP_KONSOLE, 0);
    ws_tick();
    alt(K_F1);
    press(K_LEFT); /* Favorites -> Leave (wraps round) */
    CHECK(screen_has("Log out"));
    press(K_ENTER);
    CHECK(g_ws.phase == PHASE_LOGIN);
    CHECK(wm_focused() == NULL);

    login();
    svc_launch(APP_KONSOLE, 0);
    ws_tick();
    cmd("reboot");
    CHECK(reboots == 1);
    CHECK(screen_has("Restarting"));

    login();
    alt(K_F1);
    type("shut");
    press(K_ENTER);
    CHECK(poweroffs == 1);
    CHECK(screen_has("safe to turn off"));
    save("shutdown");
    press('x'); /* keys are ignored once shut down */
    CHECK(g_ws.phase == PHASE_OFF);
}

/* Random key presses for a while: looks for crashes and memory errors
 * (this build uses AddressSanitizer). */
static void test_fuzz(void)
{
    static const int specials[] = {
        K_ENTER, K_ESC, K_TAB, K_BACKSPACE, K_UP, K_DOWN, K_LEFT, K_RIGHT, K_HOME,
        K_END, K_PGUP, K_PGDN, K_DELETE, K_F1, K_F2, K_F4, K_F7, K_F12,
    };
    k_srand(12345);
    login();
    for (int i = 0; i < 40000; i++) {
        if (g_ws.phase != PHASE_DESKTOP)
            login();
        uint32_t r = k_rand();
        int code = (r & 3) ? (int)(32 + (r >> 8) % 95)
                           : specials[(r >> 8) % (sizeof specials / sizeof specials[0])];
        int mods = (r >> 20) & 7;
        if (mods & MOD_ALT && code == K_F4 && (r >> 24) % 4)
            mods &= ~MOD_ALT; /* keep some windows alive */
        push(code, mods);
        if (i % 97 == 0)
            advance(1);
    }
    CHECK(1);
}

int main(void)
{
    test_lib();
    test_vfs();
    test_shell();
    test_login_and_desktop();
    test_kickoff_konsole();
    test_krunner();
    test_windows_and_desktops();
    test_kwrite();
    test_toolbox_activities_widgets();
    test_settings_theme();
    test_leave();
    test_fuzz();
    printf("%d checks, %d failures, %d frames drawn\n", checks, failures, frames);
    return failures ? 1 : 0;
}
