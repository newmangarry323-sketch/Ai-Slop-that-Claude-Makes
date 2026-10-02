#!/usr/bin/env python3
"""linux_iso_test.py - boot the SkarletOS Debian ISO in QEMU and use it.

The test types on the virtual keyboard (QEMU monitor "sendkey"), takes
screenshots ("screendump"), and reads the virtual serial port, where the
commands it runs in Skarlet Terminal write markers like "APT-OK" (through
"sudo tee /dev/ttyS0").  Steps:

  1. boot from the ISO and wait for the SkarletOS login screen
  2. log in with the live password (checked for real, through PAM)
  3. open Skarlet Terminal from the launcher (Alt+F1, Enter)
  4. sudo apt-get install x11-apps x11-utils (from Debian's servers)
  5. start xeyes, a graphical program, and check that its window exists
  6. optionally (--install DISK.img): install to a virtual disk with
     skarlet-install, reboot from the disk and log in with the new password

Usage: linux_iso_test.py ISO [--qemu Q] [--uefi CODE.fd] [--out DIR] [--install DISK]
"""
import argparse
import base64
import os
import shutil
import subprocess
import sys
import tempfile
import time

sys.path.insert(0, os.path.dirname(__file__))
from qemu_test import Monitor, read_ppm, pixel  # noqa: E402

# US keyboard: characters to QEMU sendkey names.
SHIFTED = {'!': '1', '@': '2', '#': '3', '$': '4', '%': '5', '^': '6', '&': '7', '*': '8',
           '(': '9', ')': '0', '_': 'minus', '+': 'equal', '{': 'bracket_left',
           '}': 'bracket_right', '|': 'backslash', ':': 'semicolon', '"': 'apostrophe',
           '<': 'comma', '>': 'dot', '?': 'slash', '~': 'grave_accent'}
PLAIN = {' ': 'spc', '\n': 'ret', '-': 'minus', '=': 'equal', '[': 'bracket_left',
         ']': 'bracket_right', '\\': 'backslash', ';': 'semicolon', "'": 'apostrophe',
         ',': 'comma', '.': 'dot', '/': 'slash', '`': 'grave_accent'}


class Machine:
    def __init__(self, args, boot_disk=False):
        self.args = args
        self.tmp = tempfile.mkdtemp()
        self.serial = os.path.join(args.out, "serial-%s.log" % ("disk" if boot_disk else "iso"))
        sock = os.path.join(self.tmp, "mon")
        cmd = [args.qemu, "-m", "2048", "-smp", "2", "-vga", args.vga, "-display", "none",
               "-monitor", "unix:%s,server,nowait" % sock, "-serial", "file:" + self.serial,
               "-netdev", "user,id=n", "-device", "e1000,netdev=n", "-no-reboot"]
        if os.access("/dev/kvm", os.R_OK | os.W_OK):
            cmd += ["-enable-kvm", "-cpu", "host"]
        if args.uefi:
            cmd += ["-drive", "if=pflash,format=raw,readonly=on,file=" + args.uefi]
        if args.install:
            cmd += ["-drive", "file=%s,format=raw,if=virtio" % args.install]
        if not boot_disk:
            cmd += ["-cdrom", args.iso, "-boot", "d"]
        print(" ".join(cmd), flush=True)
        self.qemu = subprocess.Popen(cmd)
        self.mon = Monitor(sock)
        self.shots = 0

    def key(self, *names, gap=0.15):
        for n in names:
            self.mon.cmd("sendkey " + n)
            time.sleep(gap)

    def type(self, text):
        for ch in text:
            if ch in SHIFTED:
                self.key("shift-" + SHIFTED[ch])
            elif ch in PLAIN:
                self.key(PLAIN[ch])
            elif ch.isupper():
                self.key("shift-" + ch.lower())
            else:
                self.key(ch)

    def shot(self, name):
        self.shots += 1
        base = os.path.join(self.args.out, name)
        self.mon.cmd("screendump %s.ppm" % base)
        time.sleep(0.5)
        img = read_ppm(base + ".ppm")
        if shutil.which("convert"):
            subprocess.run(["convert", base + ".ppm", base + ".png"], check=False)
            # A small JPEG of the screen in the log, readable where the
            # build's files cannot be downloaded.
            thumb = subprocess.run(["convert", base + ".ppm", "-resize", "480x", "-quality", "60",
                                    "jpg:-"], capture_output=True).stdout
            print("THUMB %s %s" % (name, base64.b64encode(thumb).decode()), flush=True)
            os.remove(base + ".ppm")
        return img

    def serial_has(self, text):
        try:
            with open(self.serial, "rb") as f:
                return text.encode() in f.read()
        except OSError:
            return False

    def wait_serial(self, text, timeout):
        end = time.time() + timeout
        while time.time() < end:
            if self.serial_has(text):
                return True
            time.sleep(2)
        return False

    def close(self):
        self.qemu.kill()
        self.qemu.wait()
        shutil.rmtree(self.tmp, ignore_errors=True)


