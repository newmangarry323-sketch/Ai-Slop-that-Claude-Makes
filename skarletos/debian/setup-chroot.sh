#!/bin/sh
# setup-chroot.sh - configure the SkarletOS Debian system.  Runs inside it
# (in a chroot) while debian/build-iso.sh builds the image.
set -eu

echo skarlet > /etc/hostname
cat > /etc/hosts <<'HOSTS'
127.0.0.1   localhost
127.0.1.1   skarlet
::1         localhost ip6-localhost ip6-loopback
HOSTS
echo 'LANG=C.UTF-8' > /etc/default/locale

# The user: "user", password "skarlet" on the live system (skarlet-install
# asks for a new one).  Groups: sudo for administration, the others for
# the screen, sound and input devices.
mkdir -p /etc/skel/Documents /etc/skel/Music /etc/skel/.local/share/applications
groups=sudo
for g in video audio input render plugdev netdev; do
    getent group "$g" >/dev/null && groups="$groups,$g"
done
useradd -m -s /bin/bash -G "$groups" -c "SkarletOS User" user
echo 'user:skarlet' | chpasswd
passwd -l root >/dev/null   # use sudo instead of logging in as root
chmod 0440 /etc/sudoers.d/skarletos
chmod 0755 /usr/local/bin/*

# Software sources: Debian 13 with its updates and security fixes.
rm -f /etc/apt/sources.list
cat > /etc/apt/sources.list.d/debian.sources <<'SOURCES'
Types: deb
URIs: http://deb.debian.org/debian
Suites: trixie trixie-updates
Components: main contrib non-free non-free-firmware
Signed-By: /usr/share/keyrings/debian-archive-keyring.gpg

Types: deb
URIs: http://security.debian.org/debian-security
Suites: trixie-security
Components: main contrib non-free non-free-firmware
Signed-By: /usr/share/keyrings/debian-archive-keyring.gpg
SOURCES
apt-get update -q

# Networking and name resolution.
systemctl enable systemd-networkd systemd-resolved
ln -sf /run/systemd/resolve/stub-resolv.conf /etc/resolv.conf

# A friendlier message of the day on the consoles.
cat > /etc/motd <<'MOTD'

SkarletOS - Debian 13 with the SkarletOS desktop.
The desktop starts on the first console (Ctrl+Alt+F1).

MOTD
apt-get clean
