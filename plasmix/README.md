# Plasmix

A small UNIX-like operating system for **x86-64 PCs**, written in C, whose desktop
follows the design ideas of the **2012 KDE Plasma 4 workspace** (Plasma Workspaces
4.8 and 4.9). It boots on its own, with no Linux underneath, and draws everything
in the 80×25 VGA text mode.

```
░░┌──── Folder View ─────┐░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░┌── Digital Clock ──┐░░
░░│ ≡ README.txt         │▒▒▒▒▒░░░░░░░░░░░░░░░░░░░░░░░░░░│ ▀█  █▀█ ∙  ▀▀█ █▀█│░░
░░│ ≡ todo.txt           │░░░▒▒▒▒▒▒▒░░░░░░░░░░░░░░░░░░░░░│  █  █ █ ∙   ▀█ █ █│░░
░░│                      │░░░░░░░░▒▒▒▒▒▒▒░░░░░░░░░░░░░░░░│ ▀▀▀ ▀▀▀    ▀▀▀ ▀▀▀│░░
░░└──────────────────────┘░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░└───────────────────┘░░
░░┌─────── Notes ────────┐▒▒░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░▒▒▒▒▒▒▒░░░░░░░░░
░░│Welcome! Type here.   │░░▒▒▒▒▒░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░▒▒▒▒▒▒▒░░░░
░░└──────────────────────┘░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░
 K │ 1  2  3  4 │ ■ Konsole           ■ Dolphin         │ ♪ i │      10:30 │ ☼
```

