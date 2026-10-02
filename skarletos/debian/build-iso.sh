#!/bin/sh
# build-iso.sh - build the SkarletOS Linux ISO: Debian 13 ("trixie") with
# apt, a Linux kernel, the X server, and SkarletOS as the desktop.
#
# Run as root on Debian trixie (CI uses a "debian:trixie" container started
# with --privileged, because building a system needs chroot and mounts):
#
#     skarletos/debian/build-iso.sh OUTPUT.iso
#
# Steps:
#   1. build skarlet-session from this source tree
#   2. mmdebstrap: a Debian system in a folder, from the Debian mirror
#   3. configure it (debian/overlay and debian/setup-chroot.sh)
#   4. squash it into one compressed file (squashfs) that the "live-boot"
#      initramfs mounts at start-up, with a RAM overlay on top
#   5. grub-mkrescue: an ISO that boots with BIOS and UEFI
set -eu

OUT=$(realpath -m "${1:-skarletos-linux.iso}")
HERE=$(cd "$(dirname "$0")" && pwd)
SRC=$(cd "$HERE/.." && pwd)
WORK=${WORK:-/tmp/skarlet-build}
SUITE=trixie
MIRROR=${MIRROR:-http://deb.debian.org/debian}

echo "== tools"
apt-get update -q
DEBIAN_FRONTEND=noninteractive apt-get install -yq --no-install-recommends \
    mmdebstrap squashfs-tools xorriso grub-pc-bin grub-efi-amd64-bin grub-common mtools \
    dosfstools ca-certificates build-essential pkg-config libx11-dev libxext-dev \
    libpam0g-dev

echo "== skarlet-session"
make -C "$SRC" clean >/dev/null
make -C "$SRC" session

echo "== the Debian system"
rm -rf "$WORK"
mkdir -p "$WORK/iso/live" "$WORK/iso/boot/grub"
# The packages, by purpose.  "Recommends" are left out to keep it small.
PKGS="linux-image-amd64 live-boot systemd-sysv systemd-resolved libpam-systemd dbus udev
      sudo apt ca-certificates tzdata locales kbd console-setup bash-completion less nano
      curl wget procps psmisc htop file man-db iproute2 iputils-ping pciutils
      xserver-xorg-core xserver-xorg-input-libinput xserver-xorg-video-vmware
      xserver-xorg-video-fbdev xinit xauth x11-xserver-utils fonts-dejavu-core
      open-vm-tools libx11-6 libxext6 libpam0g
      grub-pc-bin grub-efi-amd64-bin grub2-common efibootmgr e2fsprogs dosfstools fdisk
      rsync"
# The package lists are kept (--skip), so "apt install" works at once.
# copy-in and sync-in keep each file's owner from the build machine, so the
# hook after them gives the files back to root (sudo ignores a sudoers.d it
# doesn't trust, and system files owned by a user are a security hole).
mmdebstrap --variant=minbase --components=main,contrib,non-free-firmware \
    --skip=cleanup/apt/lists \
    --include="$(echo $PKGS | tr ' ' ',')" \
    --customize-hook='mkdir -p "$1/usr/local/bin"' \
    --customize-hook="copy-in $SRC/build/skarlet-session /usr/local/bin/" \
    --customize-hook="sync-in $HERE/overlay /" \
    --customize-hook="cd '$HERE/overlay' && find . -print0 | (cd \"\$1\" && xargs -0 chown -h 0:0) &&
        chown 0:0 \"\$1/usr/local/bin/skarlet-session\" &&
        chmod 0440 \"\$1/etc/sudoers.d/skarletos\"" \
    --customize-hook="copy-in $HERE/setup-chroot.sh /tmp/" \
    --customize-hook='chroot "$1" sh /tmp/setup-chroot.sh' \
    --customize-hook='rm -f "$1/tmp/setup-chroot.sh"' \
    "$SUITE" "$WORK/rootfs" "$MIRROR"

echo "== kernel, initramfs, squashfs"
cp "$WORK"/rootfs/boot/vmlinuz-* "$WORK/iso/live/vmlinuz"
cp "$WORK"/rootfs/boot/initrd.img-* "$WORK/iso/live/initrd.img"
mksquashfs "$WORK/rootfs" "$WORK/iso/live/filesystem.squashfs" -comp xz -noappend \
    -e boot/lost+found
cp "$HERE/grub.cfg" "$WORK/iso/boot/grub/grub.cfg"
cp "$SRC/README.md" "$WORK/iso/README.md"

echo "== ISO"
grub-mkrescue -o "$OUT" "$WORK/iso" -- -volid SKARLETOS
ls -la "$OUT"
