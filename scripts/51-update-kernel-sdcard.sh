#!/usr/bin/env bash
# Exper Easypad 8 — SD Card Kernel, U-Boot & DTB Update Script Only
# This script does not reformat the card.
# Usage: sudo bash scripts/51-update-kernel-sdcard.sh /dev/sdX
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PRJ="$(cd "$SCRIPT_DIR/.." && pwd)"

[ -n "${1:-}" ] || { echo "Usage: sudo $0 /dev/sdX"; exit 1; }
DEV="$1"

[ -b "$DEV" ] || { echo "ERROR: $DEV is not a valid block device!"; exit 1; }
case "$DEV" in *[0-9]) echo "ERROR: Specify the base disk, not a partition number (e.g. /dev/sdc)"; exit 1;; esac

P1="${DEV}1"
P2="${DEV}2"
[ -b "$P1" ] || P1="${DEV}p1"
[ -b "$P2" ] || P2="${DEV}p2"

[ -b "$P1" ] || { echo "ERROR: Partition 1 ($P1) not found!"; exit 1; }

MKIMAGE=""
ROOT_PRJ="$(cd "$PRJ/.." && pwd)"
if command -v mkimage >/dev/null 2>&1; then
    MKIMAGE="mkimage"
elif [ -x "$PRJ/uboot/mkimage" ]; then
    MKIMAGE="$PRJ/uboot/mkimage"
elif [ -x "$ROOT_PRJ/piranha-a10/uboot/u-boot/tools/mkimage" ]; then
    MKIMAGE="$ROOT_PRJ/piranha-a10/uboot/u-boot/tools/mkimage"
fi
[ -n "$MKIMAGE" ] && [ -x "$MKIMAGE" ] || { echo "ERROR: mkimage tool not found!"; exit 1; }

# 1. Write New U-Boot SPL (Non-Secure/HYP + PSCI active, see Finding 3)
echo "==> Writing Non-Secure/PSCI U-Boot (Sector 8)..."
dd if="$PRJ/uboot/u-boot-sunxi-with-spl.bin" of="$DEV" bs=1k seek=8 conv=fsync

# 2. Boot Partition Files
echo "==> Mounting boot partition ($P1)..."
MNT_BOOT=$(mktemp -d)
mount "$P1" "$MNT_BOOT"

echo "==> Copying new zImage, zImage.dtb and DTB..."
cp "$PRJ/kernel/zImage" "$MNT_BOOT/zImage"
cp "$PRJ/kernel/zImage.dtb" "$MNT_BOOT/zImage.dtb"
cp "$PRJ/kernel/sun6i-a31s-exper-t8e.dtb" "$MNT_BOOT/sun6i-a31s-exper-t8e.dtb"

cat > "$MNT_BOOT/boot.cmd" <<'EOF'
echo ">>> Exper Easypad 8 LowMem Bootloader <<<"
setenv fdt_high 0x50000000
setenv initrd_high 0x50000000
setenv bootm_size 0x10000000
setenv bootm_boot_mode nonsec
setenv bootargs console=tty0 console=ttyS0,115200 earlycon=uart8250,mmio32,0x01c28000 root=/dev/mmcblk0p2 rootwait rw nosmp panic=0 fbcon=map:0 loglevel=8 ignore_loglevel consoleblank=0 debug initcall_debug
echo ">>> Loading DTB..."
load mmc 0:1 0x43000000 sun6i-a31s-exper-t8e.dtb
fdt addr 0x43000000
fdt resize 65536
echo ">>> Loading zImage..."
load mmc 0:1 0x42000000 zImage
echo ">>> Framebuffer and Chosen status:"
fdt print /chosen
echo ">>> Starting Linux..."
bootz 0x42000000 - 0x43000000
EOF

"$MKIMAGE" -C none -A arm -T script -d "$MNT_BOOT/boot.cmd" "$MNT_BOOT/boot.scr"

sync
umount "$MNT_BOOT"
rmdir "$MNT_BOOT"

# 3. Rootfs Diagnostic Service and Modules
if [ -b "$P2" ]; then
    echo "==> Updating rootfs ($P2)..."
    MNT_ROOT=$(mktemp -d)
    mount "$P2" "$MNT_ROOT"
    if [ -d "$PRJ/kernel/modules" ]; then
        cp -a "$PRJ/kernel/modules/"* "$MNT_ROOT/lib/modules/" 2>/dev/null || true
    fi

    # Add automatic dmesg and boot log service
    mkdir -p "$MNT_ROOT/etc/local.d"
    cat > "$MNT_ROOT/etc/local.d/boot_diag.start" <<'EOF'
#!/bin/sh
mkdir -p /mnt/boot 2>/dev/null || true
mount -t vfat /dev/mmcblk0p1 /mnt/boot 2>/dev/null || true
# Since /boot is mounted "ro" from fstab, /mnt/boot which shares the same
# superblock also remains ro; files cannot be written silently (no error is printed).
# After remounting rw, the kernel reverts with errors=remount-ro if necessary.
mount -o remount,rw /mnt/boot 2>/dev/null || true
echo "BOOT_SUCCESS $(date)" > /mnt/boot/BOOT_SUCCESS.txt
dmesg > /mnt/boot/dmesg_boot.txt 2>/dev/null || true
sync
EOF
    chmod +x "$MNT_ROOT/etc/local.d/boot_diag.start"

    # Add `local` service to default runlevel — otherwise boot_diag.start
    # never runs (openrc: runlevel = /etc/runlevels/<lvl>/<svc> symlink).
    # If the service is enabled, BOOT_SUCCESS.txt + dmesg_boot.txt are written
    # to the FAT partition on each boot; dmesg can be read even without a serial port connected.
    mkdir -p "$MNT_ROOT/etc/runlevels/default"
    ln -sf /etc/init.d/local "$MNT_ROOT/etc/runlevels/default/local"

    sync
    umount "$MNT_ROOT"
    rmdir "$MNT_ROOT"
fi

echo "----------------------------------------------------"
echo "==> SUCCESS! New U-Boot (Non-Secure/PSCI), Kernel and DTB written."
echo "You can insert the card into the tablet and power it on."
echo "----------------------------------------------------"