def looks_like_login(img):
    """The SkarletOS login screen: a dark picture with a big white clock above
    the middle."""
    w, h, _ = img
    if w < 640:
        return False
    bright = sum(1 for y in range(h // 6, h // 2, 4) for x in range(w // 3, 2 * w // 3, 4)
                 if sum(pixel(img, x, y)) > 650)
    dark = sum(1 for y in range(0, h, 16) for x in range(0, w, 16) if sum(pixel(img, x, y)) < 200)
    return bright > 40 and dark > (w // 16) * (h // 16) // 2


def looks_like_desktop(img):
    """The desktop: the maroon launcher button at the left end of the panel
    (searched for near the bottom-left corner)."""
    w, h, _ = img
    hits = 0
    for y in range(h - 70, h, 2):
        for x in range(0, 120, 2):
            r, g, b = pixel(img, x, y)
            hits += r > 100 and g < 40 and b < 40
    print("  maroon samples near the launcher button: %d; pixel (36, h-20) = %s" %
          (hits, pixel(img, 36, h - 20)), flush=True)
    return hits > 20


def wait_screen(m, test, name, timeout):
    end = time.time() + timeout
    img = None
    while time.time() < end:
        img = m.shot(name)
        if test(img):
            return img
        time.sleep(5)
    return None


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("iso")
    ap.add_argument("--qemu", default="qemu-system-x86_64")
    ap.add_argument("--vga", default="std")
    ap.add_argument("--uefi")
    ap.add_argument("--out", default="build/linux-test")
    ap.add_argument("--install", help="a raw disk image to install to, then boot from")
    ap.add_argument("--boot-only", action="store_true", help="stop at the login screen")
    args = ap.parse_args()
    os.makedirs(args.out, exist_ok=True)
    failures = 0

    def check(cond, what):
        nonlocal failures
        print(("ok   " if cond else "FAIL ") + what, flush=True)
        failures += not cond
        return cond

    m = Machine(args)
    try:
        img = wait_screen(m, looks_like_login, "01-login", 600)
        if not check(img is not None, "the SkarletOS login screen appears"):
            return 1
        print("screen: %dx%d" % (img[0], img[1]))
        if args.boot_only:
            return 0
        m.type("wrong\n")
        time.sleep(3)
        check(looks_like_login(m.shot("02-wrong-password")), "a wrong password is refused")
        m.type("skarlet\n")
        check(wait_screen(m, looks_like_desktop, "03-desktop", 60) is not None,
              "the right password opens the desktop")

        m.key("alt-f1", gap=2)
        m.shot("04-launcher")
        m.key("ret", gap=6) # Skarlet Terminal
        m.shot("05-terminal")
        m.type("sudo apt-get update && sudo apt-get install -y x11-apps x11-utils && "
               "echo APT-OK | sudo tee /dev/ttyS0\n")
        time.sleep(4)
        m.type("skarlet\n") # sudo's password
        check(m.wait_serial("APT-OK", 900), "sudo apt-get install x11-apps works")
        m.shot("06-apt")
        m.type("xeyes & sleep 5; xwininfo -root -tree | grep -q '\"xeyes\"' && "
               "echo XEYES-OK | sudo tee /dev/ttyS0\n")
        check(m.wait_serial("XEYES-OK", 60), "xeyes (a graphical program) opens a window")
        time.sleep(2)
        m.shot("07-xeyes")
        m.key("alt-f1", gap=2)
        m.type("eyes")
        time.sleep(4)
        m.shot("08-launcher-search")
        m.key("esc", gap=1)

        if args.install:
            m.key("alt-tab", gap=1) # back to the terminal (xeyes is on top)
            m.type("sudo skarlet-install --disk /dev/vda --password newpass --yes && "
                   "echo INSTALL-OK | sudo tee /dev/ttyS0\n")
            check(m.wait_serial("INSTALL-OK", 1200), "skarlet-install installs to the disk")
            m.shot("09-installed")
    finally:
        m.close()

    if args.install and failures == 0:
        d = Machine(args, boot_disk=True)
        try:
            img = wait_screen(d, looks_like_login, "10-disk-login", 600)
            check(img is not None, "the installed system starts from the disk")
            if img is not None:
                d.type("newpass\n")
                check(wait_screen(d, looks_like_desktop, "11-disk-desktop", 60) is not None,
                      "the new password opens the installed desktop")
        finally:
            d.close()
    print("%d failures" % failures)
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
