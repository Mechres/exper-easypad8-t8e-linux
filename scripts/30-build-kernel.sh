#!/usr/bin/env bash
# Exper Easypad 8 (Allwinner A31s) — Mainline Kernel & DTB Build Script
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
source "$SCRIPT_DIR/00-env.sh"

KERNEL_SRC="${KERNEL_SRC:-$ROOT_PRJ/telefon-canlandirma/piranha-a10/kernel/linux}"
echo "Kernel source directory: $KERNEL_SRC"
mkdir -p "$PRJ/kernel"

cd "$KERNEL_SRC"

# Copy DTS (Full hardware tree specially designed for Kernel)
cp "$PRJ/dts/sun6i-a31s-exper-t8e-kernel.dts" "$KERNEL_SRC/arch/arm/boot/dts/allwinner/sun6i-a31s-exper-t8e.dts"

echo "==> Applying sunxi_defconfig..."
make sunxi_defconfig

echo "==> Configuring hardware and Simple Framebuffer..."
# Framebuffer and Display Console (SimpleFB)
./scripts/config --enable CONFIG_FB
./scripts/config --enable CONFIG_FB_SIMPLE
./scripts/config --enable CONFIG_FRAMEBUFFER_CONSOLE
./scripts/config --disable CONFIG_FRAMEBUFFER_CONSOLE_DEFERRED_TAKEOVER
./scripts/config --enable CONFIG_LOGO
./scripts/config --enable CONFIG_FONTS
./scripts/config --enable CONFIG_FONT_8x16

# Allwinner DRM Display Engine (Disabled to prevent SimpleFB conflict)
./scripts/config --disable CONFIG_DRM_SUN4I
./scripts/config --disable CONFIG_DRM_SUN4I_BACKEND

# Early Debug Console (DEBUG_LL; decompressor/head.S while MMU is disabled)
./scripts/config --enable CONFIG_DEBUG_KERNEL
./scripts/config --enable CONFIG_DEBUG_LL
./scripts/config --enable CONFIG_DEBUG_SUNXI_UART0
./scripts/config --disable CONFIG_EARLY_PRINTK
./scripts/config --enable CONFIG_DEBUG_UNCOMPRESS

# Serial console: disable 16550A variant probes (ST16C650V1/TI16750/Xscale/size_fifo)
# — autoconfig_16550a() hangs deterministically in this area on A31s DW APB UART
# (Finding 15, t≈9.7). Since Kconfig default is `default !X86`, it was always
# enabled on ARM; not needed for console-UART (per Kconfig help recommendation).
./scripts/config --disable CONFIG_SERIAL_8250_16550A_VARIANTS

# Bootargs assurance (even if U-Boot injection fails)
./scripts/config --set-val CONFIG_LOG_BUF_SHIFT 20
# Finding 16: console=ttyS0 REMOVED — conflict between earlycon and ttyS0 setup
# (LCR/BUSY mismatch) was hanging the kernel. earlycon (keep_bootcon) carries all serial
# output; getty (userspace) opens ttyS0 itself.
./scripts/config --set-str CONFIG_CMDLINE "console=tty0 keep_bootcon earlycon=uart8250,mmio32,0x01c28000 root=/dev/mmcblk0p2 rootwait rw nosmp panic=0 fbcon=map:0 loglevel=8 ignore_loglevel consoleblank=0 debug initcall_debug"
./scripts/config --enable CONFIG_CMDLINE_FORCE

# Touchscreen (Goodix GT911)
./scripts/config --enable CONFIG_INPUT_TOUCHSCREEN
./scripts/config --enable CONFIG_TOUCHSCREEN_GOODIX

# Wi-Fi (Broadcom AP6210 / BCM40181 SDIO)
./scripts/config --enable CONFIG_WLAN
./scripts/config --enable CONFIG_CFG80211
./scripts/config --module CONFIG_BRCMUTIL
./scripts/config --module CONFIG_BRCMFMAC
./scripts/config --enable CONFIG_BRCMFMAC_SDIO

# Accelerometer (LIS3DH)
./scripts/config --enable CONFIG_IIO
./scripts/config --enable CONFIG_IIO_BUFFER
./scripts/config --module CONFIG_SENSORS_LIS3_I2C

# USB Networking and Storage
./scripts/config --enable CONFIG_USB_NET_DRIVERS
./scripts/config --enable CONFIG_USB_USBNET
./scripts/config --enable CONFIG_USB_NET_CDCETHER
./scripts/config --enable CONFIG_USB_NET_RNDIS_HOST
./scripts/config --enable CONFIG_USB_NET_ASIX
./scripts/config --enable CONFIG_USB_NET_MCS7830
./scripts/config --enable CONFIG_USB_STORAGE

# USB Gadget Ethernet (USB networking when tablet is connected to PC)
./scripts/config --enable CONFIG_USB_ETH
./scripts/config --enable CONFIG_USB_ETH_RNDIS

# Filesystems
./scripts/config --enable CONFIG_EXT4_FS
./scripts/config --enable CONFIG_VFAT_FS
./scripts/config --enable CONFIG_DEVTMPFS
./scripts/config --enable CONFIG_DEVTMPFS_MOUNT

echo "==> Running olddefconfig..."
make olddefconfig

echo "==> Configuration check:"
grep -E "CONFIG_FB=|CONFIG_FB_SIMPLE|CONFIG_FRAMEBUFFER_CONSOLE=|CONFIG_DRM_SUN4I" .config || true

echo "==> Building Kernel and DTB ($JOBS)..."
make $JOBS zImage dtbs modules

echo "==> Copying outputs..."
cp arch/arm/boot/zImage "$PRJ/kernel/zImage"
cp arch/arm/boot/dts/allwinner/sun6i-a31s-exper-t8e.dtb "$PRJ/kernel/sun6i-a31s-exper-t8e.dtb"

echo "==> Installing modules to staging area..."
TMP_MOD=$(mktemp -d /tmp/exper-modules.XXXXXX)
make INSTALL_MOD_PATH="$TMP_MOD" modules_install
rm -rf "$PRJ/kernel/modules"
mkdir -p "$PRJ/kernel/modules"
cp -a "$TMP_MOD/lib/modules" "$PRJ/kernel/"
rm -rf "$TMP_MOD"

# Also update modules inside rootfs
if [ -d "$PRJ/rootfs/alpine/lib/modules" ]; then
    echo "==> Updating rootfs lib/modules..."
    cp -a "$PRJ/kernel/modules/"* "$PRJ/rootfs/alpine/lib/modules/"
fi

echo "==> Success!"
ls -lh "$PRJ/kernel/zImage" "$PRJ/kernel/sun6i-a31s-exper-t8e.dtb"
