#!/usr/bin/env python3
"""mkiso.py - build a bootable CD image (ISO 9660 + El Torito), no extra tools.

Usage:  mkiso.py OUT.iso ROOT_DIR BIOS_BOOT_IMAGE UEFI_BOOT_IMAGE

ROOT_DIR is copied into the image.  The two boot images are paths *inside*
ROOT_DIR (for example boot/limine/limine-bios-cd.bin).  The image boots on:
  - BIOS machines: the BIOS loads the first 2 KiB of the BIOS image ("no
    emulation" El Torito boot) after we patch a "boot info table" into it,
    which tells it where on the disc it is;
  - UEFI machines: the firmware opens the UEFI image as a small FAT disk and
    runs EFI/BOOT/BOOTX64.EFI from it.

The format, briefly (sectors are 2048 bytes):
  0-15   unused "system area"
  16     Primary Volume Descriptor: names the disc, points at the root dir
  17     Boot Record: points at the El Torito boot catalog
  18     Volume Descriptor Set Terminator
  19     boot catalog: one BIOS entry and one UEFI entry
  then   path tables, directories and file contents
References: ECMA-119 (the ISO 9660 standard) and the "El Torito" Bootable
CD-ROM Format Specification 1.0 by Phoenix Technologies and IBM.

Names are stored in plain ISO 9660 form (upper case, "NAME.EXT;1"); we write
no Rock Ridge or Joliet extensions, so file names on the disc look upper-case.
"""
import os
import struct
import sys
import time

SECTOR = 2048


def both16(v):
    return struct.pack("<H", v) + struct.pack(">H", v)


def both32(v):
    return struct.pack("<I", v) + struct.pack(">I", v)


def pad(data, size=SECTOR):
    return data + b"\0" * (-len(data) % size)


def iso_name(name, is_dir):
    n = name.upper()
    return n if is_dir else (n if "." in n else n + ".") + ";1"


class Node:
    def __init__(self, path, name, is_dir):
        self.path, self.name, self.is_dir = path, name, is_dir
        self.children = []
        self.parent = None
        self.lba = 0
        self.size = 0
        self.number = 0  # position in the path table (directories only)


def scan(path, name=""):
    node = Node(path, name, os.path.isdir(path))
    if node.is_dir:
        for entry in sorted(os.listdir(path), key=lambda e: iso_name(e, os.path.isdir(os.path.join(path, e)))):
            child = scan(os.path.join(path, entry), entry)
            child.parent = node
            node.children.append(child)
    else:
        node.size = os.path.getsize(path)
    return node


def dir_date(t):
    tm = time.gmtime(t)
    return bytes([tm.tm_year - 1900, tm.tm_mon, tm.tm_mday, tm.tm_hour, tm.tm_min, tm.tm_sec, 0])


def vd_date(t):
    return time.strftime("%Y%m%d%H%M%S", time.gmtime(t)).encode() + b"00\0"


def record(node, ident, stamp):
    """One directory record describing node, named ident (bytes)."""
    length = 33 + len(ident)
    length += length % 2
    rec = bytes([length, 0]) + both32(node.lba) + both32(node.size) + dir_date(stamp)
    rec += bytes([2 if node.is_dir else 0, 0, 0]) + both16(1) + bytes([len(ident)]) + ident
    return pad(rec, 2) if len(rec) % 2 else rec


def dir_records(node, stamp):
    """The records of a directory, never letting one cross a sector boundary."""
    recs = [record(node, b"\0", stamp), record(node.parent or node, b"\1", stamp)]
    recs += [record(c, iso_name(c.name, c.is_dir).encode(), stamp) for c in node.children]
    out = b""
    for r in recs:
        if len(out) // SECTOR != (len(out) + len(r) - 1) // SECTOR:
            out = pad(out)
        out += r
    return pad(out)


