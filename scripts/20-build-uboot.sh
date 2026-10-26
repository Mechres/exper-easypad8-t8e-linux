#!/usr/bin/env bash
# Exper Easypad 8 (Allwinner A31s) — U-Boot Build Script
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
source "$SCRIPT_DIR/00-env.sh"

UBOOT_SRC="${UBOOT_SRC:-$ROOT_PRJ/u-boot}"
echo "U-Boot source directory: $UBOOT_SRC"
if [ ! -f "$UBOOT_SRC/Makefile" ]; then
    echo "ERROR: no U-Boot tree at $UBOOT_SRC"
    echo "       git clone https://source.denx.de/u-boot/u-boot.git \"$UBOOT_SRC\""
    echo "       cd \"$UBOOT_SRC\" && git am \"$PRJ\"/patches/uboot/*.patch"
    echo "       (or point UBOOT_SRC at an existing checkout)"
    exit 1
fi

# Copy Config and DTS
cp "$PRJ/uboot/exper_t8e_defconfig" "$UBOOT_SRC/configs/exper_t8e_defconfig"
cp "$PRJ/dts/sun6i-a31s-exper-t8e.dts" "$UBOOT_SRC/arch/arm/dts/sun6i-a31s-exper-t8e.dts"

cd "$UBOOT_SRC"
echo "==> Applying exper_t8e_defconfig..."
make exper_t8e_defconfig

echo "==> Building U-Boot ($JOBS)..."
make $JOBS

cp u-boot-sunxi-with-spl.bin "$PRJ/uboot/u-boot-sunxi-with-spl.bin"
echo "==> Success! Output: $PRJ/uboot/u-boot-sunxi-with-spl.bin"
ls -lh "$PRJ/uboot/u-boot-sunxi-with-spl.bin"
