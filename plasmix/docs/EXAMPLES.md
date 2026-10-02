# Plasmix demo and examples

Everything on this page was produced by running Plasmix, not drawn or typed by hand:

* The **screenshots** come from the real kernel (`build/plasmix.bin`) booted in the
  Unicorn x86-64 emulator. `tools/demo_tour.py` presses keys on the emulated PS/2
  keyboard and saves the VGA text memory after each step. `tools/vga_render.py`
  draws that memory with the standard VGA palette and the Unifont 8×16 font.
  Every step checks that the expected text is on screen before saving it.
  Re-create them with `make demo`.
* The **shell transcripts** are output of `build/plasmix-sh` (`make shell`), which
  runs the same shell and file-system code as the kernel.
* The **code examples** were applied to a copy of the source tree. That copy was
  built (kernel and tests) and checked before they were written here.

The emulated clock starts at 10:30:00 on 1 August 2012, the day KDE released
Plasma Workspaces 4.9, and ticks one second per step.

![The whole tour as an animation](screenshots/tour.gif)

## Screenshot tour

Each caption says which keys led to the screen.

### 1. Logging in
![Login screen](screenshots/01-login.png)
The login screen after typing a password (any password works; it is not
checked). The clock is at the top.

### 2. The desktop
![Desktop](screenshots/02-desktop.png)
**Enter** logs in. The desktop containment holds three plasmoids: Folder View
(the files in `~/Desktop`), the Digital Clock and Notes. The welcome notification
sits above the panel. Along the bottom: Kickoff `K`, the pager `1 2 3 4`, the task
manager (empty), the system tray, the clock and the panel toolbox `☼`. The
desktop toolbox ("cashew") is the `☼` in the top right corner.

### 3. Kickoff, the application launcher
![Kickoff favourites](screenshots/03-kickoff.png)
**Alt+F1** opens Kickoff: user name, search box, favourites list, and the
Favorites / Applications / Computer / Recent / Leave tabs.

![Kickoff applications](screenshots/04-kickoff-applications.png)
**Right arrow** switches to the Applications tab, which shows every app with its
category.

### 4. KRunner as a calculator
![KRunner calculator](screenshots/05-krunner-calculator.png)
**Alt+F2** opens KRunner. Typing `(12+30)*2` gives `= 84` from the calculator
runner, plus the command-line runner's "Run …" result.

### 5. Konsole and the shell
![Konsole](screenshots/06-konsole.png)
**Alt+F1, Enter** starts Konsole (the first favourite). Commands typed:
`uname -a`, `ls -l`, `echo Hello from Plasmix > hello.txt`, `cat hello.txt`,
`calc (12+30)*2`.

### 6. KRunner opening a place
![KRunner places](screenshots/07-krunner-places.png)
**Alt+F2** and `/home/user/Desktop`: the places runner offers to open it in
Dolphin.

### 7. Dolphin, the file manager
![Dolphin](screenshots/08-dolphin.png)
**Enter** opens Dolphin there: Places panel on the left, files on the right,
location bar on top, folder/file count at the bottom.

### 8. KWrite, the editor
![KWrite](screenshots/09-kwrite.png)
**Enter** on `README.txt` opens it in KWrite, with line numbers and a status bar.
**Ctrl+S** saves.

### 9. System Activity
![System Activity](screenshots/10-system-activity.png)
**Ctrl+Esc** lists the running "processes" (one per window) with PID, name,
virtual desktop and title. **Delete** ends the selected one.

### 10. Dashboard
![Dashboard](screenshots/11-dashboard.png)
**Ctrl+F12** hides the windows to show the desktop widgets; press it again to
bring them back.

### 11. The desktop toolbox and adding a widget
![Toolbox](screenshots/12-toolbox.png)
**Alt+F12** opens the desktop toolbox: Add Widgets, Activities, Lock Widgets,
Keyboard Shortcuts.

![Add Widgets](screenshots/13-add-widgets.png)
**Enter** lists the widget types with descriptions.

![Widget added](screenshots/14-widget-added.png)
Choosing **System Monitor** adds it to the first free spot and shows a
notification. **Ctrl+F12** shows the dashboard so the new widget is visible.

### 12. Activities
![Activities menu](screenshots/15-activities.png)
Toolbox → **Activities...** lists the activities (`•` marks the current one), plus
New Activity and Remove This Activity.

![Play activity](screenshots/16-activity-play.png)
Switching to **Play** shows a different set of widgets: a System Monitor and the
Fifteen Puzzle, focused with **Tab** and played with the arrow keys. The windows
stay behind in the "Desktop" activity they belong to.