It is a learning project: about 5,700 lines you can read in an afternoon or two.
It is **not** a real UNIX. There are no processes, no memory protection and no disk.
See [What it does not do](#what-it-does-not-do).

## What you get

* **A 64-bit kernel.** The boot loader starts the CPU in 32-bit mode. `boot/boot.S`
  checks the CPU supports x86-64, builds page tables, switches to *long mode*, and
  then calls C.
* **A UNIX-style shell** inside a Konsole window, with `ls -l`, `cd`, `cat`, `echo`,
  `> file` / `>> file` redirection, `mkdir`, `rm -r`, `cp`, `mv`, `wc`, `ps`, `kill`,
  `date`, `uname -a`, `history`, `calc` and more (`help` lists them all).
* **An in-memory file system** with a normal UNIX tree (`/bin`, `/etc`, `/home/user`,
  `/tmp`…), permission bits and errno-style errors. Every shell command also shows
  up as a file in `/bin`.
* **The Plasma 4 desktop model** (next section): plasmoids, containments, a panel,
  activities, Kickoff, KRunner, the desktop toolbox ("cashew"), Air and Oxygen
  themes, and notifications.
* **Apps:** Konsole (terminal), Dolphin (file manager), KWrite (editor),
  System Settings, and System Activity (process list). These are tiny homages that
  share the KDE names, not ports of the real programs.
* **Widgets:** Folder View, Notes, a big Digital Clock, System Monitor, and the
  Fifteen Puzzle.

## The 2012 Plasma design ideas, and where they live in the code

| Plasma 4 idea | What it means | In Plasmix |
|---|---|---|
| **Plasmoid** | Everything on the desktop and the panel is a widget, a "plasmoid" | `src/plasmoids.c`: each widget is a table of `init/draw/key` functions |
| **Containment** | A widget that holds other widgets. The desktop and the panel are both containments | Desktop: `draw_widgets()`. Panel: the `panel_applets[]` table in `src/workspace.c` |
| **Activities** | Separate desktops, each with its own set of widgets, for different tasks | `struct activity` in `src/desktop.h`. "Desktop" and "Play" ship by default; windows belong to an activity too |
| **Desktop toolbox ("cashew")** | The Plasma logo in the corner that holds Add Widgets, Activities and Lock Widgets | The `☼` top right, opened with Alt+F12 |
| **Kickoff** | Launcher with Favorites / Applications / Computer / Recently Used / Leave tabs and a search box | `draw_kickoff()` and `kickoff_build()` |
| **KRunner** | One text box that launches apps, opens places, does maths and runs commands | `krunner_build()`: calculator, application, places and command-line "runners" |
| **Desktop themes** | The look ("Air" light, "Oxygen" dark) is separate from the widgets | `struct theme` in `src/gfx.h`. Switch it in System Settings |
| **Panel** | A containment along a screen edge holding applets: launcher, pager, task manager, system tray, clock, panel toolbox | Bottom row, applets left to right |

## Building and running

You need `gcc`, GNU `ld` and `make` on Linux (on other systems, use an
`x86_64-elf` cross-compiler).

```sh
cd plasmix
make            # builds build/plasmix.bin (57 KB) and build/plasmix.elf
make run        # boots it in QEMU: qemu-system-x86_64 -kernel build/plasmix.bin
make iso        # bootable CD image via GRUB (needs grub-mkrescue and xorriso)
make host       # build/plasmix-tty: the same desktop inside a terminal (80x25 or bigger)
make test       # 171 scripted UI/shell/file-system checks plus a 40,000-key fuzz run
make emutest    # boots the real kernel in the Unicorn CPU emulator (pip install unicorn)
```

`plasmix.bin` is a Multiboot kernel. It uses the Multiboot "address fields" (flag
bit 16), so 32-bit loaders such as QEMU's `-kernel` and GRUB's `multiboot` command
can load it even though the code inside is 64-bit.

### Keys

| Key | Action | Same as KDE 4? |
|---|---|---|
| Alt+F2 | KRunner | Yes, documented by KDE UserBase [2] |
| Alt+F1 | Kickoff launcher | My choice; **not verified** as a KDE default |
| Alt+F12 | Desktop toolbox: widgets, activities, lock | Plasmix only (KDE used the mouse here) |
| Alt+Tab / Alt+F4 | Next window / close window | Common KDE bindings; not checked against a source |
| Alt+F7, then arrows | Move the window | Meant to match KWin; **not verified** |
| Ctrl+F1 … Ctrl+F4 | Switch virtual desktop | Meant to match KDE; **not verified** |
| Ctrl+F12 | Dashboard (widgets over windows) | Meant to match KDE 4; **not verified** |
| Ctrl+Esc | System Activity | Meant to match KDE 4; **not verified** |
| Tab / Shift+Tab | On the desktop: focus the next/previous widget | Plasmix |
| Alt+arrows | Move the focused widget (when widgets are unlocked) | Plasmix |
| Shift+PgUp / PgDn | Konsole scrollback | Konsole convention |
| Ctrl+S | Save in KWrite | Standard |

"Not verified" means I wrote these from memory of KDE 4 and could not confirm
them against KDE's documentation. Check them yourself if it matters to you.

In `plasmix-tty`, terminals often swallow Alt/Ctrl+F-keys, so **Ctrl+A** then a
letter also works: `k` Kickoff, `r` KRunner, `w` toolbox, `t` next window, `x` close,
`m` move, `1`–`4` desktops, `d` dashboard, `s` System Activity, `q` quit.

## How it fits together

```
boot/boot.S          32-bit entry -> page tables -> long mode -> kmain()
boot/linker.ld       memory layout (kernel at 1 MiB)
kernel/arch_x86_64.c the only hardware code: VGA text memory, PS/2 keyboard,
                     CMOS clock, reboot/power-off. Implements src/platform.h
src/platform.h       the small hardware boundary (keys, screen, time, power)
src/workspace.c      main loop, key routing, panel, Kickoff, KRunner, toolbox,
                     activities, notifications, login screen
src/wm.c             window manager: stacking, focus, frames, virtual desktops
src/apps.c           Konsole, Dolphin, KWrite, System Settings, System Activity
src/plasmoids.c      desktop widgets
src/shell.c          command parsing, redirection, built-in commands
src/vfs.c            the in-memory file system and the starting files
src/gfx.c            back buffer drawing and the Air/Oxygen themes
src/lib.c            string functions, printf, calculator (no libc in a kernel)
host/                Linux implementations of platform.h: tests and terminal app
tools/               emulator boot test
```

Because only `kernel/arch_x86_64.c` touches hardware, the rest compiles unchanged
on Linux. That is how the tests run the real desktop code.

## How it was tested

* `make test` runs the desktop code on Linux under AddressSanitizer and UBSan. It
  makes 171 checks across the shell, file system, launcher, KRunner, windows,
  activities, widgets, themes and shutdown, then sends 40,000 random key presses.
  All pass.
* `make emutest` boots the actual `plasmix.bin` in the Unicorn CPU emulator,
  starting at the 32-bit entry point exactly as a Multiboot loader would. It
  confirms paging, PAE and long mode (EFER.LME/LMA) are active and that the CPU is
  in the 64-bit code segment. It then types on the emulated PS/2 keyboard to log
  in, use KRunner, run `uname -a` and `ls /` in Konsole, and shut down through the
  QEMU power-off port. All pass.
* **Not yet tested:** QEMU itself (it couldn't be installed where this was built),
  GRUB/ISO booting, and real hardware. Real hardware is the least likely to work:
  newer PCs may lack VGA text mode or PS/2 keyboard emulation, and power-off needs
  ACPI there.

## What it does not do

No processes or scheduler, no user/kernel separation, no interrupts (the keyboard
and clock are *polled*, which keeps one CPU core busy), no disk (files vanish on
reboot), no mouse, no networking, no graphics mode. Passwords are not checked.
Each of these is a good next project.

## Learning from it, and exercises

Read the files in this order: `boot/boot.S`, `kernel/arch_x86_64.c`, `src/vfs.c`,
`src/shell.c`, `src/plasmoids.c`, `src/workspace.c`. Then try, roughly from
easiest to hardest:

1. Add a shell command (say `rev` or `head`) in `src/shell.c`. Add it to both tables.
2. Write a new plasmoid (a calendar?) in `src/plasmoids.c` and add it to the Add Widgets list.
3. Add a third theme to `g_themes` in `src/gfx.c`.
4. Support pipes (`ls | wc`) in the shell.
5. Read the mouse from the PS/2 controller (OSDev "Mouse Input") and draw a pointer.
6. Replace polling with interrupts: build an IDT, remap the PIC, and use `hlt` to idle.

The OSDev wiki pages in the sources below explain the hardware side of each step.

## Sources

The KDE facts come from KDE's own announcements and wikis; the hardware facts
come from the OSDev wiki and the Multiboot specification.

1. KDE, "KDE Plasma Workspaces 4.8 Gain Adaptive Power Management" (4.8 released
   25 January 2012): https://kde.org/announcements/4/4.8.0/plasma/
2. KDE UserBase, "Plasma/Krunner" (KRunner, Alt+F2, runners):
   https://userbase.kde.org/Plasma/Krunner
3. KDE, "Plasma Workspaces 4.9" (4.9.0 released 1 August 2012):
   https://kde.org/announcements/4/4.9.0/plasma/
4. KDE TechBase, "Projects/Plasma/Vocabulary" (plasmoid, containment, "cashew"
   desktop toolbox): https://techbase.kde.org/Projects/Plasma/Vocabulary
5. KDE TechBase, "Projects/Plasma/Plasmoids": https://techbase.kde.org/Projects/Plasma/Plasmoids
6. OSDev Wiki, "Bare Bones" (Multiboot kernel, VGA text buffer at 0xB8000):
   https://wiki.osdev.org/Bare_Bones
7. OSDev Wiki, "8042 PS/2 Controller" (ports 0x60/0x64) and "PS/2 Keyboard":
   https://wiki.osdev.org/%228042%22_PS/2_Controller and https://wiki.osdev.org/PS/2_Keyboard
8. OSDev Wiki, "CMOS" and "RTC" (ports 0x70/0x71): https://wiki.osdev.org/CMOS and
   https://wiki.osdev.org/RTC
9. OSDev Wiki, "Shutdown" (QEMU 0x604, Bochs/older QEMU 0xB004) and "Reboot"
   (8042 reset, 0xFE): https://wiki.osdev.org/Shutdown and https://wiki.osdev.org/Reboot
10. OSDev Wiki, "Setting Up Long Mode": https://wiki.osdev.org/Setting_Up_Long_Mode
11. GNU, "Multiboot Specification version 0.6.96" (header address fields, flag bit 16):
    https://www.gnu.org/software/grub/manual/multiboot/multiboot.html
12. Unicorn CPU emulator (used by `make emutest`): https://github.com/unicorn-engine/unicorn
