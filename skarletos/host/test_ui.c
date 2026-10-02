/* test_ui.c - scripted tests for the whole desktop, run on Linux.
 *
 * This file implements platform.h with a fake keyboard (a queue we fill
 * from the test), a fake clock and a 1918 x 1075 screen.  "What text is on
 * the screen" comes from the drawing library's text log.  The desktop code
 * under test is exactly the code the kernel runs.  Screens are also saved to
 * build/screens/ as PPM images (convert them with any image tool).
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#include "../src/desktop.h"
#include "../src/gfx.h"
#include "../src/lib.h"

#define TEST_W 1918
#define TEST_H 1075

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

void plat_display_size(int *w, int *h)
{
    *w = TEST_W;
    *h = TEST_H;
}
void plat_present(const uint32_t *px, int w, int h)
{
    (void)px;
    (void)w;
    (void)h;
    frames++;
}
void plat_time(struct datetime *t) { *t = fake_now; }
uint32_t plat_mem_kib(void) { return 131072; }
void plat_reboot(void) { reboots++; }
void plat_poweroff(void) { poweroffs++; }

/* External windows (another program's) ask the platform to close them. */
static int close_requests;
static long last_close;
void plat_window_close(long ext)
{
    close_requests++;
    last_close = ext;
}

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
    ws_tick(); /* let deferred work (e.g. Skarlet Terminal commands) run */
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

static int screen_has(const char *s) { return gfx_text_visible(s); }

/* ---- the mouse -------------------------------------------------------------- */

static void mouse(int type, int x, int y, int button, int clicks)
{
    ws_mouse((struct mouse){ type, x, y, button, clicks });
    ws_tick();
}

static void click(int x, int y)
{
    mouse(MOUSE_DOWN, x, y, 1, 1);
    mouse(MOUSE_UP, x, y, 1, 1);
}

static void double_click(int x, int y)
{
    click(x, y);
    mouse(MOUSE_DOWN, x, y, 1, 2);
    mouse(MOUSE_UP, x, y, 1, 2);
}

static void drag_by(int x, int y, int dx, int dy)
{
    mouse(MOUSE_DOWN, x, y, 1, 1);
    mouse(MOUSE_MOVE, x + dx / 2, y + dy / 2, 1, 0);
    mouse(MOUSE_MOVE, x + dx, y + dy, 1, 0);
    mouse(MOUSE_UP, x + dx, y + dy, 1, 0);
}

/* Find a point inside the clickable region (kind, arg) of the current
 * frame, near its middle, by asking what is under a grid of points. */
static int find_hit(int kind, int arg, int *px, int *py)
{
    int x0 = 1 << 30, y0 = 1 << 30, x1 = -1, y1 = -1, fx = -1, fy = -1;
    for (int y = 0; y < g_h; y += 3)
        for (int x = 0; x < g_w; x += 3) {
            int a = -1;
            if (ws_hit_at(x, y, &a) == kind && a == arg) {
                if (fx < 0)
                    fx = x, fy = y;
                x0 = x < x0 ? x : x0, x1 = x > x1 ? x : x1;
                y0 = y < y0 ? y : y0, y1 = y > y1 ? y : y1;
            }
        }
    if (fx < 0)
        return 0;
    int cx = (x0 + x1) / 2, cy = (y0 + y1) / 2, a = -1;
    if (ws_hit_at(cx, cy, &a) == kind && a == arg)
        *px = cx, *py = cy;
    else
        *px = fx, *py = fy;
    return 1;
}

/* Click the middle of a clickable region; fails the test if there is none. */
static int click_hit(int kind, int arg)
{
    int x, y;
    if (!find_hit(kind, arg, &x, &y))
        return 0;
    click(x, y);
    return 1;
}

/* Text drawn on the panel (the bottom of the screen). */
static int panel_has(const char *s) { return gfx_text_visible_in(PANEL_Y, g_h, s); }

static void dump(FILE *f) { fputs(gfx_textlog(), f); }

