# Route A: the tablet's own stock GPU libraries on mainline Linux

Every T8E ships the PowerVR SGX user space in its Android 4.4 firmware: Allwinner's build of Imagination's DDK
**1.12@2681055** (`libEGL_POWERVR_SGX544_115.so`, `libGLESv2_POWERVR_SGX544_115.so`, `libsrv_um.so`, `pvrsrvctl`,
`gralloc.sun6i.so`, ...). Route A runs those libraries on the Linux 7.2 kernel instead of TI's DDK 1.17 user space
(see [`../README.md`](../README.md#licensing) for why that matters). This directory ships **no stock files**: you
copy `/system` from your own tablet and the install script takes what it needs from that copy.

Background: Finding 62 in [`docs/FINDINGS.md`](../../docs/FINDINGS.md).

## Status

| What | State |
| :--- | :--- |
| Kernel driver | openpvrsgx DDK 1.14.3841212 services (Allwinner drop) ported to Linux 7.2 as a non-DRM `/dev/pvrsrvkm` module and patched to the stock 1.12 user-space ABI: 179 of 180 shared structures identical to the stock module, the one difference is kernel-internal |
| GPU start-up | The stock `pvrsrvctl` loads the stock microkernel; SGX revision 1.1.5 runs its own matching firmware (unlike the TI route) |
| OpenGL ES 2.0 | Works offscreen (pbuffer) and with window surfaces, `GL_VERSION` "OpenGL ES 2.0 build 1.12@2681055" |
| Buffers and fences | Stock `gralloc` "gpu0" allocator on `/dev/ion` (`ion-compat/`, dma-buf based); `/dev/pvr_sync` native fences in `pvrsrvkm` |
| On-screen (KMS) | Works: gralloc buffers are imported into `sun4i-drm` as dma-bufs (PRIME) and page-flipped, 55 fps on the LVDS panel with 1 or 10 blended layers; 60 s at 10 layers with no memory growth |
| Android programs | Built with the NDK, run with the stock `/system/bin/linker` |
| Linux programs | glibc programs through libhybris (`/opt/hybris`): offscreen GLES and on-screen KMS at 55 fps |
| GBM | `gbm-hybris/`: a libgbm on gralloc buffers plus a libhybris EGL platform, enough for GBM/EGL/KMS programs and weston's DRM backend (55 fps) |
| Wayland | Debian's weston 10 composites with the GL renderer on the stock libraries; shm clients, EGL clients (libhybris' Wayland platform, e.g. `weston-simple-egl`) and touch work |
| Not done | a real display-class driver (`dcnohw` is a placeholder), the stock `gralloc` "fb0" device (it uses Allwinner's 3.x `/dev/disp` ioctls), clock-framework power code, suspend, power measurement |

Route A and the TI stack both use a module called `pvrsrvkm`, so only one runs at a time. `stock-run` swaps the TI
stack out and `stock-run restore` brings it back.

## How it fits together

```
 bionic program (NDK) --- /system/bin/linker, /system/lib (stock Android libc, libEGL, libhardware, ...)
          |
   stock EGL/GLES 1.12, libsrv_um, gralloc.sun6i.so (Allwinner/Imagination, from YOUR /system)
          |                       |                         |
   /dev/pvrsrvkm            /dev/ion                   /dev/pvr_sync
   pvrsrvkm.ko + dcnohw.ko  ion_compat.ko (dma-buf)    (in pvrsrvkm.ko)
          |                       |
       SGX544 MP2           dma-buf fd --PRIME--> sun4i-drm (/dev/dri/card0) --> panel
```

- `kernel-module/`: patches against openpvrsgx (`linux_openpvrsgx`, branch `letux-pvrsrvkm-6.0-rc7`,
  `drivers/gpu/drm/pvrsgx/1.14.3841212/eurasia_km`, dual MIT/GPLv2): out-of-tree Linux 7.2 build, the stock structure
  layouts and ioctl numbering (`PVR_T8E_ABI_1_12`), the stock version string, the sunxi system layer with the T8E
  power-up sequence, vmap-backed device memory (the stock microkernel does not start without it), `dc_nohw` as
  `dcnohw.ko`, ION buffers imported as plain dma-bufs, and a self-contained `/dev/pvr_sync`.
- `ion-compat/`: `/dev/ion` with the Android 3.4-era ION ABI the stock gralloc uses. Every buffer is a dma-buf of
  physically contiguous write-combined CMA memory, so it can be imported by the GPU driver and by `sun4i-drm`.
- `tools/`: test programs (`stock-gles-probe`, `fb-hal-probe`, `stock-window-probe`, `stock-kms-gles`), `mkprops.py`
  (writes the Android property area bionic expects at `/dev/__properties__`, from `build.prop`), `stock-run.sh`, and
  in `abi/` the scripts used to match the ABI against the stock `pvrsrvkm.ko` (which keeps its DWARF debug
  information): `layout-diff.sh` (member offsets, needs `pahole`), `compare-ids.sh` and `dispatch-table.py` (ioctl
  numbering).

## Linux programs: libhybris

[libhybris](https://github.com/libhybris/libhybris) loads Android libraries into glibc programs. Built for Android
4.4.2 (headers from Halium's `android-headers` history) with the toolchain's glibc, it runs the stock EGL/GLES on
Alpine next to musl, from `/opt/hybris` with its own glibc runtime. Two things it does not do on its own are handled
by `tools/hybris-bionic-init.c`, which must be the first preload (`hybris-run` takes care of it):

- the stock bionic `libc.so` is loaded but never initialised, so property lookups inside it crash; the preload calls
  its `__system_properties_init()`. (libhybris itself reads properties from `/system/build.prop`, so the installer
  adds `ro.hardware=sun6i` there.)
- Android code keeps per-thread data in fixed slots after the TLS register (the GL dispatch table and context in
  slots 3 and 4). With glibc that space belongs to the first module's thread-local variables; the preload reserves
  it with a thread-local block of its own and checks that it landed there. Patched glibc builds used with libhybris
  elsewhere reserve these slots instead; this works with an unmodified glibc as long as the program itself has no
  thread-local variables.

`gbm-hybris/` adds GBM: a `gbm_surface` is an Android native window over gralloc buffers whose dma-bufs are imported
into the KMS device, `gbm_surface_lock_front_buffer` returns the buffer just rendered, and the libhybris EGL platform
"gbm" (`HYBRIS_EGLPLATFORM=gbm`) turns a `gbm_surface` into that window. The same preload translates the Android pixel
formats the stock EGL reports as `EGL_NATIVE_VISUAL_ID` into DRM fourccs and maps `EGL_PLATFORM_GBM_KHR` displays onto
the "gbm" platform.

## Getting your stock /system

Boot the tablet's Android (without the Linux SD card), enable USB debugging and pull the system partition:

```sh
adb pull /system t8e-system
```

Keep it private: it is Allwinner's and Imagination's proprietary firmware, licensed with your tablet. Whether using it
under Linux on the same tablet is covered is for you to judge (this is not legal advice).

## Building and installing

Requirements: what [`../README.md`](../README.md#building-and-installing) lists for the kernel module (the built
kernel, a glibc armhf cross toolchain), an Android NDK (r25c tested) and `python3`.

```sh
gpu/route-a/scripts/10-build-modules.sh               # openpvrsgx + patches, ion-compat -> gpu/work/route-a/*.ko
NDK_DIR=/path/to/android-ndk-r25c \
  gpu/route-a/scripts/20-build-tools.sh               # bionic test programs
gpu/route-a/scripts/40-build-libhybris.sh            # libhybris, gbm-hybris, glibc test programs
gpu/scripts/40-fetch-weston.sh                       # optional: Debian weston 10
STOCK_SYSTEM=/path/to/t8e-system \
  gpu/route-a/scripts/30-install-tablet.sh [--weston] # stock files + modules + tools to the tablet
```

Step 40 also needs `autoconf`, `automake`, `libtool` and `wayland-scanner` on the build machine.

The modules must match the running kernel exactly; the installer refuses a mismatch. On the tablet the stock files go
to `/system` (with `/vendor -> /system/vendor`), which is where the stock linker and EGL loader look; the modules,
the property area and `stock-run` go to `/opt/pvr-stock`.

## Using it on the tablet

```sh
/opt/pvr-stock/stock-run                                      # offscreen render + read-back check
/opt/pvr-stock/stock-run /system/bin/stock-window-probe       # window surface, fence, gralloc read-back
/opt/pvr-stock/stock-run /system/bin/stock-kms-gles 10 [layers]   # on the panel for 10 s
/opt/pvr-stock/stock-run restore                              # back to the TI stack (if installed)
```

`stock-run` prints the program's exit code and the end of the Android log (`/dev/log/main`, a plain file here).

Linux programs (after `/opt/pvr-stock/stock-run true` has started the stock services):

```sh
/opt/hybris/hybris-run /opt/hybris/bin/hybris-gles-probe            # offscreen
/opt/hybris/hybris-run /opt/hybris/bin/hybris-kms-gles 10 [layers]  # on the panel, own native window
HYBRIS_EGLPLATFORM=gbm /opt/hybris/hybris-run /opt/hybris/bin/kms-gles 10   # on the panel through GBM
/opt/hybris/weston-run                                              # weston on tty2 (--weston install)
/opt/hybris/weston-run /opt/hybris/bin/weston-simple-egl            # a client, from another shell
```
