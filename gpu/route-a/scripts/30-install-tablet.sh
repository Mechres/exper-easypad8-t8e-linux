#!/usr/bin/env bash
# Install route A on the tablet (over ssh): the stock Allwinner GPU user space from YOUR tablet's Android
# firmware, the route A kernel modules and the test programs.
#
# usage: STOCK_SYSTEM=/path/to/system 30-install-tablet.sh [--weston]
#
# STOCK_SYSTEM is a copy of /system from your own T8E's Android (adb pull /system, see gpu/route-a/README.md).
# Nothing from it is part of this repository.
#
# Installs:
#   /system/bin/linker, /system/lib, /system/vendor/{bin/pvrsrvctl,lib}, /system/etc/powervr.ini (from STOCK_SYSTEM)
#   /vendor -> /system/vendor (Android's EGL loader looks in /vendor/lib/egl)
#   /system/bin: the test programs (20-build-tools.sh)
#   /opt/pvr-stock: pvrsrvkm.ko, dcnohw.ko, ion_compat.ko (not in /lib/modules: they would clash with the TI
#   stack's pvrsrvkm), __properties__ (from STOCK_SYSTEM/build.prop), stock-run
#   /system/build.prop: the stock one plus ro.hardware (libhybris reads its properties from this file)
#   /opt/hybris: libhybris, its glibc runtime, hybris-run and the glibc test programs, if 40-build-libhybris.sh was run
#   --weston (needs /opt/hybris): Debian weston 10 (gpu/scripts/40-fetch-weston.sh) in /usr/lib/arm-linux-gnueabihf and
#            /opt/hybris/bin, /opt/hybris/weston.ini, /opt/hybris/weston-run, /lib/ld-linux-armhf.so.3 (if missing)
set -euo pipefail
. "$(dirname "$0")/route-a-env.sh"

WESTON=0
[ "${1:-}" = --weston ] && WESTON=1

: "${STOCK_SYSTEM:?set STOCK_SYSTEM to a copy of your tablet stock /system, see gpu/route-a/README.md}"
for f in bin/linker vendor/bin/pvrsrvctl vendor/lib/libsrv_um.so lib/libEGL.so build.prop; do
	[ -e "$STOCK_SYSTEM/$f" ] || { echo "$STOCK_SYSTEM/$f not found" >&2; exit 1; }
done
for f in pvrsrvkm.ko dcnohw.ko ion_compat.ko bin/stock-kms-gles; do
	[ -e "$RA_WORK/$f" ] || { echo "$RA_WORK/$f missing: run 10- and 20-*.sh first" >&2; exit 1; }
done
rel="$(tablet_ssh uname -r)"
mod="$(modinfo -F vermagic "$RA_WORK/pvrsrvkm.ko" | cut -d' ' -f1)"
[ "$rel" = "$mod" ] || { echo "modules are for $mod, the tablet runs $rel: rebuild (10-build-modules.sh)" >&2; exit 1; }

