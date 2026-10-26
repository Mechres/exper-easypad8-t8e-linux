#!/usr/bin/env bash
# Exper Easypad 8 (Allwinner A31s) — Alpine Linux armv7 Rootfs Preparation Script
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
source "$SCRIPT_DIR/00-env.sh"

ALP_REL=3.20
RFS="$PRJ/rootfs/alpine"
CTR=exper-alpine-builder

command -v podman >/dev/null || { echo "ERROR: podman is required"; exit 1; }
ls /proc/sys/fs/binfmt_misc/qemu-arm >/dev/null 2>&1 || {
    echo "ERROR: qemu-arm binfmt is not registered."; exit 1;
}

echo "==> 1. Cleanup and preparation..."
podman rm -f "$CTR" 2>/dev/null || true
rm -rf "$RFS" 2>/dev/null || sudo rm -rf "$RFS"
mkdir -p "$RFS"

echo "==> 2. Starting base alpine:$ALP_REL (arm/v7) container..."
podman run --name "$CTR" --platform linux/arm/v7 --network=host -d \
  "docker.io/library/alpine:$ALP_REL" sleep infinity

echo "==> 3. Installing repositories and packages..."
podman exec "$CTR" /bin/sh -c '
cat > /etc/apk/repositories <<EOF
http://dl-cdn.alpinelinux.org/alpine/v3.20/main
http://dl-cdn.alpinelinux.org/alpine/v3.20/community
http://uk.alpinelinux.org/alpine/v3.20/main
http://uk.alpinelinux.org/alpine/v3.20/community
EOF

apk update || true
apk add --no-cache \
  openrc alpine-base openssh sudo ca-certificates \
  wpa_supplicant wireless-tools iw \
  iproute2 net-tools eudev \
  xorg-server xf86-video-fbdev xf86-input-libinput \
  xinit jwm xterm pcmanfm \
  libinput evtest \
  font-terminus tzdata kbd \
  util-linux pciutils usbutils nano less htop alsa-utils || true
'

echo "==> 4. User, password and permissions (exper / exper)..."
podman exec "$CTR" /bin/sh -c '
set -e
echo root:exper | chpasswd
id exper >/dev/null 2>&1 || adduser -D -s /bin/sh exper
echo exper:exper | chpasswd
for g in wheel video input audio dialout netdev; do
    getent group "$g" >/dev/null || addgroup "$g"
    addgroup exper "$g" 2>/dev/null || true
done
mkdir -p /etc/sudoers.d
echo "%wheel ALL=(ALL) ALL" > /etc/sudoers.d/wheel
chmod 440 /etc/sudoers.d/wheel
'

echo "==> 5. Services and Boot Configuration..."
podman exec "$CTR" /bin/sh -c '
set -e
# inittab console (tty0 and ttyS0)
sed -i "s|^tty1:.*|tty0::respawn:/sbin/getty 38400 tty0|" /etc/inittab
echo "ttyS0::respawn:/sbin/getty -L ttyS0 115200 vt100" >> /etc/inittab

# OpenSSH privilege-separation directory. During podman export/import,
# ownership can emerge as "exper:exper"; sshd strictly rejects this
# and exits cleanly without error:
#   " /var/empty must be owned by root and not group or world-writable."
#   " * ERROR: sshd failed to start"
mkdir -p /var/empty && chown root:root /var/empty && chmod 755 /var/empty

# SSH: allow root user to log in with password. Since OpenSSH default is
# "PermitRootLogin prohibit-password", login with password is rejected
# ("Permission denied, please try again"). Since serial console is a different
# path, this is not seen there and is only encountered over SSH.
mkdir -p /etc/ssh/sshd_config.d
printf "PermitRootLogin yes\nPasswordAuthentication yes\n" \
    > /etc/ssh/sshd_config.d/10-rootpw.conf

# loopback interface — otherwise "networking" does nothing and "lo" remains "DOWN"
cat > /etc/network/interfaces <<IFEOF
auto lo
iface lo inet loopback
IFEOF

