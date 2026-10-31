#!/usr/bin/env bash
# Build pvrsrvkm.ko: TI's SGX DDK 1.17.4948957 kernel driver (MIT/GPLv2) with the patches in
# gpu/kernel-module (Linux 7.x build fixes, Allwinner A31/A31s "sunxi" system layer).
# Needs the kernel built first (scripts/30-build-kernel.sh); the module must match it exactly.
# Output: $GPU_WORK/pvrsrvkm.ko
set -euo pipefail
. "$(dirname "$0")/gpu-env.sh"

KM="$GPU_WORK/omap5-sgx-ddk-linux"
[ -f "$KERNEL_BUILD/include/config/kernel.release" ] || { echo "no kernel build in $KERNEL_BUILD (set KERNEL_BUILD)" >&2; exit 1; }

# shallow: git.ti.com refuses full clones of this repository (HTTP 501)
[ -d "$KM/.git" ] || git clone -q --depth 1 -b "$KM_BRANCH" "$KM_REPO" "$KM"
cd "$KM"
git cat-file -e "$KM_REV^{commit}" 2>/dev/null || git fetch -q --depth 1 origin "$KM_REV"
git checkout -q -B t8e "$KM_REV"
git -c user.name=build -c user.email=build@localhost am -q "$GPU_DIR"/kernel-module/*.patch

make -j"$(nproc)" KERNELDIR="$KERNEL_BUILD" ARCH=arm CROSS_COMPILE="$CROSS_COMPILE" \
	KERNEL_CROSS_COMPILE="$CROSS_COMPILE" WINDOW_SYSTEM=lws-generic PVR_BUILD_DIR=sunxi_linux
cp eurasiacon/binary_sunxi_linux_lws-generic_release/target_armhf/pvrsrvkm.ko "$GPU_WORK/"
echo "built $GPU_WORK/pvrsrvkm.ko for $(modinfo -F vermagic "$GPU_WORK/pvrsrvkm.ko" | cut -d' ' -f1)"
