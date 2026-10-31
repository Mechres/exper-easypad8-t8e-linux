#!/bin/sh
# Stock Allwinner DDK 1.12 GPU user space (/system, bionic) on Linux: start/stop the GPU services or run a program.
#   stock-run start        load the route A modules and start the stock GPU services (the OpenRC "pvr-stock"
#                          service does this at boot); swaps the TI 1.17 stack out if it is running
#   stock-run stop         stop the services and unload the modules
#   stock-run restore      stop, then start the TI stack again (if installed)
#   stock-run [program args...]   start if needed, run the program (default: the offscreen GLES probe)
set -e
D=/opt/pvr-stock
export LD_LIBRARY_PATH=/system/vendor/lib:/system/lib

started() { grep -q "^dcnohw " /proc/modules; }

start() {
	started && return 0
	# bionic maps the property area unchecked; Android's log devices become plain files
	[ -e /dev/__properties__ ] || { cp $D/__properties__ /dev/__properties__; chmod 0444 /dev/__properties__; }
	mkdir -p /dev/log
	for b in main system radio events; do [ -e /dev/log/$b ] || : > /dev/log/$b; chmod 666 /dev/log/$b; done
	grep -q "^ion_compat " /proc/modules || insmod $D/ion_compat.ko
	# the TI module has the same name (udev may have loaded it from the device tree node)
	if [ -e /etc/init.d/pvr ] && rc-service pvr status >/dev/null 2>&1; then rc-service pvr stop >/dev/null 2>&1 || true; fi
	rmmod pvrsrvkm 2>/dev/null || true
	insmod $D/pvrsrvkm.ko
	insmod $D/dcnohw.ko
	(cd /system/vendor/bin && ./pvrsrvctl --start --no-module)
}

stop() {
	if started; then (cd /system/vendor/bin && ./pvrsrvctl --stop --no-module) >/dev/null 2>&1 || true; fi
	rmmod dcnohw 2>/dev/null || true
	rmmod pvrsrvkm 2>/dev/null || true
	rmmod ion_compat 2>/dev/null || true
}

case "$1" in
start) start; exit ;;
stop) stop; exit ;;
restore)
	stop
	if [ -e /etc/init.d/pvr ]; then rc-service pvr start >/dev/null && echo "TI 1.17 stack restored"; fi
	exit 0 ;;
esac

started || { start; echo "stock services started"; }
[ $# -gt 0 ] || set -- /system/bin/stock-gles-probe
set +e
"$@"; rc=$?
echo "exit=$rc"; echo "--- Android log:"; strings -n4 /dev/log/main | tail -8
