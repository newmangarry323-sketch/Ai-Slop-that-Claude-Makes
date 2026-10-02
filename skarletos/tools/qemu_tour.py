#!/usr/bin/env python3
"""qemu_tour.py - a guided tour of the real SkarletOS ISO, one screenshot per step.

Boots the ISO in QEMU (VMware SVGA adapter, so the screen is 1918 x 1075),
presses keys on the emulated PS/2 keyboard and saves a PNG after each step.

Every step also checks what is on the screen.  The kernel keeps a log of the
text it drew in the last frame (gfx_textlog() in src/gfx.c); we find that
buffer's address in build/skarletos.elf with "nm" and read it out of guest
memory with the QEMU monitor's "pmemsave" command.  (The kernel maps memory
1:1, so its addresses are physical addresses.)  A screenshot can therefore
never silently show the wrong thing.

Usage: qemu_tour.py [--qemu PATH] [--uefi CODE.fd] ISO ELF OUT_DIR
"""
import argparse
import os
import shutil
import subprocess
import sys
import tempfile
import time

from qemu_test import Monitor, read_ppm

# Characters to QEMU "sendkey" names (US layout).
KEYS = {" ": "spc", "\n": "ret", "-": "minus", "=": "equal", "/": "slash", ".": "dot",
        ",": "comma", ";": "semicolon", "'": "apostrophe", ">": "shift-dot", "<": "shift-comma",
        "*": "shift-8", "+": "shift-equal", "(": "shift-9", ")": "shift-0", "_": "shift-minus",
        "!": "shift-1", "~": "shift-grave_accent", ":": "shift-semicolon", '"': "shift-apostrophe"}


