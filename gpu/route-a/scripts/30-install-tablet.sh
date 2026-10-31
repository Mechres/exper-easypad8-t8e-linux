#!/usr/bin/env bash
# Install route A on the tablet (over ssh): the stock Allwinner GPU user space from YOUR tablet's Android
# firmware, the route A kernel modules and the test programs.
#
# usage: STOCK_SYSTEM=/path/to/system 30-install-tablet.sh
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
set -euo pipefail
. "$(dirname "$0")/route-a-env.sh"

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

tar -C "$O" --owner=0 --group=0 -czf - system opt | tablet_ssh 'tar -xzf - -C / && chmod 755 /system/bin/* /system/vendor/bin/pvrsrvctl &&
	{ [ -e /vendor ] || ln -s /system/vendor /vendor; } && echo installed'
echo "on the tablet: /opt/pvr-stock/stock-run [/system/bin/stock-kms-gles 10]; /opt/pvr-stock/stock-run restore"
