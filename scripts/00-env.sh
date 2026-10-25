#!/usr/bin/env bash
# Exper Easypad 8 (Allwinner A31s / sun6i) — Common Environment Variables
# Usage: source scripts/00-env.sh

PRJ_DERIVED="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
ROOT_PRJ="$(cd "$PRJ_DERIVED/.." && pwd)"

export PRJ="${PRJ:-$PRJ_DERIVED}"
# Use ready-made bootlin armv7 toolchain from piranha-a10
export TC="${TC:-$ROOT_PRJ/piranha-a10/toolchain/armv7-eabihf--glibc--stable-2024.05-1/bin}"
export PATH="$TC:$PATH"
export ARCH="${ARCH:-arm}"
export CROSS_COMPILE="${CROSS_COMPILE:-arm-buildroot-linux-gnueabihf-}"
export JOBS="${JOBS:--j$(nproc)}"

echo "PRJ=$PRJ"
if command -v ${CROSS_COMPILE}gcc >/dev/null 2>&1; then
    echo -n "cross gcc: "
    ${CROSS_COMPILE}gcc --version | head -1
else
    echo "WARNING: Cross toolchain not found ($TC)!"
fi
