<img src="https://raw.githubusercontent.com/newmangarry323-sketch/Ai-Slop-that-Claude-Makes/main/skarletos/docs/logo/skarletos-logo-128.png" alt="SkarletOS logo" width="96" align="right">

**SkarletOS** is a small UNIX-like operating system for 64-bit (x86-64) PCs,
written in C. It boots on its own, with no Linux or Windows underneath, and
draws a modern desktop inspired by KDE Plasma at **1918 × 1075**, with a
**maroon** accent colour. It is a learning project: about 6,500 lines of C you
can read, change and rebuild.

![SkarletOS desktop with Skarlet Terminal](https://raw.githubusercontent.com/newmangarry323-sketch/Ai-Slop-that-Claude-Makes/main/skarletos/docs/screenshots/06-terminal.png)

### Download

**`skarletos.iso`** (4 MiB) is a bootable CD image for virtual machines. It
boots with BIOS or UEFI firmware.

SHA-256: `6d568cf789a9c74563bdc66343401a588226dec6c2c122d267b976103cfef550`

Check it on Windows with `certutil -hashfile skarletos.iso SHA256`, or on
Linux/macOS with `sha256sum skarletos.iso`.

### Run it in VMware

1. *File → New Virtual Machine*, Typical. Choose **Installer disc image file
   (iso)** and pick `skarletos.iso`.
2. Guest operating system: **Other → Other 64-bit**.
3. Memory 256 MB or more. Any disk size is fine; SkarletOS never touches the disk.
4. Either firmware works (*VM Settings → Options → Advanced*). With UEFI, leave
   **Secure Boot off**: the boot loader (Limine) isn't signed with the keys
   Secure Boot checks.
5. Power on, type any password at the login screen and press **Enter**.

SkarletOS is keyboard only. **Alt+F1** (or the Windows key) opens the launcher,
**Alt+F2** the runner (apps, places, maths like `6*7`, commands), **Alt+F12**
the desktop menu, **Alt+Tab / Alt+F4** switch and close windows. **Ctrl+Alt**
gives the keyboard back to your computer in VMware.

### What's in it

* **Desktop:** a floating panel (launcher, pager, task buttons, tray, clock),
  rounded translucent windows and popups with soft shadows, desktop widgets
  (Folder View, Notes, Digital Clock, System Monitor, Fifteen Puzzle),
  activities, notifications, a login screen, and dark and light themes with five
  accent colours (Maroon, Blue, Teal, Green, Purple).
* **Apps:** Skarlet Terminal (a UNIX-style shell with `ls`, `cd`, `cat`, `cp`,
  `mv`, `rm`, redirection, `ps`, `kill`, `calc` and more), Skarlet Files,
  Skarlet Write (a text editor), Skarlet Settings and Skarlet Monitor.
* **Kernel:** 32-bit to 64-bit long-mode start-up, PCI scan, its own VMware
  SVGA II and Bochs/QEMU graphics drivers (exact 1918 × 1075 on VMware's
  adapter), a PS/2 keyboard driver, the CMOS clock and an in-memory file
  system. A boot log goes to the first serial port (COM1).

### How it was tested

This exact ISO file was booted in QEMU 9.2.0 with real key presses:

* BIOS (SeaBIOS) and UEFI (edk2), each with QEMU's VMware SVGA adapter:
  1918 × 1075, all checks pass.
* BIOS with QEMU's standard VGA: 1912 × 1075 (that adapter needs a width that
  is a multiple of 8), all checks pass.
* A 21-step tour checked the text on every screen against the kernel's own
  record of what it drew, and ended with the guest powering itself off.
* The desktop code also passes 192 scripted checks and 40,000 random key
  presses under AddressSanitizer.

**Not tested:** VMware itself (it couldn't be run where this was built),
VirtualBox and real PCs. QEMU's VMware adapter implements the same registers
VMware documents, so VMware is expected to work, but that is an expectation,
not a result. If the screen stays blank, add a serial port writing to a file
in the VM's settings: SkarletOS logs which graphics driver it chose there.

### What it does not do

No mouse, no networking, no disk (files are lost on reboot), no processes or
memory protection, no sound. Passwords are not checked. On real PCs,
"Shut down" can't switch the machine off, because that needs ACPI support
SkarletOS doesn't have.

### Source, docs and licences

* Source, build instructions and exercises:
  [`skarletos/README.md`](https://github.com/newmangarry323-sketch/Ai-Slop-that-Claude-Makes/blob/main/skarletos/README.md)
* Screenshot tour, shell examples and two worked examples of extending it:
  [`skarletos/docs/EXAMPLES.md`](https://github.com/newmangarry323-sketch/Ai-Slop-that-Claude-Makes/blob/main/skarletos/docs/EXAMPLES.md)
* The ISO contains the **Limine** boot loader v11.4.1 (BSD 2-Clause; licence
  attached as `LIMINE-LICENSE.txt` and on the disc). The text uses glyphs
  rendered from the **DejaVu** fonts (Bitstream Vera licence; see
  `skarletos/docs/FONT-LICENSE.txt`).

### Sources

* KDE, "KDE MegaRelease 6" (Plasma 6, the style the desktop takes its cues
  from): https://kde.org/announcements/megarelease/6/
* Broadcom (VMware) TechDocs, "Configuring the Firmware Type for a Virtual
  Machine":
  https://techdocs.broadcom.com/us/en/vmware-cis/desktop-hypervisors/workstation-pro/17-0/using-vmware-workstation-pro/configuring-virtual-machine-option-settings/configuring-advanced-options-for-a-virtual-machine/configuring-the-firmware-type-for-a-virtual-machine.html
* VMware's `svga_reg.h`, as shipped in Linux:
  https://github.com/torvalds/linux/blob/master/drivers/gpu/drm/vmwgfx/device_include/svga_reg.h
* Limine: https://github.com/limine-bootloader/limine