# USB Ethernet service
cat > /etc/init.d/net.usb0 <<"EOF"
#!/sbin/openrc-run
description="USB Ethernet gadget (usb0)"
depend() {
    need localmount
    after modules bootmisc
}
start() {
    ebegin "Starting usb0 network"
    ip link set usb0 up 2>/dev/null || true
    ip addr replace 172.16.42.1/24 dev usb0 2>/dev/null || true
    eend 0
}
stop() {
    ebegin "Stopping usb0"
    ip addr flush dev usb0 2>/dev/null || true
    ip link set usb0 down 2>/dev/null || true
    eend 0
}
EOF
chmod +x /etc/init.d/net.usb0

# OpenRC runlevels
rc-update add devfs sysinit || true
rc-update add dmesg sysinit || true
rc-update add udev sysinit || true
rc-update add udev-trigger sysinit || true
rc-update add bootmisc boot || true
rc-update add hostname boot || true
rc-update add modules boot || true
rc-update add networking boot || true
rc-update add localmount boot || true
rc-update add sshd default || true
rc-update add net.usb0 default || true
'

echo "==> 6. Extracting container to rootfs directory..."
podman export "$CTR" | tar -x -C "$RFS"
podman rm -f "$CTR"

echo "==> 7. Adding hostname, hosts, fstab and modules..."
echo exper-t8e > "$RFS/etc/hostname"
cat > "$RFS/etc/hosts" <<'EOF'
127.0.0.1	localhost
127.0.1.1	exper-t8e
::1		localhost ip6-localhost ip6-loopback
EOF

cat > "$RFS/etc/fstab" <<'EOF'
/dev/mmcblk0p1	/boot	vfat	ro,defaults	0 2
/dev/mmcblk0p2	/	ext4	defaults,noatime	0 1
EOF

cat > "$RFS/etc/resolv.conf" <<'EOF'
nameserver 1.1.1.1
nameserver 8.8.8.8
EOF

mkdir -p "$RFS/etc/modules-load.d"
cat > "$RFS/etc/modules-load.d/exper.conf" <<'EOF'
g_ether
goodix
brcmfmac
EOF

# sun4i-drm needs its component drivers loaded (sun4i-drm itself only binds once backend, DRC and
# TCON are present); the DRM DTB (sun6i-a31s-exper-t8e-drm.dtb) is the default boot DTB (Finding 47).
cat > "$RFS/etc/modules-load.d/exper-drm.conf" <<'EOF'
sun4i-backend
sun6i-drc
sun4i-drm
EOF

# Copy built kernel modules
echo "==> Copying kernel modules..."
mkdir -p "$RFS/lib/modules"
cp -r "$PRJ/kernel/modules/"* "$RFS/lib/modules/" 2>/dev/null || true

# Copy Wi-Fi firmware
echo "==> Copying Broadcom AP6210 Wi-Fi firmware..."
mkdir -p "$RFS/lib/firmware/brcm"
cp "$PRJ/kernel/firmware/fw_bcm40181a2.bin" "$RFS/lib/firmware/brcm/brcmfmac43362-sdio.bin"
cp "$PRJ/kernel/firmware/nvram_ap6181.txt" "$RFS/lib/firmware/brcm/brcmfmac43362-sdio.txt"
cp "$PRJ/kernel/firmware/fw_bcm40181a2.bin" "$RFS/lib/firmware/brcm/brcmfmac40181a2-sdio.bin"
cp "$PRJ/kernel/firmware/nvram_ap6181.txt" "$RFS/lib/firmware/brcm/brcmfmac40181a2-sdio.txt"

# X11 / JWM configuration (exper user)
mkdir -p "$RFS/home/exper"
cat > "$RFS/home/exper/.xinitrc" <<'EOF'
#!/bin/sh
xset s off -dpms
exec jwm
EOF
chmod +x "$RFS/home/exper/.xinitrc"

echo "==> Alpine rootfs successfully prepared: $RFS"
ls -lh "$RFS"