/* Save the current frame as a PPM image (a simple uncompressed format). */
static void save(const char *name)
{
    char path[128];
    mkdir("build", 0755);
    mkdir("build/screens", 0755);
    snprintf(path, sizeof path, "build/screens/%s.ppm", name);
    FILE *f = fopen(path, "wb");
    if (!f)
        return;
    fprintf(f, "P6\n%d %d\n255\n", g_w, g_h);
    for (int i = 0; i < g_w * g_h; i++) {
        unsigned char rgb[3] = { (unsigned char)(g_px[i] >> 16), (unsigned char)(g_px[i] >> 8),
                                 (unsigned char)g_px[i] };
        fwrite(rgb, 1, 3, f);
    }
    fclose(f);
}

/* Is the colour c close to want (each channel within tol)? */
static int near(uint32_t c, uint32_t want, int tol)
{
    for (int sh = 0; sh <= 16; sh += 8) {
        int d = (int)((c >> sh) & 255) - (int)((want >> sh) & 255);
        if (d < -tol || d > tol)
            return 0;
    }
    return 1;
}

#define CHECK(cond)                                                              \
    do {                                                                         \
        checks++;                                                                \
        if (!(cond)) {                                                           \
            failures++;                                                          \
            fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);     \
            dump(stderr);                                                        \
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
    CHECK(k_strcasestr("Skarlet Terminal", "TERM"));
    CHECK(!k_strcasestr("Skarlet Files", "xyz"));
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
    CHECK(strcmp(prompt, "user@skarlet:~/Documents$ ") == 0);
    sh_run(&sh, "cd /etc");
    shell_prompt(&sh, prompt, sizeof prompt);
    CHECK(strcmp(prompt, "user@skarlet:/etc$ ") == 0);
    CHECK(strstr(sh_run(&sh, "cd nowhere"), "No such file or directory"));
    CHECK(strstr(sh_run(&sh, "cd passwd"), "Not a directory"));
    sh_run(&sh, "cd");
    CHECK(strcmp(sh_run(&sh, "pwd"), "/home/user\n") == 0);

    sh_run(&sh, "mkdir work");
    sh_run(&sh, "touch work/a");
    sh_run(&sh, "cp /etc/hostname work");
    file_text("/home/user/work/hostname", buf, sizeof buf);
    CHECK(strcmp(buf, "skarlet\n") == 0);
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
    CHECK(strstr(sh_run(&sh, "help"), "skwrite"));
    CHECK(strstr(sh_run(&sh, "kill 1"), "refusing"));
    CHECK(strstr(sh_run(&sh, "kill 9999"), "No such process"));
    CHECK(strstr(sh_run(&sh, "history"), "kill 9999"));
    /* Every built-in has a file in /bin. */
    CHECK(vfs_lookup(VFS_ROOT, "/bin/sksettings") > 0);
}

/* ---- desktop tests ------------------------------------------------------- */

static void test_login_and_desktop(void)
{
    boot();
    CHECK(screen_has("Welcome to SkarletOS"));
    CHECK(screen_has("Password"));
    type("secret");
    CHECK(!screen_has("Password")); /* the placeholder gives way to dots */
    save("login");
    press(K_ENTER);
    CHECK(g_ws.phase == PHASE_DESKTOP);
    /* The screen is the requested 1918 x 1075. */
    CHECK(g_w == 1918 && g_h == 1075);
    /* The default look: Skarlet Dark with a maroon accent. */
    CHECK(strcmp(g_theme->name, "Skarlet Dark") == 0);
    CHECK(strcmp(g_accents[g_accent_index].name, "Maroon") == 0);
    CHECK(g_theme->accent == MAROON && MAROON == 0x800000);
    /* The launcher button on the panel is a maroon circle. */
    CHECK(gfx_get(PANEL_MARGIN + 8 + 20, PANEL_Y + PANEL_H / 2 + 12) == MAROON);
    /* The panel floats: wallpaper shows below it and at its sides. */
    CHECK(PANEL_Y + PANEL_H < g_h);
    CHECK(panel_has("10:30") && panel_has("Wed 1 Aug"));
    CHECK(screen_has("Folder View"));
    CHECK(screen_has("README.txt"));
    CHECK(screen_has("Welcome! Type here.")); /* the Notes widget */
    CHECK(screen_has("1 August 2012"));
    CHECK(screen_has("Welcome")); /* notification */
    save("desktop");

    /* Notifications disappear after a few seconds; the clock keeps time. */
    advance(6);
    CHECK(!screen_has("Alt+F1 opens the launcher"));
    CHECK(g_ws.uptime == 6);
    advance(60);
    CHECK(panel_has("10:31"));
}