O="$(mktemp -d)"
trap 'rm -rf "$O"' EXIT
mkdir -p "$O/system/bin" "$O/system/vendor/bin" "$O/system/etc" "$O/opt/pvr-stock"
cp -a "$STOCK_SYSTEM/bin/linker" "$O/system/bin/"
[ -e "$STOCK_SYSTEM/bin/toolbox" ] && cp -a "$STOCK_SYSTEM/bin/toolbox" "$O/system/bin/"
cp -a "$STOCK_SYSTEM/lib" "$O/system/"
cp -a "$STOCK_SYSTEM/vendor/lib" "$O/system/vendor/"
cp -a "$STOCK_SYSTEM/vendor/bin/pvrsrvctl" "$O/system/vendor/bin/"
[ -e "$STOCK_SYSTEM/etc/powervr.ini" ] && cp -a "$STOCK_SYSTEM/etc/powervr.ini" "$O/system/etc/"
cp "$RA_WORK"/bin/* "$O/system/bin/"
cp "$RA_WORK"/{pvrsrvkm,dcnohw,ion_compat}.ko "$O/opt/pvr-stock/"
# ro.hardware comes from the kernel command line on Android; the HAL loader needs it for gralloc.sun6i.so
python3 -I "$RA_DIR/tools/mkprops.py" "$O/opt/pvr-stock/__properties__" "$STOCK_SYSTEM/build.prop" ro.hardware=sun6i
install -m 755 "$RA_DIR/tools/stock-run.sh" "$O/opt/pvr-stock/stock-run"
{ cat "$STOCK_SYSTEM/build.prop"; echo ro.hardware=sun6i; } > "$O/system/build.prop"
if [ -d "$HYBRIS_STAGE" ]; then cp -a "$HYBRIS_STAGE" "$O/opt/hybris"; fi
if [ "$WESTON" = 1 ]; then
	W="$GPU_WORK/weston-root"
	[ -d "$W/usr/bin" ] || { echo "run gpu/scripts/40-fetch-weston.sh first" >&2; exit 1; }
	[ -d "$HYBRIS_STAGE" ] || { echo "run 40-build-libhybris.sh first" >&2; exit 1; }
	mkdir -p "$O/usr/lib/arm-linux-gnueabihf" "$O/usr/share/fonts/dejavu-debian"
	cp -a "$W/usr/lib/arm-linux-gnueabihf/." "$W/lib/arm-linux-gnueabihf/." "$O/usr/lib/arm-linux-gnueabihf/"
	rm -rf "$O/usr/lib/arm-linux-gnueabihf/pkgconfig" \
		"$O"/usr/lib/arm-linux-gnueabihf/libweston-10/{rdp-backend,pipewire-plugin,remoting-plugin}.so
	cp -a "$W"/usr/bin/weston* "$O/opt/hybris/bin/"
	cp -a "$W/usr/share/weston" "$O/usr/share/"
	cp "$W"/usr/share/fonts/truetype/dejavu/*.ttf "$O/usr/share/fonts/dejavu-debian/"
	cat > "$O/opt/hybris/weston.ini" <<'EOS'
[core]
backend=drm-backend.so
idle-time=0
require-input=false

[shell]
panel-position=top
locking=false

[output]
name=LVDS-1
mode=preferred

[launcher]
icon=/usr/share/weston/icon_terminal.png
path=/opt/hybris/bin/weston-terminal

[launcher]
icon=/usr/share/weston/icon_flower.png
path=/opt/hybris/bin/weston-flower

[launcher]
icon=/usr/share/weston/icon_ivi_simple-egl.png
path=/opt/hybris/weston-run /opt/hybris/bin/weston-simple-egl
EOS
	cat > "$O/opt/hybris/weston-run" <<'EOS'
#!/bin/sh
# weston on the stock GPU libraries (libhybris, GBM on gralloc) on tty2; with arguments, run a Wayland client
# in weston's environment instead.
export XDG_RUNTIME_DIR=${XDG_RUNTIME_DIR:-/run/user/0}
export WAYLAND_DISPLAY=${WAYLAND_DISPLAY:-wayland-0}
if [ $# -gt 0 ]; then
	# EGL clients use libhybris' Wayland platform (shm-only clients do not mind the preloads)
	HYBRIS_EGLPLATFORM=wayland exec /opt/hybris/hybris-run "$@"
fi
/opt/pvr-stock/stock-run true >/dev/null
# no logind: fail at once instead of a 30 s D-Bus timeout, weston then uses its direct launcher
export DBUS_SYSTEM_BUS_ADDRESS=${DBUS_SYSTEM_BUS_ADDRESS:-unix:path=/nonexistent}
mkdir -p "$XDG_RUNTIME_DIR" && chmod 700 "$XDG_RUNTIME_DIR"
pgrep -f 'bin/weston --config' >/dev/null || rm -f "$XDG_RUNTIME_DIR/$WAYLAND_DISPLAY" "$XDG_RUNTIME_DIR/$WAYLAND_DISPLAY.lock"
HYBRIS_EGLPLATFORM=gbm exec /opt/hybris/hybris-run /opt/hybris/bin/weston --config=/opt/hybris/weston.ini \
	--tty=2 --socket="$WAYLAND_DISPLAY"
EOS
	chmod +x "$O/opt/hybris/weston-run"
fi

tar -C "$O" --owner=0 --group=0 -czf - system opt | tablet_ssh 'tar -xzf - -C / && chmod 755 /system/bin/* /system/vendor/bin/pvrsrvctl &&
	{ [ -e /vendor ] || ln -s /system/vendor /vendor; } && echo installed'
if [ "$WESTON" = 1 ]; then
	tablet_ssh '[ -e /lib/ld-linux-armhf.so.3 ] || ln -s /opt/hybris/lib/ld-linux-armhf.so.3 /lib/ld-linux-armhf.so.3'
	echo "  weston: /opt/hybris/weston-run (stop it with Ctrl-C or kill; /opt/pvr-stock/stock-run restore afterwards)"
fi
echo "on the tablet: /opt/pvr-stock/stock-run [/system/bin/stock-kms-gles 10]; /opt/pvr-stock/stock-run restore"
if [ -d "$HYBRIS_STAGE" ]; then echo "  libhybris: /opt/pvr-stock/stock-run true; /opt/hybris/hybris-run /opt/hybris/bin/hybris-kms-gles 10"; fi