### 13. System Settings and the Oxygen theme
![Settings](screenshots/17-settings-oxygen.png)
Back in "Desktop", **Alt+F2** `settings` **Enter** opens System Settings. Changes
made: theme Air → Oxygen, wallpaper → Dots, clock → 12-hour (the panel now reads
`10:30 AM`).

![Oxygen desktop](screenshots/18-oxygen-desktop.png)
**Alt+F4** closes System Settings. The windows and panel now use the dark Oxygen
colours.

### 14. Leaving
![Leave tab](screenshots/19-kickoff-leave.png)
**Alt+F1, Left arrow** jumps to Kickoff's Leave tab: Log out, Restart, Shut down.

![Shut down](screenshots/20-shutdown.png)
**Shut down** draws this screen, then writes to the QEMU power-off port (the demo
checks that the write happened).

## Shell examples

Try them with `make shell && ./build/plasmix-sh`, or in Konsole inside Plasmix.

### Looking around the file system
```
user@plasmix:~$ pwd
/home/user
user@plasmix:~$ ls
Desktop/  Documents/  Music/
user@plasmix:~$ ls -a
Desktop/  Documents/  Music/  .profile
user@plasmix:~$ ls -l /
drwxr-xr-x root root     0 bin/
drwxr-xr-x root root     0 etc/
drwxr-xr-x root root     0 home/
drwx------ root root     0 root/
drwxrwxrwt root root     0 tmp/
drwxr-xr-x root root     0 usr/
user@plasmix:~$ cat /etc/os-release
NAME="Plasmix"
VERSION="0.1"
ID=plasmix
PRETTY_NAME="Plasmix 0.1 (x86-64)"
user@plasmix:~$ cd Documents
user@plasmix:~/Documents$ cat plasma-notes.txt
Plasma 4 ideas this desktop copies:
* everything on the desktop and panel is a widget (plasmoid)
* widgets live in containments (the desktop, the panel)
* activities: separate sets of widgets for separate tasks
* KRunner: one text box that launches, calculates and runs
```
Note the UNIX details: `ls` hides "dot files" unless you add `-a`, and `ls -l`
shows permission bits. On a real UNIX system `drwx------` would make `/root`
private, and the sticky bit (`t`) on `/tmp` would stop users deleting each
other's files. **Plasmix only displays these bits; it does not enforce them.**
There is a single user and no permission checks, which would be a good exercise
to add.

### Making, changing and removing files
```
user@plasmix:~$ mkdir projects
user@plasmix:~$ cd projects
user@plasmix:~/projects$ echo "first line" > notes.txt
user@plasmix:~/projects$ echo "second line" >> notes.txt
user@plasmix:~/projects$ cat notes.txt
first line
second line
user@plasmix:~/projects$ wc notes.txt
   2    4    23 notes.txt
user@plasmix:~/projects$ cp notes.txt backup.txt
user@plasmix:~/projects$ mv backup.txt old.txt
user@plasmix:~/projects$ ls -l
-rw-r--r-- user user    23 notes.txt
-rw-r--r-- user user    23 old.txt
user@plasmix:~/projects$ rm old.txt
user@plasmix:~/projects$ ls
notes.txt
user@plasmix:~/projects$ cd ..
user@plasmix:~$ rm -r projects
user@plasmix:~$ ls
Desktop/  Documents/  Music/
```
`>` replaces a file's contents and `>>` appends. `wc` prints lines, words and
bytes: 23 bytes is the 21 visible characters plus the two newlines.

### Errors, UNIX style
```
user@plasmix:~$ cat missing.txt
cat: missing.txt: No such file or directory
user@plasmix:~$ cd /etc/passwd
cd: /etc/passwd: Not a directory
user@plasmix:~$ rm /bin
rm: /bin: Is a directory
user@plasmix:~$ rmdir /home
rmdir: /home: Directory not empty
user@plasmix:~$ mkdir /tmp
mkdir: /tmp: File exists
user@plasmix:~$ echo hi >
sh: syntax error near '>'
user@plasmix:~$ frobnicate
sh: frobnicate: command not found
```
These are the usual C library messages for the error codes ENOENT, ENOTDIR,
EISDIR, ENOTEMPTY and EEXIST. `src/vfs.h` uses Linux's numbers for them; other
UNIX systems number some codes differently (ENOTEMPTY, for example).

### Maths and system information
```
user@plasmix:~$ calc 2+3*4
14
user@plasmix:~$ calc (2+3)*4
20
user@plasmix:~$ calc 17 % 5
2
user@plasmix:~$ calc 7/0
calc: invalid expression
user@plasmix:~$ uname -a
Plasmix plasmix 0.1 #1 x86_64 Plasmix
user@plasmix:~$ whoami
user
user@plasmix:~$ hostname
plasmix
```
`calc` uses whole numbers only (`7/2` is `3`). It refuses division by zero and
results outside the 32-bit range.