static void test_launcher_terminal(void)
{
    login();
    alt(K_F1);
    CHECK(screen_has("user") && screen_has("on skarlet"));
    CHECK(screen_has("Search:"));
    CHECK(screen_has("Favorites") && screen_has("Recently Used") && screen_has("Power"));
    CHECK(screen_has("Skarlet Terminal") && screen_has("Terminal"));
    save("launcher");
    press(K_RIGHT); /* Applications tab: categories first */
    CHECK(screen_has("System") && screen_has("3 applications"));
    press(K_ENTER); /* open the System category */
    CHECK(screen_has("All Applications") && screen_has("Skarlet Monitor"));
    press(K_BACKSPACE); /* back to the categories */
    CHECK(!screen_has("All Applications"));
    press(K_RIGHT); /* Computer */
    CHECK(screen_has("Places") && screen_has("/home/user/Documents"));
    press(K_RIGHT);
    press(K_RIGHT); /* Leave tab, in sections */
    CHECK(screen_has("Session") && screen_has("Log out") && screen_has("Shut down"));
    type("term"); /* search */
    CHECK(screen_has("Applications") && !screen_has("Shut down"));
    press(K_ENTER);
    CHECK(wm_focused() && wm_focused()->app == APP_TERMINAL);
    CHECK(screen_has("Welcome to SkarletOS, a toy UNIX-like"));
    CHECK(screen_has("user@skarlet:~$"));

    cmd("ls /");
    CHECK(screen_has("bin/  etc/  home/"));
    cmd("cd Documents");
    CHECK(screen_has("user@skarlet:~/Documents$"));
    CHECK(strstr(wm_focused()->title, "Documents - Skarlet Terminal") != NULL);
    cmd("calc 6*7");
    CHECK(screen_has("42"));
    save("skterm");

    /* History: Up brings back the previous command. */
    press(K_UP);
    CHECK(strcmp(wm_focused()->s.term.input, "calc 6*7") == 0);
    press(K_DOWN);
    CHECK(wm_focused()->s.term.len == 0);

    /* Lots of output scrolls; Shift+PgUp shows older lines. */
    for (int i = 0; i < 30; i++)
        cmd("echo line");
    push(K_PGUP, MOD_SHIFT);
    CHECK(screen_has("scrollback"));
    press('x');
    CHECK(!screen_has("scrollback"));
    ctrl('c');
    cmd("clear");
    CHECK(!screen_has("echo line"));

    /* Recently used apps show up in Skarlet Launcher's Recent tab. */
    alt(K_F1);
    press(K_RIGHT);
    press(K_RIGHT);
    press(K_RIGHT);
    CHECK(screen_has("Terminal"));
    press(K_ESC);

    cmd("exit");
    CHECK(wm_focused() == NULL);
}

static void test_runner(void)
{
    login();
    alt(K_F2);
    CHECK(screen_has("Apps, places like /etc")); /* the runner's hint */
    type("6*7");
    CHECK(screen_has("= 42"));
    save("runner");
    press(K_ENTER); /* puts the result back in the box */
    CHECK(screen_has("Run \"42\""));
    press(K_ESC);
    CHECK(!screen_has("Run \"42\""));

    alt(K_F2);
    type("/etc");
    CHECK(screen_has("Open /etc"));
    press(K_ENTER);
    CHECK(wm_focused() && wm_focused()->app == APP_FILES);
    CHECK(screen_has("passwd"));
    CHECK(screen_has("PLACES") && screen_has("Documents"));
    save("skfiles");

    /* Unknown text becomes a command run in Skarlet Terminal. */
    alt(K_F2);
    type("echo from-runner");
    CHECK(screen_has("Run \"echo from-runner\""));
    press(K_UP);   /* moving past either end of the list is harmless */
    press(K_DOWN);
    press(K_DOWN);
    press(K_ENTER);
    ws_tick();
    CHECK(wm_focused() && wm_focused()->app == APP_TERMINAL);
    CHECK(screen_has("from-runner"));
}

