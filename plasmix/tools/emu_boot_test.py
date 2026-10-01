#!/usr/bin/env python3
"""Boot the real Plasmix kernel image in the Unicorn CPU emulator.

This does what a Multiboot boot loader does: copy build/plasmix.bin to
1 MiB, put the Multiboot magic in EAX and an info structure address in EBX,
and start the CPU at the entry point in 32-bit protected mode.  The kernel
then switches the CPU to 64-bit long mode by itself.

The keyboard controller, CMOS clock and power-off ports are simulated with
I/O port hooks, so we can "type" on the PS/2 keyboard and read the VGA text
screen (memory at 0xB8000) back to check what the kernel drew.

Usage: emu_boot_test.py build/plasmix.elf build/plasmix.bin
Needs the 'unicorn' Python package (pip install unicorn).
"""
import os
import struct
import sys

from unicorn import Uc, UcError, UC_ARCH_X86, UC_MODE_32, UC_HOOK_INSN
from unicorn.x86_const import (UC_X86_INS_IN, UC_X86_INS_OUT, UC_X86_REG_EAX,
                               UC_X86_REG_EBX, UC_X86_REG_EIP, UC_X86_REG_CS,
                               UC_X86_REG_CR0, UC_X86_REG_CR4)

LOAD = 0x100000
MB_INFO = 0x9000
VGA = 0xB8000

# Scan code set 1 for the keys the test types.
SCAN = {c: 0x02 + i for i, c in enumerate("1234567890")}
SCAN.update({c: 0x10 + i for i, c in enumerate("qwertyuiop")})
SCAN.update({c: 0x1E + i for i, c in enumerate("asdfghjkl")})
SCAN.update({c: 0x2C + i for i, c in enumerate("zxcvbnm")})
SCAN.update({" ": 0x39, "-": 0x0C, "\n": 0x1C, "/": 0x35, ".": 0x34})
SHIFTED = {"*": 0x09}  # shift + 8
ESC, LALT, LSHIFT, F1, F2 = 0x01, 0x38, 0x2A, 0x3B, 0x3C


class Machine:
    def __init__(self, image):
        self.keys = []
        self.cmos_index = 0
        self.poweroff = False
        self.mu = Uc(UC_ARCH_X86, UC_MODE_32)
        self.mu.mem_map(0, 16 * 1024 * 1024)
        self.mu.mem_write(LOAD, image)
        # Multiboot info: flags bit 0 = mem_lower/mem_upper are valid.
        self.mu.mem_write(MB_INFO, struct.pack("<III", 1, 639, 127 * 1024))
        self.mu.hook_add(UC_HOOK_INSN, self.port_in, None, 1, 0, UC_X86_INS_IN)
        self.mu.hook_add(UC_HOOK_INSN, self.port_out, None, 1, 0, UC_X86_INS_OUT)

    # --- simulated hardware ---------------------------------------------
    def port_in(self, uc, port, size, user):
        if port == 0x64:  # keyboard controller status: bit 0 = data waiting
            return 1 if self.keys else 0
        if port == 0x60:
            return self.keys.pop(0) if self.keys else 0
        if port == 0x71:  # CMOS: 1 August 2012, 10:30:00, BCD, 24-hour
            return {0x00: 0x00, 0x02: 0x30, 0x04: 0x10, 0x07: 0x01, 0x08: 0x08,
                    0x09: 0x12, 0x0A: 0x00, 0x0B: 0x02}.get(self.cmos_index, 0)
        return 0

    def port_out(self, uc, port, size, value, user):
        if port == 0x70:
            self.cmos_index = value & 0x7F
        elif port == 0x604 and value == 0x2000:  # QEMU ACPI power-off
            self.poweroff = True
            uc.emu_stop()

    # --- helpers -------------------------------------------------------------
    def run(self, entry=None, instructions=40_000_000):
        # The emulator was created in 32-bit mode, so the instruction pointer
        # is read as EIP; the kernel lives below 4 GiB, so nothing is lost.
        start = entry if entry is not None else self.mu.reg_read(UC_X86_REG_EIP)
        self.mu.emu_start(start, 0, 0, instructions)

    def screen(self):
        raw = self.mu.mem_read(VGA, 80 * 25 * 2)
        rows = []
        for y in range(25):
            chars = raw[y * 160:(y + 1) * 160:2]
            rows.append("".join(chr(c) if 32 <= c < 127 else " " for c in chars))
        return "\n".join(rows)

    def tap(self, sc):
        self.keys += [sc, sc | 0x80]

    def chord(self, mod, sc):
        self.keys += [mod, sc, sc | 0x80, mod | 0x80]

    def type(self, text):
        for ch in text:
            if ch in SHIFTED:
                self.chord(LSHIFT, SHIFTED[ch])
            else:
                self.tap(SCAN[ch])


def main():
    elf_path, bin_path = sys.argv[1], sys.argv[2]
    image = open(bin_path, "rb").read()
    magic, flags, checksum, _, _, _, _, entry = struct.unpack_from("<8I", image, 0)
    assert magic == 0x1BADB002 and (magic + flags + checksum) & 0xFFFFFFFF == 0
    failures = []

    def check(cond, what, m):
        print(("ok   " if cond else "FAIL ") + what)
        if not cond:
            failures.append(what)
            print(m.screen())

    m = Machine(image)
    m.mu.reg_write(UC_X86_REG_EAX, 0x2BADB002)
    m.mu.reg_write(UC_X86_REG_EBX, MB_INFO)
    try:
        m.run(entry)
    except UcError as e:
        print("CPU fault:", e, "at", hex(m.mu.reg_read(UC_X86_REG_EIP)))
        print(m.screen())
        return 1

    cr0 = m.mu.reg_read(UC_X86_REG_CR0)
    cr4 = m.mu.reg_read(UC_X86_REG_CR4)
    efer = m.mu.msr_read(0xC0000080)
    check(cr0 & 0x80000001 == 0x80000001, "paging and protected mode on (CR0)", m)
    check(cr4 & 0x20 != 0, "PAE on (CR4)", m)
    check(efer & 0x500 == 0x500, "long mode enabled and active (EFER.LME+LMA)", m)
    check(m.mu.reg_read(UC_X86_REG_CS) == 0x08, "running in the 64-bit code segment", m)
    check("Welcome to Plasmix" in m.screen(), "login screen drawn", m)

    m.tap(SCAN["\n"])
    m.run()
    s = m.screen()
    check("Folder View" in s and " K " in s and "10:30" in s, "desktop and panel drawn", m)

    m.chord(LALT, F2)
    m.type("6*7")
    m.run()
    check("= 42" in m.screen(), "KRunner calculates 6*7", m)
    m.tap(ESC)

    m.chord(LALT, F1)
    m.tap(SCAN["\n"])  # first favourite: Konsole
    m.run()
    m.type("uname -a\n")
    m.run()
    check("Plasmix plasmix 0.1 #1 x86_64" in m.screen(), "Konsole runs uname -a", m)
    m.type("ls /\n")
    m.run()
    check("bin/  etc/  home/" in m.screen(), "ls / lists the file system", m)

    os.makedirs("build/screens", exist_ok=True)
    with open("build/screens/emu-konsole.txt", "w") as f:
        f.write(m.screen() + "\n")

    m.chord(LALT, F1)
    m.type("shut\n")
    m.run()
    check(m.poweroff, "Kickoff > Shut down powers off (port 0x604)", m)

    print("%d failures" % len(failures))
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
