#!/usr/bin/env bash
# Exper Easypad 8 (Allwinner A31s) — Mainline Kernel, DTB & Module Build Script
#
# Source : a mainline Linux tree with the series in patches/kernel/ applied
#          (git am patches/kernel/*.patch; base = Linux 7.2-rc, commit cb8a75eec, or any tree that has it).
# Config : configs/exper_t8e_defconfig (savedefconfig output: no bring-up probes, no DEBUG_LL,
#          no "debug initcall_debug ignore_loglevel" on the command line).
# Build  : out of tree ($BUILD), so the kernel tree stays clean.
#
# Optional environment: KERNEL_SRC, BUILD, SYNC_DTS=1 (copy dts/*.dts over the tree's copies, useful
# when iterating on the device trees without re-generating patches).
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
source "$SCRIPT_DIR/00-env.sh"

KERNEL_SRC="${KERNEL_SRC:-$ROOT_PRJ/linux}"
BUILD="${BUILD:-$PRJ/kernel/build}"
DTS_DIR="arch/arm/boot/dts/allwinner"
echo "Kernel source directory: $KERNEL_SRC"
echo "Build directory        : $BUILD"
mkdir -p "$PRJ/kernel" "$BUILD"

if [ "${SYNC_DTS:-0}" = "1" ]; then
    cp "$PRJ/dts/sun6i-a31s-exper-t8e-kernel.dts" "$KERNEL_SRC/$DTS_DIR/sun6i-a31s-exper-t8e.dts"
    cp "$PRJ/dts/sun6i-a31s-exper-t8e-drm.dts" "$KERNEL_SRC/$DTS_DIR/sun6i-a31s-exper-t8e-drm.dts"
fi
if [ ! -f "$KERNEL_SRC/$DTS_DIR/sun6i-a31s-exper-t8e-drm.dts" ]; then
    echo "ERROR: $KERNEL_SRC does not contain the T8E patch series."
    echo "       cd \"$KERNEL_SRC\" && git am \"$PRJ\"/patches/kernel/*.patch"
    exit 1
fi

echo "==> Applying configs/exper_t8e_defconfig..."
cp "$PRJ/configs/exper_t8e_defconfig" "$BUILD/.config"
make -C "$KERNEL_SRC" O="$BUILD" olddefconfig

echo "==> Building kernel, DTBs and modules ($JOBS)..."
make -C "$KERNEL_SRC" O="$BUILD" $JOBS zImage dtbs modules
REL="$(cat "$BUILD/include/config/kernel.release")"

echo "==> Copying outputs (release $REL)..."
cp "$BUILD/arch/arm/boot/zImage" "$PRJ/kernel/zImage"
cp "$BUILD/arch/arm/boot/dts/allwinner/sun6i-a31s-exper-t8e.dtb" "$PRJ/kernel/"
cp "$BUILD/arch/arm/boot/dts/allwinner/sun6i-a31s-exper-t8e-drm.dtb" "$PRJ/kernel/"

echo "==> Installing modules to staging area..."
TMP_MOD=$(mktemp -d /tmp/exper-modules.XXXXXX)
make -C "$KERNEL_SRC" O="$BUILD" INSTALL_MOD_PATH="$TMP_MOD" INSTALL_MOD_STRIP=1 modules_install
rm -rf "$PRJ/kernel/modules"
mkdir -p "$PRJ/kernel/modules"
cp -a "$TMP_MOD/lib/modules/." "$PRJ/kernel/modules/"
rm -rf "$TMP_MOD"
rm -f "$PRJ/kernel/modules/$REL/build" "$PRJ/kernel/modules/$REL/source"

# Also update modules inside rootfs (old release directories are dropped)
if [ -d "$PRJ/rootfs/alpine/lib/modules" ]; then
    echo "==> Updating rootfs lib/modules..."
    rm -rf "$PRJ/rootfs/alpine/lib/modules/"*
    cp -a "$PRJ/kernel/modules/." "$PRJ/rootfs/alpine/lib/modules/"
fi

echo "==> Success!"
ls -lh "$PRJ/kernel/zImage" "$PRJ/kernel/"*.dtb