static void test_windows_and_desktops(void)
{
    login();
    alt(K_F1);
    press(K_ENTER); /* Skarlet Terminal */
    alt(K_F1);
    press(K_DOWN);
    press(K_ENTER); /* Skarlet Files */
    struct window *vis[MAX_WIN];
    CHECK(wm_list_visible(vis, MAX_WIN) == 2);
    CHECK(wm_focused()->app == APP_FILES);
    alt(K_TAB);
    CHECK(wm_focused()->app == APP_TERMINAL);
    alt(K_TAB);
    CHECK(wm_focused()->app == APP_FILES);

    /* Alt+F7 moves the window with the arrow keys. */
    int x0 = wm_focused()->x, y0 = wm_focused()->y;
    alt(K_F7);
    CHECK(screen_has("(moving)"));
    press(K_RIGHT);
    press(K_DOWN);
    press(K_ENTER);
    CHECK(wm_focused()->x == x0 + 24 && wm_focused()->y == y0 + 24);
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

    /* Ctrl+Esc: Skarlet Monitor lists the processes; Delete ends one. */
    ctrl(K_ESC);
    CHECK(wm_focused()->app == APP_MONITOR);
    ws_tick();
    CHECK(screen_has("skterm"));
    save("system-activity");
    press(K_DELETE); /* first row = the oldest window, the Skarlet Terminal */
    ws_draw();
    CHECK(wm_list_visible(vis, MAX_WIN) == 2);

    alt(K_F4);
    alt(K_F4);
    CHECK(wm_list_visible(vis, MAX_WIN) == 0);
}

static void test_write(void)
{
    login();
    svc_launch(APP_TERMINAL, 0);
    ws_tick();
    cmd("skwrite note.txt");
    CHECK(wm_focused()->app == APP_WRITE);
    CHECK(strstr(wm_focused()->title, "note.txt - Skarlet Write") != NULL);
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
    save("skwrite");
}

static void test_toolbox_activities_widgets(void)
{
    login();
    alt(K_F12);
    CHECK(screen_has("Add Widgets...") && screen_has("Desktop Settings"));
    save("toolbox");
    press(K_ENTER); /* Add Widgets...: a strip of tiles above the panel */
    CHECK(screen_has("Add Widgets") && screen_has("Fifteen") && screen_has("Puzzle"));
    CHECK(screen_has("Shows the files on your desktop")); /* the selected tile */
    type("clock"); /* the search narrows the tiles */
    CHECK(screen_has("Big clock with the date") && !screen_has("Fifteen"));
    press(K_BACKSPACE);
    press(K_BACKSPACE);
    press(K_BACKSPACE);
    press(K_BACKSPACE);
    press(K_BACKSPACE);
    press(K_RIGHT); /* Notes */
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
    CHECK(p->x == x0 - 16);

    /* Unlocked and focused: the applet handle shows beside it. */
    CHECK(near(gfx_get(p->x + p->w + 10 + 20, p->y + 40), g_theme->card, 40));

    /* Remove it again from the toolbox. */
    alt(K_F12);
    press(K_DOWN);
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
    press(K_ENTER); /* Activities...: a strip like Add Widgets */
    CHECK(screen_has("Activities") && screen_has("Play") && screen_has("New Activity"));
    CHECK(screen_has("The current activity"));
    press(K_RIGHT);
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
    svc_launch(APP_FILES, 0);
    ws_tick();
    g_ws.activity = 0;
    struct window *vis[MAX_WIN];
    CHECK(wm_list_visible(vis, MAX_WIN) == 0);
    g_ws.activity = 1;
    CHECK(wm_list_visible(vis, MAX_WIN) == 1);

    /* New and removed activities. */
    alt(K_F12);
    press(K_DOWN);
    press(K_ENTER); /* the strip opens on the current activity, "Play" */
    press(K_RIGHT);
    press(K_ENTER); /* New Activity */
    CHECK(g_ws.nactivities == 3 && g_ws.activity == 2);
    alt(K_F12);
    press(K_DOWN);
    press(K_ENTER);
    press(K_DELETE); /* remove the selected (current) activity */
    CHECK(g_ws.nactivities == 2 && g_ws.activity == 0);
    press(K_LEFT);
    press(K_DELETE); /* and another, leaving one */
    press(K_DELETE); /* the last one cannot be removed */
    CHECK(g_ws.nactivities == 1 && screen_has("cannot be removed"));
    press(K_ESC);

    /* Delete removes a focused widget (the applet handle's close button). */
    wm_close_all(); /* so keys go to the desktop, not a window */
    g_ws.locked = 0;
    a = &g_ws.activities[0];
    a->focus = -1;
    press(K_TAB);
    int before_count = 0, after_count = 0;
    for (int i = 0; i < MAX_WIDGETS; i++)
        before_count += a->widgets[i].used;
    press(K_DELETE);
    for (int i = 0; i < MAX_WIDGETS; i++)
        after_count += a->widgets[i].used;
    CHECK(after_count == before_count - 1 && screen_has("Removed"));
}

