<img src="https://raw.githubusercontent.com/newmangarry323-sketch/Ai-Slop-that-Claude-Makes/main/skarletos/docs/logo/skarletos-logo-128.png" alt="SkarletOS logo" width="96" align="right">

**SkarletOS, Debian edition** is the SkarletOS desktop running on top of
**Debian 13 ("trixie")** with a real Linux kernel, so it can install and run
Linux software, both terminal programs and graphical ones, with **apt**,
Debian's package manager. It is a live ISO: start it in VMware (or any PC or
virtual machine), try it, and install it to the disk if you want to keep
what you install.

### Download

**`skarletos-linux.iso`** below. SHA-256 is in `SHA256SUMS-linux`. Check it
on Windows with `certutil -hashfile skarletos-linux.iso SHA256`.

### Run it in VMware

1. *File → New Virtual Machine*, Typical, **Installer disc image file
   (iso)**: pick `skarletos-linux.iso`.
2. Guest operating system: **Linux**, version **Debian 13.x 64-bit** (or
   "Other Linux 6.x kernel 64-bit" if your VMware is older).
3. Give it **2 GB of memory or more** and a disk of **20 GB** (needed only if
   you install it).
4. BIOS or UEFI both work; with UEFI, turn **Secure Boot off**.
5. Power on. The SkarletOS login screen appears; the live system's password
   is **`skarlet`**.

### Installing software

Open **Skarlet Terminal** (Alt+F1, then Enter) and use apt:

```
sudo apt update               # refresh the list of software (password: skarlet)
apt search WORD               # find programs
sudo apt install firefox-esr  # install one; gimp, vlc, libreoffice... work the same way
sudo apt remove NAME          # uninstall
```

Programs you install appear in the launcher (Alt+F1) by themselves, sorted
into categories, and their windows get SkarletOS window frames, panel
buttons, Alt+Tab, minimise, maximise and full screen.

The live system keeps everything in memory, so it is gone after a
restart. To keep it, install SkarletOS onto the virtual disk:

```
sudo skarlet-install
```

It asks which disk (everything on it is erased) and a new password,
copies the running system, **including the programs you installed**, and
sets up booting for both BIOS and UEFI. Then shut down, remove the ISO from
the virtual CD drive and start again.

### What's in it

* Debian 13 with the Linux kernel, systemd, apt, sudo, the X server,
  networking (DHCP), open-vm-tools for VMware, and a few basics (nano,
  htop, curl, man pages).
* The SkarletOS desktop as the window manager: floating panel, launcher,
  runner, widgets, activities, notifications, dark and light themes with
  the maroon accent, at 1918 × 1075 where the graphics allow it.
* Skarlet Terminal is a real terminal (bash, colours, full-screen programs
  like nano and htop). Skarlet Files and Write use the real disk. Skarlet
  Monitor shows the real processes. The login screen checks your real
  password. Settings and widgets are remembered.

### How it was tested

Every build runs on GitHub's servers (the log of each run is public in the
repository's Actions tab):

* the ISO boots in QEMU with **BIOS** to the SkarletOS login screen; a
  wrong password is refused and `skarlet` logs in;
* in Skarlet Terminal, `sudo apt-get install x11-apps` downloads and installs
  from Debian's servers, and **xeyes** opens a window on the desktop;
* `sudo apt-get install firefox-esr` installs Firefox, which then appears in
  the launcher and **starts from it**;
* `skarlet-install` installs the system to an empty virtual disk; the
  machine then **starts from that disk** and the new password logs in;
* the ISO also boots with **UEFI** firmware to the login screen.

**Not tested:** VMware itself (QEMU stands in for it), VirtualBox and real
PCs. In QEMU the screen was 1912 × 1075, not 1918 × 1075: that virtual
graphics card needs a width divisible by 8. VMware's graphics should give
the exact size, but that is an expectation, not a tested result.

### Known limits

* The SkarletOS desktop draws its own windows' frames, but the programs
  inside are ordinary Linux programs with their own look (GTK, Qt...).
* Copy and paste between programs works as in any X11 desktop, but
  SkarletOS's own apps (Files, Write) do not take part in it yet.
* Changing the VMware window size does not resize the desktop yet (pick
  the size in VMware's display settings or use full screen).
* There is no graphical software store: installing is done with apt in the
  terminal.

### Source and licences

* Source: [`skarletos/`](https://github.com/newmangarry323-sketch/Ai-Slop-that-Claude-Makes/tree/main/skarletos)
  (`linux/` is the desktop session, `debian/` builds this ISO).
* Debian is free software under the licences listed in each package
  (`/usr/share/doc/*/copyright` on the system). The ISO contains the GRUB
  boot loader (GPL-3.0-or-later). The terminal uses libvterm (MIT,
  `skarletos/linux/libvterm/LICENSE`). Fonts: DejaVu (Bitstream Vera
  licence).

### Sources

* Debian 13 "trixie": https://www.debian.org/releases/trixie/
* Debian Live / live-boot: https://wiki.debian.org/DebianLive
* mmdebstrap (builds the system): https://gitlab.mister-muffin.de/josch/mmdebstrap
* freedesktop.org Desktop Entry Specification (how installed programs are
  found): https://specifications.freedesktop.org/desktop-entry-spec/latest/
* Extended Window Manager Hints (EWMH) and the ICCCM (what a window manager
  must do): https://specifications.freedesktop.org/wm-spec/latest/ and
  https://x.org/releases/X11R7.6/doc/xorg-docs/specs/ICCCM/icccm.html
* libvterm: https://github.com/neovim/libvterm
