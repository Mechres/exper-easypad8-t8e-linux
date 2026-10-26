#!/usr/bin/env bash
# Exper Easypad 8 (Allwinner A31s / sun6i) — Common Environment Variables
# Usage: source scripts/00-env.sh

PRJ_DERIVED="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
ROOT_PRJ="$(cd "$PRJ_DERIVED/.." && pwd)"

export PRJ="${PRJ:-$PRJ_DERIVED}"
# Cross toolchain: set TOOLCHAIN_DIR to the toolchain's bin/ directory (e.g. an unpacked
# Bootlin armv7-eabihf--glibc--stable-2024.05-1 release), or install a distro
# cross compiler (Debian/Ubuntu: gcc-arm-linux-gnueabihf, Arch: arm-linux-gnueabihf-gcc).
if [ -n "${TOOLCHAIN_DIR:-}" ]; then
    export TC="$TOOLCHAIN_DIR"
    export PATH="$TC:$PATH"
fi
export ARCH="${ARCH:-arm}"
if [ -z "${CROSS_COMPILE:-}" ]; then
    for p in arm-buildroot-linux-gnueabihf- arm-linux-gnueabihf- arm-none-linux-gnueabihf-; do
        if command -v "${p}gcc" >/dev/null 2>&1; then CROSS_COMPILE="$p"; break; fi
    done
fi
export CROSS_COMPILE="${CROSS_COMPILE:-arm-linux-gnueabihf-}"
export JOBS="${JOBS:--j$(nproc)}"

echo "PRJ=$PRJ"
if command -v ${CROSS_COMPILE}gcc >/dev/null 2>&1; then
    echo -n "cross gcc: "
    ${CROSS_COMPILE}gcc --version | head -1
else
    echo "WARNING: no ARMv7 cross toolchain found (looked for ${CROSS_COMPILE}gcc)."
    echo "         Set TOOLCHAIN_DIR=/path/to/toolchain/bin or install gcc-arm-linux-gnueabihf."
fi
