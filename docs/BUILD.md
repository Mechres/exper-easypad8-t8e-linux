# Build & Installation Guide — Exper Easypad 8 (T8E)

This document provides step-by-step instructions to compile U-Boot, build the Linux kernel, generate the Alpine Linux rootfs, and flash an SD card for the Exper Easypad 8 tablet.

---

## 1. Prerequisites

### Host System Requirements
- Linux x86_64 or aarch64 workstation
- Standard build tools: `git`, `make`, `gcc`, `bison`, `flex`, `swig`, `python3`, `libssl-dev`, `bc`
- Podman or Docker (for rootfs container building)
- ARMv7 cross-compiler toolchain: `arm-linux-gnueabihf-` or Bootlin `armv7-eabihf--glibc`

### Environment Setup
All build scripts inherit environment settings from `scripts/00-env.sh`. You can configure the paths using environment variables:

```bash
# Optional: specify custom paths if not using defaults
export TOOLCHAIN_DIR="/path/to/arm-toolchain/bin"
export KERNEL_SRC="/path/to/linux"
export UBOOT_SRC="/path/to/u-boot"

# Source the environment
source scripts/00-env.sh
```

---

## 2. Building U-Boot

1. Apply the U-Boot patch in `patches/uboot/` to your U-Boot source tree:
   ```bash
   cd "$UBOOT_SRC"
   git am /path/to/exper-easypad8-t8e/patches/uboot/0001-*.patch
   ```
2. Build U-Boot using the automated script:
   ```bash
   bash scripts/20-build-uboot.sh
   ```
3. The resulting binary will be placed at:
   `uboot/u-boot-sunxi-with-spl.bin`

---

## 3. Building the Linux Kernel

1. Apply the clean kernel patch series from `patches/kernel/` to a mainline Linux tree (6.12+ or 7.x):
   ```bash
   cd "$KERNEL_SRC"
   git am /path/to/exper-easypad8-t8e/patches/kernel/*.patch
   ```
2. Build the kernel zImage, Device Tree blob, and kernel modules:
   ```bash
   bash scripts/30-build-kernel.sh
   ```
3. Build artifacts generated in `kernel/`:
   - `kernel/zImage`
   - `kernel/sun6i-a31s-exper-t8e.dtb`
   - `kernel/modules/`

---

## 4. Generating Alpine Linux Rootfs

The rootfs is constructed inside an unprivileged Alpine container via Podman:

```bash
bash scripts/40-alpine-rootfs.sh
```

This prepares:
- Alpine Linux 3.20 ARMv7 userland
- OpenRC init system
- OpenSSH server with root login enabled
- USB gadget networking (`usb0` configured with `172.16.42.1/24`)
- Serial login getty on `ttyS0`

---

## 5. Flashing the MicroSD Card

> [!CAUTION]
> Verify your SD card device path carefully (e.g. `/dev/sdX`). Writing to the wrong drive will erase data!

1. Flash U-Boot, create partitions, format filesystems, and install rootfs:
   ```bash
   sudo bash scripts/50-sdcard-alpine.sh /dev/sdX
   ```
   *Partition Layout:*
   - `sdX1` (64 MB, FAT16, label `EXPEBOOT`): `boot.scr`, `zImage`, `sun6i-a31s-exper-t8e.dtb`
   - `sdX2` (Remainder, ext4, label `experoot`): Alpine Linux root filesystem

2. To update only the kernel and DTB on an existing SD card without re-formatting:
   ```bash
   sudo bash scripts/51-update-kernel-sdcard.sh /dev/sdX
   ```

---

## 6. First Boot & SSH Access

1. Insert the MicroSD card into the tablet's slot.
2. Connect the Micro-USB port to your host PC.
3. Power on the tablet.
4. On your host PC, configure the host USB network interface:
   ```bash
   sudo ip link set dev usb0 up
   sudo ip addr add 172.16.42.2/24 dev usb0
   ```
5. Connect via SSH:
   ```bash
   ssh root@172.16.42.1
   # Password: exper
   ```
