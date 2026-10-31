# PowerVR SGX544 MP2 GPU on mainline Linux

The A31s in the Exper Easypad T8E has an Imagination PowerVR SGX544 MP2 (core revision 1.1.5). Mainline Linux has no
driver for it. This directory contains everything needed to run it with OpenGL ES 2.0 and Wayland on the tablet's Linux
7.2 kernel **except the GPU user-space libraries**, which cannot be included (see [Licensing](#licensing)).

Background, measurements and the full story: Findings 52, 60 and 61 in [`docs/FINDINGS.md`](../docs/FINDINGS.md).

## Status

| What | State |
| :--- | :--- |
| Kernel driver | TI's SGX DDK 1.17.4948957 kernel module, ported to Linux 7.2 with an Allwinner A31/A31s system layer; loads from the device tree node (patch 0020), GPU microkernel runs |
| OpenGL ES 2.0 | Works: GBM/EGL/GLES2 through TI's Mesa fork (`sgx` Gallium driver); `GL_RENDERER` "PowerVR SGX 544MP" |
| On-screen (KMS) | Works on the LVDS panel at 55 fps (vsync), 3 min fill-rate stress stable |
| glmark2-es2-drm | Score 211 at 1024x768; `terrain` and `refract` are CPU-bound inside the closed GLES library (1 to 2 fps) |
| Wayland | Debian bookworm's weston 10 composites with the GL renderer; EGL Wayland clients run at 55 fps; touch works |
| Boot integration | OpenRC service `pvr` initialises the GPU at boot; `weston` service available, not enabled |
| Route A | Stock Allwinner DDK 1.12 user space from the tablet's own firmware on a ported openpvrsgx module: GLES2 offscreen and on the panel at 55 fps, bionic programs only so far ([`route-a/`](route-a/README.md)) |
| Not done | Clock-framework power code (raw CCU writes for now), suspend/resume, power measurement, GLES 3 (the hardware is GLES 2 only) |

Core revision 1.1.5 runs TI's microkernel built for revision 1.1.6 (DRA7/AM57xx, the same SGX544 MP2). Nothing has gone
wrong in testing so far, but errata differences between the two revisions are possible.

## Licensing

| Part | License | In this repository |
| :--- | :--- | :--- |
| Kernel module source (TI `omap5-sgx-ddk-linux`) | Dual MIT / GPLv2 | Patches in `kernel-module/`; the scripts fetch TI's source |
| Mesa fork (`StaticRocket/mesa`, used by TI's meta-ti) | MIT | Patch in `mesa/`; the scripts fetch the source |
| Tools and shims in `tools/`, scripts in `scripts/` | MIT | Yes |
| Device tree node | GPL-2.0+ | `patches/kernel/0020-*` |
| Debian bookworm packages (libdrm, wayland, weston, ...) | Various free licenses | Downloaded by the scripts from Debian |
| **TI SGX user space** (`libsrv_um`, `libsrv_init`, `libGLESv2_PVR_MESA`, `libpvr_dri_support`, `pvrsrvctl`, ...) | TI text file license: binary redistribution only unmodified and **"for use only with TI Devices"** | **No.** The A31s is not a TI device, so these binaries are not distributed here and the scripts do not download them |

The working stack therefore needs a copy of TI's user space that you obtain and use under TI's terms; whether that use is
covered for a non-TI device is for you to judge (this is not legal advice). The alternative is **route A**
([`route-a/`](route-a/README.md)): the tablet's own stock Allwinner GPU libraries (DDK 1.12, part of every T8E's Android
firmware, copied from your own tablet) with a kernel module adapted to them. It renders offscreen and on the panel at
55 fps, for Android (bionic) programs so far.

## How it fits together

```
 GLES/EGL/GBM app (glibc)              weston (Debian, glibc)
          |                                     |
   Mesa (sgx driver) ---- libpvr_dri_support, libGLESv2_PVR_MESA, libsrv_um  (TI, closed)
          |                                     |
   /dev/dri/card0 (sun4i-drm, display)    /dev/dri/renderD128 (pvr, GPU)
                                                |
                                   pvrsrvkm.ko (TI DDK 1.17 + sunxi port) -> SGX544 MP2
```

- The user space is glibc; Alpine is musl. Everything GPU-related lives in `/opt/pvr` and runs through the glibc loader
  (`/opt/pvr/pvr-run prog` or `/opt/pvr/weston-run prog`), side by side with musl.
- TI's `libsrv_um` only accepts a display DRM device named `omapdrm`. `tools/pvr-sunxi-shim.c` presents `sun4i-drm` under
  that name. weston 10 needs `GL_EXT_unpack_subimage`, which the SGX library lacks; `tools/gles-unpack-shim.c` emulates
  it. Both are loaded through `/etc/ld.so.preload`, which glibc reads and musl ignores.
- `tools/segv-report.c` is a preload crash reporter for debugging on the tablet (there is no gdb).
- The kernel module needs the board's GPU node: patch 0020 (`patches/kernel/`).

## Building and installing

Requirements on the build machine: the same **glibc** armhf cross toolchain used for the kernel (`TOOLCHAIN_DIR` or
`CROSS_COMPILE`, see `scripts/00-env.sh`), the built kernel (`scripts/30-build-kernel.sh`, with patch 0020), `meson`,
`ninja`, `python3`, `wayland-scanner`, `curl`, `xz`, `bsdtar`, `sshpass`. The work directory defaults to `gpu/work/`
(`GPU_WORK`); the kernel build directory to `kernel/build/` (`KERNEL_BUILD`).

```sh
gpu/scripts/10-build-kernel-module.sh   # TI kernel module + kernel-module/*.patch -> gpu/work/pvrsrvkm.ko
gpu/scripts/20-build-mesa.sh            # sysroot (toolchain glibc + Debian -dev packages), Mesa -> gpu/work/stage
gpu/scripts/30-build-tools.sh           # shims and test programs -> gpu/work/stage
gpu/scripts/40-fetch-weston.sh          # optional: Debian weston 10 and its libraries
TI_UM_DIR=/path/to/targetfs/ti572x_linux/lws-generic/release \
  gpu/scripts/50-install-tablet.sh --weston [--dtb kernel/sun6i-a31s-exper-t8e-drm.dtb]
```

The module must match the running kernel exactly; rebuild it (step 10) after every kernel rebuild. The installer refuses
a mismatch.

## Using it on the tablet

```sh
rc-service pvr status                                 # started at boot
/opt/pvr/pvr-run /opt/pvr/bin/gles-probe              # offscreen render + read-back check
/opt/pvr/pvr-run /opt/pvr/bin/kms-gles 10 [layers]    # on the panel; layers > 1 adds blended fill-rate load
rc-service weston start                               # weston on tty2 (takes over the display)
/opt/pvr/weston-run /opt/pvr/bin/weston-simple-egl    # an EGL Wayland client
```

Only start glibc programs through `weston-run`: it sets `LD_LIBRARY_PATH` to the glibc libraries. The weston panel has
launchers for a terminal, the flower demo and `weston-simple-egl`. A larger SGX parameter buffer is set in
`/etc/powervr.ini`.
