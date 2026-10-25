#!/usr/bin/env bash
# Exper Easypad 8 (Allwinner A31s) — MicroSD Card Flasher Script
# *** CAUTION: THIS SCRIPT DELETES ALL DATA ON THE TARGET SD CARD ***
# Usage: sudo bash scripts/50-sdcard-alpine.sh /dev/sdX

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PRJ="$(cd "$SCRIPT_DIR/.." && pwd)"

[ -n "${1:-}" ] || { echo "Usage: sudo $0 /dev/sdX"; exit 1; }
DEV="$1"

ROOT_PRJ="$(cd "$PRJ/.." && pwd)"

UBOOT="$PRJ/uboot/u-boot-sunxi-with-spl.bin"
ZIMG="$PRJ/kernel/zImage"
DTB="$PRJ/kernel/sun6i-a31s-exper-t8e.dtb"
RFS="$PRJ/rootfs/alpine"

MKIMAGE=""
if command -v mkimage >/dev/null 2>&1; then
    MKIMAGE="mkimage"
elif [ -x "$PRJ/uboot/mkimage" ]; then
    MKIMAGE="$PRJ/uboot/mkimage"
elif [ -x "$ROOT_PRJ/piranha-a10/uboot/u-boot/tools/mkimage" ]; then
    MKIMAGE="$ROOT_PRJ/piranha-a10/uboot/u-boot/tools/mkimage"
fi

# 1. Check input files
for f in "$UBOOT" "$ZIMG" "$DTB"; do
    [ -f "$f" ] || { echo "ERROR: Missing file: $f"; exit 1; }
done
[ -n "$MKIMAGE" ] && [ -x "$MKIMAGE" ] || { echo "ERROR: mkimage tool not found!"; exit 1; }
[ -d "$RFS" ] || { echo "ERROR: Rootfs directory not found: $RFS"; exit 1; }

echo "==> Input files verified."

# 2. Safety Checks
[ -b "$DEV" ] || { echo "ERROR: $DEV is not a valid block device!"; exit 1; }
case "$DEV" in *[0-9]) echo "ERROR: Specify the base disk, not a partition number (e.g. /dev/sde)"; exit 1;; esac

RM=$(lsblk -ndo RM "$DEV" || true)
SIZE=$(lsblk -ndo SIZE "$DEV" || true)
MODEL=$(lsblk -ndo MODEL "$DEV" || true)

echo "----------------------------------------------------"
echo "Target Device: $DEV"
echo "Size         : $SIZE"
echo "Model        : $MODEL"
echo "Removable    : $RM (must be 1)"
echo "----------------------------------------------------"

[ "$RM" = "1" ] || { echo "ERROR: $DEV is not a removable device! Wrong disk might be selected."; exit 1; }

SZB=$(blockdev --getsize64 "$DEV")
[ "$SZB" -lt 137438953472 ] || { echo "ERROR: Device size is larger than 128GB, aborted for safety."; exit 1; }

echo ""
echo "WARNING: All data on $DEV will be permanently erased!"
read -rp "Type 'YAZ' in capital letters to continue: " CONFIRM
[ "$CONFIRM" = "YAZ" ] || { echo "Operation cancelled."; exit 1; }

# 3. Unmount partitions
echo "==> Unmounting mounted partitions..."
for p in "$DEV"?*; do umount "$p" 2>/dev/null || true; done

# 4. Partitioning (p1 = 64MB FAT16 Boot, p2 = Remaining space ext4 Rootfs)
echo "==> Writing disk partition table..."
wipefs -a "$DEV"
sfdisk "$DEV" <<'EOF'
label: dos
unit: sectors
2048,131072,c,*
133120,,83
EOF

partprobe "$DEV" || true
sleep 2

P1="${DEV}1"
P2="${DEV}2"
[ -b "$P1" ] || P1="${DEV}p1"
[ -b "$P2" ] || P2="${DEV}p2"

echo "==> Formatting filesystems..."
mkfs.vfat -F16 -n EXPEBOOT "$P1"
mkfs.ext4 -F -L experoot "$P2"

# 5. Write U-Boot SPL (Sector 8 / 8KB offset)
echo "==> Writing U-Boot SPL (Sector 8, does not touch internal NAND)..."
dd if="$UBOOT" of="$DEV" bs=1k seek=8 conv=fsync

# 6. Boot Partition Files
echo "==> Copying boot partition files..."
MNT_BOOT=$(mktemp -d)
mount "$P1" "$MNT_BOOT"

cp "$ZIMG" "$MNT_BOOT/zImage"
cp "$DTB" "$MNT_BOOT/sun6i-a31s-exper-t8e.dtb"

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
echo ">>> Starting Linux..."
bootz 0x42000000 - 0x43000000
EOF

"$MKIMAGE" -C none -A arm -T script -d "$MNT_BOOT/boot.cmd" "$MNT_BOOT/boot.scr"

sync
umount "$MNT_BOOT"
rmdir "$MNT_BOOT"

# 7. Rootfs Partition Files
echo "==> Copying rootfs..."
MNT_ROOT=$(mktemp -d)
mount "$P2" "$MNT_ROOT"

rsync -aHAX --numeric-ids --info=progress2 "$RFS/" "$MNT_ROOT/"

sync
umount "$MNT_ROOT"
rmdir "$MNT_ROOT"

echo "----------------------------------------------------"
echo "==> SUCCESS! MicroSD card prepared."
echo "You can insert the card into your Exper Easypad 8 tablet and power it on."
echo "User: exper  /  Password: exper  (Root password: exper)"
echo "----------------------------------------------------"
