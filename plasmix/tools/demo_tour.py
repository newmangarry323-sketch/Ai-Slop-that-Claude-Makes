#!/usr/bin/env python3
"""Record a guided tour of the real Plasmix kernel, one screen per step.

The kernel image (build/plasmix.bin) is booted in the Unicorn CPU emulator,
exactly like tools/emu_boot_test.py does. Keys are pressed on the emulated
PS/2 keyboard and, after each step, the 4000 bytes of VGA text memory at
0xB8000 are saved as build/demo/NN-name.vga.  tools/vga_render.py turns
those dumps into PNG screenshots.

Every step also checks that the text it expects is on the screen, so a
screenshot can never silently show the wrong thing.

Usage: demo_tour.py build/plasmix.bin build/demo
"""
import os
import struct
import sys

from unicorn.x86_const import UC_X86_REG_EAX, UC_X86_REG_EBX

from emu_boot_test import MB_INFO, VGA, Machine

# Scan code set 1. Plain keys, and keys that need Shift.
PLAIN = {c: 0x02 + i for i, c in enumerate("1234567890")}
PLAIN.update({c: 0x10 + i for i, c in enumerate("qwertyuiop")})
PLAIN.update({c: 0x1E + i for i, c in enumerate("asdfghjkl")})
PLAIN.update({c: 0x2C + i for i, c in enumerate("zxcvbnm")})
PLAIN.update({" ": 0x39, "-": 0x0C, "=": 0x0D, "\n": 0x1C, "/": 0x35, ".": 0x34,
              ",": 0x33, ";": 0x27, "'": 0x28, "\t": 0x0F})
SHIFT = {">": 0x34, "<": 0x33, "*": 0x09, "+": 0x0D, "(": 0x0A, ")": 0x0B,
         '"': 0x28, ":": 0x27, "_": 0x0C, "!": 0x02, "~": 0x29, "?": 0x35}
SHIFT.update({c.upper(): sc for c, sc in PLAIN.items() if c.isalpha()})

LSHIFT, LCTRL, LALT = 0x2A, 0x1D, 0x38
ESC, F1, F2, F4, F12 = 0x01, 0x3B, 0x3C, 0x3E, 0x58
UP, DOWN, LEFT, RIGHT = 0x48, 0x50, 0x4B, 0x4D  # sent with the 0xE0 prefix