static void test_settings_theme(void)
{
    login();
    svc_launch(APP_SETTINGS, 0);
    ws_tick();
    CHECK(screen_has("Theme") && screen_has("Skarlet Dark"));
    CHECK(screen_has("Accent colour") && screen_has("Maroon"));
    press(K_ENTER);
    CHECK(strcmp(g_theme->name, "Skarlet Light") == 0);
    CHECK(g_theme->accent == MAROON); /* the accent survives */
    CHECK(!g_theme->dark);
    press(K_DOWN);
    press(K_RIGHT); /* accent: Maroon -> Blue */
    CHECK(strcmp(g_accents[g_accent_index].name, "Blue") == 0);
    CHECK(g_theme->accent != MAROON);
    press(K_LEFT);  /* and back to Maroon */
    CHECK(g_theme->accent == MAROON);
    press(K_DOWN);
    press(K_RIGHT); /* wallpaper */
    press(K_DOWN);
    press(K_ENTER); /* 12-hour clock */
    CHECK(panel_has("10:30 AM"));
    save("settings-light");
    alt(K_F4);
    save("desktop-light");
    /* Logging in again after a reboot gives the defaults back. */
    boot();
    CHECK(strcmp(g_theme->name, "Skarlet Dark") == 0 && g_accent_index == 0);
}