def main():
    if len(sys.argv) != 5:
        sys.exit(__doc__)
    out, root_dir, bios_rel, uefi_rel = sys.argv[1:]
    stamp = int(os.environ.get("SOURCE_DATE_EPOCH", time.time()))
    root = scan(root_dir)

    # Directories in path-table order: breadth first, by parent then name.
    dirs = [root]
    for d in dirs:
        dirs += [c for c in d.children if c.is_dir]
    for i, d in enumerate(dirs):
        d.number = i + 1

    def path_table(big_endian):
        fmt = ">" if big_endian else "<"
        t = b""
        for d in dirs:
            ident = iso_name(d.name, True).encode() if d is not root else b"\0"
            t += bytes([len(ident), 0]) + struct.pack(fmt + "IH", d.lba, (d.parent or d).number)
            t += ident + (b"\0" if len(ident) % 2 else b"")
        return t

    # Lay out the disc.  Sizes of path tables and directories do not depend
    # on the LBAs inside them, so one pass with dummy LBAs gives the sizes.
    lba = 20  # after the descriptors (16-18) and the boot catalog (19)
    pt_size = len(path_table(False))
    pt_sectors = (pt_size + SECTOR - 1) // SECTOR
    l_table, m_table = lba, lba + pt_sectors
    lba += 2 * pt_sectors
    for d in dirs:
        d.size = len(dir_records(d, stamp))
        d.lba = lba
        lba += d.size // SECTOR
    files = []

    def collect(n):
        for c in n.children:
            if c.is_dir:
                collect(c)
            else:
                files.append(c)

    collect(root)
    for f in files:
        f.lba = lba
        lba += max(1, (f.size + SECTOR - 1) // SECTOR)
    # Pad the disc to a whole MiB.  Boot loaders read CDs in big blocks (Limine
    # reads up to 512 sectors at a time) and some BIOSes cannot tell how long
    # the disc is, so a read must never run past the end; mkisofs/xorriso pad
    # their images for the same reason.
    total = (lba + 511) // 512 * 512

    def find(rel):
        n = root
        for part in rel.strip("/").split("/"):
            n = next(c for c in n.children if c.name == part)
        return n

    bios, uefi = find(bios_rel), find(uefi_rel)

    img = bytearray(total * SECTOR)

    # A PC partition table (MBR) in the first 512 bytes, with one "EFI System"
    # partition (type 0xEF) covering the UEFI boot image.  CD booting does
    # not need it, but it lets a UEFI boot loader work out which disc it was
    # started from (Limine matches the firmware's El Torito partition start
    # against the partitions it finds), as xorriso's -efi-boot-part does.
    # Partition table addresses count 512-byte sectors.
    mbr = bytearray(512)
    mbr[0x1B8:0x1BC] = b"SkOS"  # disk signature
    mbr[446:462] = struct.pack("<B3sB3sII", 0x00, b"\xfe\xff\xff", 0xEF, b"\xfe\xff\xff",
                               uefi.lba * 4, (uefi.size + 511) // 512)
    mbr[510:512] = b"\x55\xaa"
    img[0:512] = mbr

    # Primary Volume Descriptor.
    pvd = bytearray(SECTOR)
    pvd[0:8] = b"\1CD001\1\0"
    pvd[8:40] = b" " * 32
    pvd[40:72] = b"SKARLETOS".ljust(32)
    pvd[80:88] = both32(total)
    pvd[120:124] = both16(1)
    pvd[124:128] = both16(1)
    pvd[128:132] = both16(SECTOR)
    pvd[132:140] = both32(pt_size)
    pvd[140:144] = struct.pack("<I", l_table)
    pvd[148:152] = struct.pack(">I", m_table)
    pvd[156:190] = record(root, b"\0", stamp)
    for off, ln in ((190, 128), (318, 128), (446, 128), (574, 128), (702, 37), (739, 37), (776, 37)):
        pvd[off:off + ln] = b" " * ln
    pvd[574:574 + 9] = b"SKARLETOS"
    pvd[813:830] = vd_date(stamp)
    pvd[830:847] = vd_date(stamp)
    pvd[847:864] = b"0" * 16 + b"\0"
    pvd[864:881] = b"0" * 16 + b"\0"
    pvd[881] = 1
    img[16 * SECTOR:17 * SECTOR] = pvd

    # El Torito Boot Record, pointing at the catalog in sector 19.
    br = bytearray(SECTOR)
    br[0:7] = b"\0CD001\1"
    br[7:39] = b"EL TORITO SPECIFICATION".ljust(32, b"\0")
    br[0x47:0x4B] = struct.pack("<I", 19)
    img[17 * SECTOR:18 * SECTOR] = br

    img[18 * SECTOR:18 * SECTOR + 7] = b"\xffCD001\1"

    # Boot catalog: validation entry, default (BIOS) entry, UEFI section.
    val = bytearray(32)
    val[0] = 1  # header ID
    val[1] = 0  # platform: 80x86
    val[4:28] = b"SkarletOS".ljust(24, b"\0")
    val[30:32] = b"\x55\xaa"
    words = struct.unpack("<16H", bytes(val))
    val[28:30] = struct.pack("<H", (-sum(words)) & 0xFFFF)
    # 0x88 = bootable, media 0 = no emulation, 4 x 512-byte sectors loaded.
    bios_entry = struct.pack("<BBHBBHI", 0x88, 0, 0, 0, 0, 4, bios.lba) + bytes(20)
    sect_hdr = struct.pack("<BBH", 0x91, 0xEF, 1) + b"SkarletOS UEFI".ljust(28, b"\0")
    uefi_sectors = min((uefi.size + 511) // 512, 0xFFFF)
    uefi_entry = struct.pack("<BBHBBHI", 0x88, 0, 0, 0, 0, uefi_sectors, uefi.lba) + bytes(20)
    cat = bytes(val) + bios_entry + sect_hdr + uefi_entry
    img[19 * SECTOR:19 * SECTOR + len(cat)] = cat

    for table, start in ((path_table(False), l_table), (path_table(True), m_table)):
        img[start * SECTOR:start * SECTOR + len(table)] = table
    for d in dirs:
        data = dir_records(d, stamp)
        img[d.lba * SECTOR:d.lba * SECTOR + len(data)] = data
    for f in files:
        with open(f.path, "rb") as fh:
            data = bytearray(fh.read())
        if f is bios:
            # Boot info table (El Torito "boot-info-table", as mkisofs writes
            # it): at byte 8, the PVD's LBA, this file's LBA and length, and
            # a 32-bit sum of the file from byte 64 on.
            body = pad(bytes(data[64:]), 4)
            csum = sum(struct.unpack("<%dI" % (len(body) // 4), body)) & 0xFFFFFFFF
            data[8:24] = struct.pack("<IIII", 16, f.lba, f.size, csum)
        img[f.lba * SECTOR:f.lba * SECTOR + len(data)] = data

    with open(out, "wb") as fh:
        fh.write(img)
    print("Wrote %s (%d KiB, %d files)" % (out, len(img) // 1024, len(files)))


if __name__ == "__main__":
    main()