class Tour(Machine):
    def __init__(self, image, out_dir):
        super().__init__(image)
        self.out_dir = out_dir
        self.count = 0
        self.clock = 10 * 3600 + 30 * 60  # seconds since midnight: 10:30:00

    def port_in(self, uc, port, size, user):
        # Unlike the boot test, let the CMOS clock run: one second per step,
        # so clocks and uptimes in the screenshots move like on a real PC.
        if port == 0x71 and self.cmos_index in (0x00, 0x02, 0x04):
            value = {0x00: self.clock % 60, 0x02: self.clock // 60 % 60,
                     0x04: self.clock // 3600 % 24}[self.cmos_index]
            return (value // 10) << 4 | value % 10  # BCD
        return super().port_in(uc, port, size, user)

    def type(self, text):
        for ch in text:
            if ch in SHIFT:
                self.chord(LSHIFT, SHIFT[ch])
            else:
                self.tap(PLAIN[ch])

    def ext(self, sc, times=1):
        for _ in range(times):
            self.keys += [0xE0, sc, 0xE0, sc | 0x80]

    def settle(self):
        self.clock += 1
        self.run(instructions=15_000_000)

    def shot(self, name, *must_contain):
        self.settle()
        screen = self.screen()
        for text in must_contain:
            if text not in screen:
                print(screen)
                raise SystemExit("step %r: expected %r on screen" % (name, text))
        self.count += 1
        path = os.path.join(self.out_dir, "%02d-%s.vga" % (self.count, name))
        with open(path, "wb") as f:
            f.write(bytes(self.mu.mem_read(VGA, 80 * 25 * 2)))
        print("saved", path)


def main():
    image = open(sys.argv[1], "rb").read()
    out_dir = sys.argv[2]
    os.makedirs(out_dir, exist_ok=True)
    for old in os.listdir(out_dir):
        if old.endswith(".vga"):
            os.remove(os.path.join(out_dir, old))
    entry = struct.unpack_from("<8I", image, 0)[7]

    t = Tour(image, out_dir)
    t.mu.reg_write(UC_X86_REG_EAX, 0x2BADB002)
    t.mu.reg_write(UC_X86_REG_EBX, MB_INFO)
    t.run(entry, 15_000_000)

    t.type("plasma")
    t.shot("login", "Welcome to Plasmix", "******")

    t.tap(PLAIN["\n"])
    t.shot("desktop", "Folder View", "Digital Clock", "Notes", "Welcome")

    t.chord(LALT, F1)
    t.shot("kickoff", "User user on plasmix", "Favorites", "Konsole")
    t.ext(RIGHT)
    t.shot("kickoff-applications", "System Activity", "Settings / Configure")
    t.tap(ESC)

    t.chord(LALT, F2)
    t.type("(12+30)*2")
    t.shot("krunner-calculator", "= 84", "Calculator")
    t.tap(ESC)

    t.chord(LALT, F1)
    t.tap(PLAIN["\n"])  # first favourite: Konsole
    t.settle()
    for cmd in ["uname -a", "ls -l", "echo Hello from Plasmix > hello.txt",
                "cat hello.txt", "calc (12+30)*2"]:
        t.type(cmd + "\n")
        t.settle()
    t.shot("konsole", "x86_64", "drwxr-xr-x user user", "Hello from Plasmix", "84")

    t.chord(LALT, F2)
    t.type("/home/user/Desktop")
    t.shot("krunner-places", "Open /home/user/Desktop", "Dolphin")
    t.tap(PLAIN["\n"])
    t.shot("dolphin", "Places", "README.txt", "todo.txt")

    t.tap(PLAIN["\n"])  # open README.txt in KWrite
    t.shot("kwrite", "README.txt - KWrite", "Plasmix desktop quick start")

    t.chord(LCTRL, ESC)
    t.shot("system-activity", "System Activity", "konsole", "dolphin", "kwrite")
    t.tap(ESC)

    t.chord(LCTRL, F12)  # dashboard: widgets above the windows
    t.shot("dashboard", "Folder View", "Notes")
    t.chord(LCTRL, F12)

    t.chord(LALT, F12)
    t.shot("toolbox", "Desktop Toolbox", "Add Widgets...", "Activities...")
    t.tap(PLAIN["\n"])
    t.shot("add-widgets", "Add Widgets", "Fifteen Puzzle", "System Monitor")
    t.ext(DOWN, 3)  # System Monitor
    t.tap(PLAIN["\n"])
    t.chord(LCTRL, F12)
    t.shot("widget-added", "System Monitor", "Added System Monitor")
    t.chord(LCTRL, F12)

    t.chord(LALT, F12)
    t.ext(DOWN, 2)  # Add Widgets, Remove System Monitor, Activities...
    t.tap(PLAIN["\n"])
    t.shot("activities", "Activities", "Play", "New Activity")
    t.ext(DOWN)
    t.tap(PLAIN["\n"])
    t.tap(PLAIN["\t"])  # focus the Fifteen Puzzle
    t.ext(LEFT)
    t.ext(UP)
    t.shot("activity-play", "Fifteen Puzzle", "System Monitor")

    t.chord(LALT, F12)
    t.ext(DOWN, 2)
    t.tap(PLAIN["\n"])  # Activities...
    t.tap(PLAIN["\n"])  # Desktop
    t.chord(LALT, F2)
    t.type("settings")
    t.tap(PLAIN["\n"])
    t.settle()
    t.tap(PLAIN["\n"])  # Desktop theme -> Oxygen
    t.ext(DOWN)
    t.ext(RIGHT)        # wallpaper -> Dots
    t.ext(DOWN)
    t.tap(PLAIN["\n"])  # 12-hour clock
    t.shot("settings-oxygen", "System Settings", "Oxygen", "12-hour", "AM")
    t.chord(LALT, F4)
    t.shot("oxygen-desktop", "Konsole", "KWrite")

    t.chord(LALT, F1)
    t.ext(LEFT)  # Favorites -> Leave
    t.shot("kickoff-leave", "Log out", "Restart", "Shut down")
    t.ext(DOWN, 2)
    t.tap(PLAIN["\n"])
    t.shot("shutdown", "Plasmix has shut down", "safe to turn off")
    if not t.poweroff:
        raise SystemExit("expected the kernel to write the QEMU power-off port")
    print("tour complete: %d screens" % t.count)


if __name__ == "__main__":
    main()