static void test_leave(void)
{
    login();
    svc_launch(APP_TERMINAL, 0);
    ws_tick();
    alt(K_F1);
    press(K_LEFT); /* Favorites -> Leave (wraps round) */
    CHECK(screen_has("Log out"));
    press(K_ENTER);
    CHECK(g_ws.phase == PHASE_LOGIN);
    CHECK(wm_focused() == NULL);

    login();
    svc_launch(APP_TERMINAL, 0);
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

static void test_mouse(void)
{
    boot();
    CHECK(click_hit(HIT_LOGIN, 0));
    CHECK(g_ws.phase == PHASE_DESKTOP);

    /* The panel's launcher button opens the launcher; its tabs and entries click. */
    CHECK(click_hit(HIT_PANEL_LAUNCHER, 0));
    CHECK(ws_popup_open() && screen_has("Favorites"));
    CHECK(click_hit(HIT_TAB, 4));
    CHECK(screen_has("Shut down") && screen_has("Restart"));
    CHECK(click_hit(HIT_TAB, 0));
    CHECK(click_hit(HIT_ITEM, 0)); /* Skarlet Terminal */
    struct window *w = wm_focused();
    CHECK(w && w->app == APP_TERMINAL && !ws_popup_open());
    if (!w)
        return;
    int pid = w->pid;

    /* Drag the title bar to move the window. */
    int x0 = w->x, y0 = w->y;
    drag_by(w->x + 100, w->y + 12, 200, 60);
    CHECK(w->x == x0 + 200 && w->y == y0 + 60);

    /* Maximize and restore with the button, then with a double click. */
    int ww = w->w, wh = w->h;
    CHECK(click_hit(HIT_WIN_MAX, pid));
    CHECK(w->maximized && w->x == 0 && w->y == 0 && w->w == g_w && w->h == DESK_BOTTOM);
    CHECK(click_hit(HIT_WIN_MAX, pid));
    CHECK(!w->maximized && w->w == ww && w->h == wh && w->x == x0 + 200);
    double_click(w->x + 100, w->y + 12);
    CHECK(w->maximized);
    double_click(w->x + 100, w->y + 12);
    CHECK(!w->maximized);

    /* Resize from the bottom-right corner. */
    drag_by(w->x + w->w - 4, w->y + w->h - 4, 100, 50);
    CHECK(w->w == ww + 100 && w->h == wh + 50);

    /* Minimize; the task button stays and brings it back; clicking the
     * button of the focused window minimizes it again. */
    CHECK(click_hit(HIT_WIN_MIN, pid));
    CHECK(w->minimized && wm_focused() == NULL);
    CHECK(click_hit(HIT_PANEL_TASK, pid));
    CHECK(!w->minimized && wm_focused() == w);
    CHECK(click_hit(HIT_PANEL_TASK, pid));
    CHECK(w->minimized);
    CHECK(click_hit(HIT_PANEL_TASK, pid));

    /* The wheel scrolls the terminal back. */
    for (int i = 0; i < 20; i++)
        cmd("echo scroll");
    int cx, cy, cw, ch;
    wm_client_rect(w, &cx, &cy, &cw, &ch);
    mouse(MOUSE_WHEEL, cx + 50, cy + 50, 4, 1);
    CHECK(w->s.term.scroll == 3 && screen_has("scrollback"));
    mouse(MOUSE_WHEEL, cx + 50, cy + 50, 5, 1);
    CHECK(w->s.term.scroll == 0);

    /* The close button. */
    CHECK(click_hit(HIT_WIN_CLOSE, pid));
    CHECK(wm_by_pid(pid) == NULL);

    /* Another program's window: SkarletOS frames it and lists it, and
     * closing asks the program instead of removing the window. */
    struct window *e = wm_open_external(4242, "Firefox ESR", 800, 600);
    ws_tick();
    CHECK(e && e->app == APP_EXTERNAL && e->w == 802 && e->h == 600 + TITLE_H + 2);
    CHECK(screen_has("Firefox ESR") && wm_by_ext(4242) == e && wm_focused() == e);
    if (!e)
        return;
    int epid = e->pid;
    int ex, ey, ecw, ech;
    wm_client_rect(e, &ex, &ey, &ecw, &ech);
    CHECK(ecw == 800 && ech == 600);
    CHECK(find_hit(HIT_PANEL_TASK, epid, &ex, &ey));
    CHECK(click_hit(HIT_WIN_CLOSE, epid));
    CHECK(close_requests == 1 && last_close == 4242 && wm_by_pid(epid) == e);
    alt(K_F4);
    CHECK(close_requests == 2);
    wm_remove(e); /* the program has gone */
    ws_tick();
    CHECK(wm_by_ext(4242) == NULL && !screen_has("Firefox ESR"));

    /* Skarlet Files: double-click a folder to open it. */
    svc_launch(APP_FILES, "/home/user");
    ws_tick();
    w = wm_focused();
    wm_client_rect(w, &cx, &cy, &cw, &ch);
    double_click(cx + 190 + 120, cy + 50 + 10); /* the first row: Desktop */
    CHECK(strstr(w->title, "Desktop - Skarlet Files") != NULL);
    click(cx + 60, cy + 36 + 4 * 36 + 10); /* Places: Root */
    CHECK(strstr(w->title, "/ - Skarlet Files") != NULL);
    wm_close(w);

    /* Skarlet Settings: click the colour swatches. */
    svc_launch(APP_SETTINGS, 0);
    ws_tick();
    w = wm_focused();
    wm_client_rect(w, &cx, &cy, &cw, &ch);
    int right = cw - 40, row1 = 88 + 52 + 22;
    click(cx + right - 12 - 30 * (g_accent_count - 2), cy + row1); /* Blue */
    CHECK(strcmp(g_accents[g_accent_index].name, "Blue") == 0);
    click(cx + right - 12 - 30 * (g_accent_count - 1), cy + row1); /* Maroon */
    CHECK(g_theme->accent == MAROON);
    wm_close(w);
    ws_tick();

    /* A click outside a popup closes it. */
    alt(K_F2);
    CHECK(ws_popup_open());
    click(g_w / 2, g_h / 2);
    CHECK(!ws_popup_open());

    /* The wheel over the pager switches virtual desktops. */
    int px, py;
    CHECK(find_hit(HIT_PANEL_DESK, 0, &px, &py));
    mouse(MOUSE_WHEEL, px, py, 5, 1);
    CHECK(g_ws.desk == 1);
    CHECK(click_hit(HIT_PANEL_DESK, 0));
    CHECK(g_ws.desk == 0);

    /* Notifications go away when clicked. */
    svc_notify("Test", "Click me");
    ws_tick();
    CHECK(screen_has("Click me"));
    CHECK(click_hit(HIT_TOAST, 0));
    CHECK(!screen_has("Click me"));

    /* Widgets: click to focus, drag the handle's move grip. */
    struct activity *a = &g_ws.activities[0];
    CHECK(click_hit(HIT_WIDGET, 0));
    CHECK(a->focus == 0);
    int wx = a->widgets[0].x, wy = a->widgets[0].y;
    CHECK(find_hit(HIT_WIDGET_MOVE, 0, &px, &py));
    drag_by(px, py, 32, 48);
    CHECK(a->widgets[0].x == wx + 32 && a->widgets[0].y == wy + 48);

    /* The desktop menu and the Add Widgets sheet: one click picks a tile, a
     * second click adds it. */
    alt(K_F12);
    CHECK(click_hit(HIT_ITEM, 0));
    CHECK(screen_has("Add Widgets") && screen_has("Fifteen"));
    CHECK(click_hit(HIT_ITEM, 1));
    CHECK(ws_popup_open() && screen_has("A sticky note to type into"));
    CHECK(click_hit(HIT_ITEM, 1));
    CHECK(!ws_popup_open() && screen_has("Added Notes"));
    save("mouse");
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
        /* Queue keys in small bursts: each burst is one redraw, as when a
         * fast typist's keys arrive between two frames. */
        queue[qtail] = (struct key){ code, mods };
        qtail = (qtail + 1) % 64;
        if (i % 8 == 7)
            ws_tick();
        /* Now and then the mouse: clicks, double clicks, drags and the wheel
         * anywhere, and other programs' windows coming and going. */
        if (i % 13 == 0) {
            uint32_t m = k_rand();
            int x = (int)(m % (uint32_t)g_w), y = (int)((m >> 11) % (uint32_t)g_h);
            int type = (int)((m >> 22) % 4), clicks = 1 + (int)((m >> 26) & 1);
            ws_mouse((struct mouse){ type, x, y, type == MOUSE_WHEEL ? 4 + (int)((m >> 27) & 1) : 1,
                                     clicks });
        }
        if (i % 401 == 0)
            wm_open_external(1000 + i, "Fuzz window", 300 + i % 500, 200 + i % 300);
        if (i % 577 == 0) {
            struct window *all[MAX_WIN];
            int n = wm_list_all(all, MAX_WIN);
            for (int j = 0; j < n; j++)
                if (all[j]->ext) {
                    wm_remove(all[j]);
                    break;
                }
        }
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
    test_launcher_terminal();
    test_runner();
    test_windows_and_desktops();
    test_write();
    test_toolbox_activities_widgets();
    test_settings_theme();
    test_leave();
    test_mouse();
    test_fuzz();
    printf("%d checks, %d failures, %d frames drawn\n", checks, failures, frames);
    return failures ? 1 : 0;
}
