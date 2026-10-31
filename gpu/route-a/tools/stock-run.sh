#!/bin/sh
# Run a program on the stock Allwinner DDK 1.12 GPU user space (/system, bionic) on Linux.
# Swaps the TI 1.17 stack (OpenRC service "pvr") out if it is running, loads the route A modules, starts the
# stock GPU services and runs the program (default: the offscreen GLES probe).
#   stock-run [program args...]
#   stock-run restore      unload the route A modules and start the TI stack again (if installed)
set -e
D=/opt/pvr-stock
if [ "$1" = restore ]; then
	rmmod dcnohw 2>/dev/null || true; rmmod pvrsrvkm 2>/dev/null || true; rmmod ion_compat 2>/dev/null || true
	if [ -e /etc/init.d/pvr ]; then rc-service pvr start >/dev/null && echo "TI 1.17 stack restored"; fi
	exit 0
fi
# bionic maps the property area unchecked; Android's log devices become plain files
[ -e /dev/__properties__ ] || { cp $D/__properties__ /dev/__properties__; chmod 0444 /dev/__properties__; }
mkdir -p /dev/log; for b in main system radio events; do : > /dev/log/$b; chmod 666 /dev/log/$b; done
grep -q "^ion_compat" /proc/modules || insmod $D/ion_compat.ko
if ! grep -q "^dcnohw" /proc/modules; then
	[ -e /etc/init.d/pvr ] && { rc-service pvr stop >/dev/null 2>&1 || true; }
	rmmod pvrsrvkm 2>/dev/null || true
	insmod $D/pvrsrvkm.ko; insmod $D/dcnohw.ko
	(cd /system/vendor/bin && LD_LIBRARY_PATH=/system/vendor/lib:/system/lib ./pvrsrvctl --start --no-module)
	echo "stock services started"
fi
export LD_LIBRARY_PATH=/system/vendor/lib:/system/lib
[ $# -gt 0 ] || set -- /system/bin/stock-gles-probe
set +e
"$@"; rc=$?
echo "exit=$rc"; echo "--- Android log:"; strings -n4 /dev/log/main | tail -8