### Windows as processes
```
user@plasmix:~$ konsole
user@plasmix:~$ dolphin /etc
user@plasmix:~$ kwrite ~/Desktop/todo.txt
user@plasmix:~$ ps
  PID CMD        DESK  TITLE
    1 plasma        -  Plasma Desktop Shell
  100 konsole       1  user - Konsole
  101 dolphin       1  etc - Dolphin
  102 kwrite        1  todo.txt - KWrite
user@plasmix:~$ kill 101
user@plasmix:~$ ps
  PID CMD        DESK  TITLE
    1 plasma        -  Plasma Desktop Shell
  100 konsole       1  user - Konsole
  102 kwrite        1  todo.txt - KWrite
user@plasmix:~$ kdialog --msgbox "Build finished"
user@plasmix:~$ history
   1  konsole
   2  dolphin /etc
   3  kwrite ~/Desktop/todo.txt
   4  ps
   5  kill 101
   6  ps
   7  kdialog --msgbox "Build finished"
   8  history
```
Plasmix has no real processes. Each window gets a process ID so that `ps` and
`kill` behave the UNIX way. In the desktop, `kdialog --msgbox` shows a
notification.

## KRunner examples

Open KRunner with **Alt+F2**, type, then pick a result with the arrow keys and
**Enter**.

| You type | You get |
|---|---|
| `6*7` or `=6*7` | `= 42` (calculator runner). Enter puts 42 back in the box |
| `dolphin`, `file`, `term` | Matching apps by name, id or generic name ("File Manager", "Terminal") |
| `/etc`, `~/Documents` | "Open /etc" in Dolphin. A file path opens in KWrite |
| `echo hi > /tmp/x` | "Run …": opens Konsole and runs it there |

## Code examples: extending Plasmix

### A new shell command: `rev`
Add this to `src/shell.c`, above `struct command`:

```c
static int cmd_rev(struct ctx *c)
{
    for (int i = 1; i < c->argc; i++) {
        int n = k_strlen(c->argv[i]);
        char buf[SH_LINE];
        for (int j = 0; j < n; j++)
            buf[j] = c->argv[i][n - 1 - j];
        buf[n] = 0;
        out(c, buf);
        out(c, i + 1 < c->argc ? " " : "");
    }
    out(c, "\n");
    return 0;
}
```

Then register it in two places in the same file: `{ "rev", cmd_rev },` in the
`commands[]` table (which runs it), and `"rev",` in `g_shell_commands[]` (which
makes `help` list it and puts `/bin/rev` in the file system). Result:

```
user@plasmix:~$ rev hello world
olleh dlrow
```

Because the command uses `out()`, redirection works without extra code:
`rev abc > x.txt`.

### A new widget: Counter
Add this to `src/plasmoids.c`, above `g_plasmoid_types`:

```c
static void counter_draw(struct plasmoid *p, int x, int y, int w, int h, int focused)
{
    char buf[16];
    k_snprintf(buf, sizeof buf, "%d", p->sel); /* p->sel is free to use here */
    gfx_center(x, y + h / 2, w, buf, g_theme->widget_head);
    if (focused)
        gfx_center(x, y + h - 1, w, "+ / - to count", g_theme->widget);
}

static int counter_key(struct plasmoid *p, struct key k)
{
    if (k.code == '+' || k.code == '=')
        p->sel++;
    else if (k.code == '-')
        p->sel--;
    else
        return 0; /* not ours: let the desktop handle it */
    return 1;
}
```

Add a row to the `g_plasmoid_types` table:

```c
    [PL_COUNTER] = { "counter", "Counter", "Press + and - to count", 18, 5, 0,
                     counter_draw, counter_key },
```

and a name to the enum in `src/desktop.h`, just before `PL_COUNT`:

```c
enum { PL_FOLDERVIEW, PL_NOTES, PL_CLOCK, PL_SYSMON, PL_FIFTEEN, PL_COUNTER, PL_COUNT };
```

Counter now appears in **Alt+F12 → Add Widgets...**. In the check run, adding it
and pressing `+ + + -` showed `2`.

This is the Plasma idea in miniature. The desktop containment never needs to
know what a Counter is: it only calls `draw` and `key` through the table.

### Things to try next
* `head FILE` (first 5 lines) or `grep WORD FILE` as shell commands.
* A **Calendar** widget using the date in `g_ws.now`.
* A third theme: copy one entry of `g_themes[]` in `src/gfx.c` and change its colours.
