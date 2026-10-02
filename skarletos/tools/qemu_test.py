#!/usr/bin/env python3
"""qemu_test.py - boot build/skarletos.iso in QEMU, press keys, take screenshots.

This is the closest we can get to "try it in VMware" automatically: QEMU's
"-vga vmware" adapter implements the same VMware SVGA II interface that
SkarletOS programs, and "-vga std" exercises the Bochs/QEMU fallback.

Usage:
  qemu_test.py [--qemu qemu-system-x86_64] [--vga vmware|std] [--uefi CODE.fd]
               [--out DIR] [--tour] ISO

It talks to QEMU's "monitor" (a text command interface) over a Unix socket:
"sendkey" presses keys and "screendump" saves the screen as a PPM image.
Exits non-zero if the screen does not look like SkarletOS.
"""
import argparse
import os
import shutil
import socket
import subprocess
import sys
import tempfile
import time


class Monitor:
    def __init__(self, path):
        self.sock = socket.socket(socket.AF_UNIX)
        for _ in range(100):
            try:
                self.sock.connect(path)
                break
            except OSError:
                time.sleep(0.1)
        else:
            sys.exit("could not connect to the QEMU monitor")
        self.read_prompt()

    def read_prompt(self, need_newline=False):
        buf = b""
        while not (buf.endswith(b"(qemu) ") and (b"\r\n" in buf or not need_newline)):
            chunk = self.sock.recv(4096)
            if not chunk:
                break
            buf += chunk
        return buf.decode(errors="replace")

    def cmd(self, line):
        # Throw away anything QEMU printed on its own (for example an extra
        # prompt when the guest powers off), so this reply is really ours.
        self.sock.setblocking(False)
        try:
            while self.sock.recv(4096):
                pass
        except BlockingIOError:
            pass
        self.sock.setblocking(True)
        self.sock.sendall(line.encode() + b"\n")
        # The reply is the echoed command, a line break once QEMU has run
        # it, any output, and the next prompt.
        return self.read_prompt(need_newline=True)


def read_ppm(path):
    with open(path, "rb") as f:
        data = f.read()
    # Header: "P6\n<w> <h>\n255\n" (QEMU writes no comments).
    parts = data.split(b"\n", 3)
    w, h = map(int, parts[1].split())
    px = parts[3]
    # QEMU's screendump writes each row padded to a multiple of 4 bytes
    # (ppm_save() writes the line buffer's stride), which is not valid PPM
    # when width * 3 is not a multiple of 4 -- as with 1918 pixels.  Remove
    # the padding and save a correct file.
    stride = (w * 3 + 3) & ~3
    if stride != w * 3 and len(px) == stride * h:
        px = b"".join(px[y * stride:y * stride + w * 3] for y in range(h))
        with open(path, "wb") as f:
            f.write(b"P6\n%d %d\n255\n" % (w, h) + px)
    return w, h, px


def pixel(img, x, y):
    w, h, px = img
    i = (y * w + x) * 3
    return px[i], px[i + 1], px[i + 2]


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("iso")
    ap.add_argument("--qemu", default="qemu-system-x86_64")
    ap.add_argument("--vga", default="vmware")
    ap.add_argument("--uefi", help="UEFI firmware (e.g. QEMU's edk2-x86_64-code.fd)")
    ap.add_argument("--out", default="build/qemu")
    ap.add_argument("--tour", action="store_true", help="also open apps and take more screenshots")
    ap.add_argument("--boot-wait", type=float, default=20)
    ap.add_argument("--extra", default="", help="more QEMU options, e.g. '-device bochs-display'")
    args = ap.parse_args()

    os.makedirs(args.out, exist_ok=True)
    tmp = tempfile.mkdtemp()
    sock = os.path.join(tmp, "mon")
    cmd = [args.qemu, "-cdrom", args.iso, "-m", "512M", "-vga", args.vga, "-display", "none",
           "-monitor", "unix:%s,server,nowait" % sock, "-no-reboot"]
    cmd += args.extra.split()
    if args.uefi:
        cmd += ["-drive", "if=pflash,format=raw,readonly=on,file=" + args.uefi]
    print(" ".join(cmd))
    qemu = subprocess.Popen(cmd)
    failures = 0
    try:
        mon = Monitor(sock)
        shots = []

        def shot(name, wait=0):
            time.sleep(wait)
            path = os.path.join(args.out, name + ".ppm")
            mon.cmd("screendump " + path)
            time.sleep(0.5)
            shots.append(path)
            return read_ppm(path)

        def keys(*names, gap=0.6):
            for k in names:
                mon.cmd("sendkey " + k)
                time.sleep(gap)

        def check(cond, what):
            nonlocal failures
            print(("ok   " if cond else "FAIL ") + what)
            failures += not cond

        login = shot("01-login", args.boot_wait)
        w, h, _ = login
        print("screen: %dx%d" % (w, h))
        if args.vga == "vmware" and not args.extra:
            check((w, h) == (1918, 1075), "VMware SVGA mode is exactly 1918x1075")
        else:
            check(w >= 1024 and h >= 768, "a graphics mode is set")
        # The login screen is dark, with a white clock in the middle.
        bright = sum(1 for y in range(h // 4, h // 2, 4) for x in range(w // 3, 2 * w // 3, 4)
                     if sum(pixel(login, x, y)) > 600)
        check(bright > 50, "login clock is drawn (%d bright samples)" % bright)

        keys("ret", gap=3)
        desk = shot("02-desktop", 1)
        # The launcher button: a maroon circle at the left end of the panel
        # (PANEL_MARGIN + 8 + 20 = 36 px from the left, panel centre 32 px
        # above the bottom edge of the desktop).
        ox, oy = (w - min(w, 1918)) // 2, (h - min(h, 1075)) // 2
        dh = min(h, 1075)
        r, g, b = pixel(desk, ox + 36, oy + dh - 8 - 24 + 12)
        check(r > 100 and g < 40 and b < 40, "maroon launcher button on the panel (%d,%d,%d)" % (r, g, b))

        keys("alt-f1", gap=3)
        shot("03-launcher", 1)
        keys("esc", gap=2)
        if args.tour:
            for name, seq in (("04-terminal", ["alt-f1", "ret"]),
                              ("05-files", ["alt-f2", "f", "i", "l", "e", "s", "ret"]),
                              ("06-runner", ["alt-f2", "6", "shift-8", "7"])):
                keys(*seq, gap=1.5)
                shot(name, 3)
                if name == "06-runner":
                    keys("esc")
        for p in shots:
            if shutil.which("convert"):
                subprocess.run(["convert", p, p[:-4] + ".png"], check=False)
    finally:
        qemu.kill()
        qemu.wait()
        shutil.rmtree(tmp, ignore_errors=True)
    print("%d failures" % failures)
    sys.exit(1 if failures else 0)


if __name__ == "__main__":
    main()