class Tour:
    def __init__(self, args):
        self.args = args
        out = subprocess.run(["nm", args.elf], capture_output=True, text=True, check=True).stdout
        syms = {line.split()[2]: int(line.split()[0], 16) for line in out.splitlines()
                if len(line.split()) == 3}
        self.textlog = syms["textlog"]
        self.tmp = tempfile.mkdtemp()
        sock = os.path.join(self.tmp, "mon")
        cmd = [args.qemu, "-cdrom", args.iso, "-m", "512M", "-vga", "vmware", "-display", "none",
               "-monitor", "unix:%s,server,nowait" % sock, "-no-reboot", "-no-shutdown",
               "-rtc", "base=2026-08-01T10:30:00"]
        if args.uefi:
            cmd += ["-drive", "if=pflash,format=raw,readonly=on,file=" + args.uefi]
        self.qemu = subprocess.Popen(cmd)
        self.mon = Monitor(sock)
        self.count = 0
        os.makedirs(args.out, exist_ok=True)

    def key(self, *names, gap=0.5):
        for n in names:
            self.mon.cmd("sendkey " + n)
            time.sleep(gap)

    def type(self, text):
        for ch in text:
            name = KEYS.get(ch)
            if name is None:
                name = ("shift-" + ch.lower()) if ch.isupper() else ch
            self.key(name, gap=0.25)

    def screen_text(self):
        path = os.path.join(self.tmp, "textlog")
        # The file name is quoted: unquoted, "65536 /tmp/..." would be read
        # as a division by the monitor's expression parser.
        reply = self.mon.cmd('pmemsave %d 65536 "%s"' % (self.textlog, path))
        if "rror" in reply or "invalid" in reply:
            raise SystemExit("pmemsave failed: " + reply)
        time.sleep(0.3)
        with open(path, "rb") as f:
            return f.read().split(b"\0", 1)[0].decode(errors="replace")

    def shot(self, name, *must_contain, wait=2.5):
        time.sleep(wait)
        text = ""
        for _ in range(10):  # the frame may still be drawing; give it time
            text = self.screen_text()
            if all(t in text for t in must_contain):
                break
            time.sleep(1.5)
        else:
            print(text)
            print(self.mon.cmd("info status")[-200:])
            self.mon.cmd("screendump %s/failed.ppm" % self.args.out)
            missing = [t for t in must_contain if t not in text]
            raise SystemExit("step %r: expected %r on screen" % (name, missing))
        self.count += 1
        base = os.path.join(self.args.out, "%02d-%s" % (self.count, name))
        self.mon.cmd("screendump %s.ppm" % base)
        time.sleep(0.5)
        w, h, _ = read_ppm(base + ".ppm")  # also repairs QEMU's padded rows
        subprocess.run(["convert", base + ".ppm", base + ".png"], check=True)
        os.remove(base + ".ppm")
        print("saved %s.png (%dx%d)" % (base, w, h))

    def close(self):
        self.qemu.kill()
        self.qemu.wait()
        shutil.rmtree(self.tmp, ignore_errors=True)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("iso")
    ap.add_argument("elf")
    ap.add_argument("out")
    ap.add_argument("--qemu", default="qemu-system-x86_64")
    ap.add_argument("--uefi")
    args = ap.parse_args()
    for old in os.listdir(args.out) if os.path.isdir(args.out) else []:
        if old.endswith(".png") and old[:2].isdigit():
            os.remove(os.path.join(args.out, old))

    t = Tour(args)
    try:
        time.sleep(20)  # firmware, Limine and the first frame
        t.type("skarlet")
        t.shot("login", "Welcome to SkarletOS", "user", "Press Enter to log in")
        t.key("ret")
        t.shot("desktop", "Folder View", "Welcome! Type here.", "Welcome", "1 August 2026")
        t.key("alt-f1")
        t.shot("launcher", "on skarlet", "Search:", "Favorites", "Power", "Skarlet Terminal")
        t.key("right", "ret")  # Applications tab, then the System category
        t.shot("launcher-applications", "All Applications", "Skarlet Monitor")
        t.key("esc", "alt-f2")
        t.type("(12+30)*2")
        t.shot("runner-calculator", "84")
        t.key("esc", "alt-f1", "ret")  # first favourite: Skarlet Terminal
        for c in ["uname -a", "ls -l", "echo Hello from SkarletOS > hello.txt",
                  "cat hello.txt", "calc (12+30)*2"]:
            t.type(c + "\n")
            time.sleep(1)
        t.shot("terminal", "x86_64", "Hello from SkarletOS", "84")
        t.key("alt-f2")
        t.type("/home/user/Desktop")
        t.shot("runner-places", "/home/user/Desktop")
        t.key("ret")
        t.shot("files", "README.txt", "todo.txt", "PLACES")
        t.key("ret")  # open README.txt in Skarlet Write
        t.shot("write", "README.txt - Skarlet Write")
        t.key("ctrl-esc")
        t.shot("monitor", "skterm", "skfiles", "skwrite")
        t.key("esc", "ctrl-f12")  # dashboard: widgets above the windows
        t.shot("dashboard", "Folder View", "Welcome! Type here.")
        t.key("ctrl-f12", "alt-f12")
        t.shot("desktop-menu", "Add Widgets...", "Activities...", "Desktop Settings")
        t.key("ret")
        t.shot("add-widgets", "Add Widgets", "Fifteen", "Shows the files on your desktop")
        t.key("right", "right", "right", "ret", "ctrl-f12")  # System Monitor
        t.shot("widget-added", "System Monitor", "Added System Monitor")
        t.key("ctrl-f12", "alt-f12", "down", "ret")
        t.shot("activities", "Activities", "Play", "New Activity")
        t.key("right", "ret", "tab", "left", "up")
        t.shot("activity-play", "Fifteen Puzzle")
        t.key("alt-f12", "down", "ret", "left", "ret")  # back to the Desktop activity
        t.key("alt-f2")
        t.type("settings\n")
        time.sleep(2)
        t.key("ret", "down", "down", "right", "down", "ret")  # light, Dots, 12-hour clock
        t.shot("settings-light", "Skarlet Light", "Maroon", "Dots", "12-hour")
        t.key("alt-f4")
        t.shot("desktop-light", "AM")
        t.key("alt-f1", "right", "right", "right")
        t.shot("launcher-recent", "Recently Used", "README.txt")
        t.key("right")
        t.shot("power", "Log out", "Restart", "Shut down")
        t.key("esc", "alt-f1")
        t.type("shut")
        t.key("ret")
        t.shot("shutdown", "SkarletOS has shut down")
        status = t.mon.cmd("info status")
        if "shutdown" not in status:
            raise SystemExit("expected the guest to power off, QEMU says: " + status)
        print("tour complete: %d screens; the guest powered itself off" % t.count)
    finally:
        t.close()


if __name__ == "__main__":
    sys.path.insert(0, os.path.dirname(__file__))
    main()
