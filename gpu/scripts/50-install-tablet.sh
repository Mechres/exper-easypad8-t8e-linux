#!/usr/bin/env bash
# Install the SGX544 GPU stack on the tablet (over ssh).
#
# usage: TI_UM_DIR=/path/to/ti572x_linux/lws-generic/release 50-install-tablet.sh [--weston] [--dtb FILE]
#
# TI_UM_DIR must point to TI's SGX DDK 1.17.4948957 user space for the ti572x target
# (git.ti.com/graphics/omap5-sgx-ddk-um-linux, branch 1.17.4948957/mesa/glibc-2.35,
# targetfs/ti572x_linux/lws-generic/release). It is NOT part of this repository and this script does not
# download it: TI licenses those binaries "for use only with TI Devices", and the A31s is not one.
# See gpu/README.md before using it.
#
# Installs:
#   /lib/modules/<release>/extra/pvrsrvkm.ko (udev loads it from the DT node), depmod
#   /opt/pvr: the toolchain's glibc runtime, Debian libdrm2, TI user space, Mesa, the shims and test tools
#   /opt/pvr/pvr-run (glibc program), /etc/ld.so.preload (shims, read by glibc only), /etc/powervr.ini
#   OpenRC "pvr" service (enabled): runs pvrsrvctl after the module is loaded
#   --weston: weston 10 (40-fetch-weston.sh) in /usr/lib/arm-linux-gnueabihf, /opt/pvr/weston-run,
#             /lib/ld-linux-armhf.so.3, OpenRC "weston" service (not enabled)
#   --dtb FILE: FILE becomes the default boot DTB (/boot/sun6i-a31s-exper-t8e-drm-new.dtb, old one kept as .pre-gpu)
set -euo pipefail
. "$(dirname "$0")/gpu-env.sh"

WESTON=0 DTB=
while [ $# -gt 0 ]; do
	case "$1" in
	--weston) WESTON=1 ;;
	--dtb) DTB=$2; shift ;;
	*) echo "unknown option $1" >&2; exit 1 ;;
	esac
	shift
done

: "${TI_UM_DIR:?set TI_UM_DIR to the TI ti572x_linux user space directory, not included: see gpu/README.md}"
[ -x "$TI_UM_DIR/usr/bin/pvrsrvctl" ] || { echo "$TI_UM_DIR/usr/bin/pvrsrvctl not found" >&2; exit 1; }
[ -f "$GPU_WORK/pvrsrvkm.ko" ] && [ -d "$STAGE/lib" ] || { echo "run 10-, 20- and 30-*.sh first" >&2; exit 1; }

O="$(mktemp -d)"
trap 'rm -rf "$O"' EXIT
mkdir -p "$O/opt/pvr/lib" "$O/opt/pvr/bin" "$O/etc/init.d"

for f in ld-linux-armhf.so.3 libc.so.6 libdl.so.2 libm.so.6 libpthread.so.0 librt.so.1 libutil.so.1 libgcc_s.so.1; do
	cp -L "$TC_SYSROOT/lib/$f" "$O/opt/pvr/lib/"
done
cp -L "$TC_SYSROOT/usr/lib/libstdc++.so.6" "$SYSROOT/usr/lib/libdrm.so.2" "$O/opt/pvr/lib/"
cp -a "$TI_UM_DIR/usr/lib/." "$O/opt/pvr/lib/"
cp "$TI_UM_DIR/usr/bin/pvrsrvctl" "$O/opt/pvr/bin/"
cp -a "$STAGE/." "$O/opt/pvr/"

printf '#!/bin/sh\n# run a glibc program from /opt/pvr on musl Alpine\nexec /opt/pvr/lib/ld-linux-armhf.so.3 --library-path /opt/pvr/lib "$@"\n' > "$O/opt/pvr/pvr-run"
chmod +x "$O/opt/pvr/pvr-run"
# glibc reads /etc/ld.so.preload, musl does not: the shims reach every glibc program, never a musl one
printf '/opt/pvr/lib/libpvr-sunxi-shim.so\n/opt/pvr/lib/libgles-unpack-shim.so\n' > "$O/etc/ld.so.preload"
# the default parameter buffer runs out on complex scenes ("TA went out of Mem and SPM occurred")
printf '[default]\nParamBufferSize=33554432\nMaxParamBufferSize=67108864\n' > "$O/etc/powervr.ini"

cat > "$O/etc/init.d/pvr" <<'EOF'
#!/sbin/openrc-run
# PowerVR SGX544 services: load pvrsrvkm and run the SGX initialisation (boots the GPU microkernel)

description="PowerVR SGX GPU services"

depend() {
	need localmount
	after udev-trigger udev-settle modules
}

start() {
	ebegin "Starting PowerVR SGX services"
	modprobe pvrsrvkm && /opt/pvr/pvr-run /opt/pvr/bin/pvrsrvctl --start --no-module >/dev/null
	eend $?
}

stop() {
	ebegin "Stopping PowerVR SGX services"
	/opt/pvr/pvr-run /opt/pvr/bin/pvrsrvctl --stop --no-module >/dev/null 2>&1
	modprobe -r pvrsrvkm
	eend $?
}
EOF
chmod +x "$O/etc/init.d/pvr"

if [ "$WESTON" = 1 ]; then
	W="$GPU_WORK/weston-root"
	[ -d "$W/usr/bin" ] || { echo "run 40-fetch-weston.sh first" >&2; exit 1; }
	mkdir -p "$O/usr/lib/arm-linux-gnueabihf" "$O/usr/share/fonts/dejavu-debian"
	cp -a "$W/usr/lib/arm-linux-gnueabihf/." "$W/lib/arm-linux-gnueabihf/." "$O/usr/lib/arm-linux-gnueabihf/"
	rm -rf "$O/usr/lib/arm-linux-gnueabihf/pkgconfig" \
		"$O"/usr/lib/arm-linux-gnueabihf/libweston-10/{rdp-backend,pipewire-plugin,remoting-plugin}.so
	cp -a "$W"/usr/bin/weston* "$O/opt/pvr/bin/"
	cp -a "$W/usr/share/weston" "$O/usr/share/"
	cp "$W"/usr/share/fonts/truetype/dejavu/*.ttf "$O/usr/share/fonts/dejavu-debian/"	# Alpine's bitmap fonts do not render
	cat > "$O/opt/pvr/weston.ini" <<'EOF'
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

# Debian's defaults point at /usr/bin, the binaries live in /opt/pvr/bin here
[launcher]
icon=/usr/share/weston/icon_terminal.png
path=/opt/pvr/bin/weston-terminal

[launcher]
icon=/usr/share/weston/icon_flower.png
path=/opt/pvr/bin/weston-flower

[launcher]
icon=/usr/share/weston/icon_ivi_simple-egl.png
path=/opt/pvr/bin/weston-simple-egl
EOF
	cat > "$O/opt/pvr/weston-run" <<'EOF'
#!/bin/sh
# Start a glibc program (default: weston) in the SGX/Wayland environment.
export LD_LIBRARY_PATH=/opt/pvr/lib:/usr/lib/arm-linux-gnueabihf
# no logind: fail at once instead of a 30 s D-Bus timeout, weston then uses its direct launcher
export DBUS_SYSTEM_BUS_ADDRESS=${DBUS_SYSTEM_BUS_ADDRESS:-unix:path=/nonexistent}
export XDG_RUNTIME_DIR=${XDG_RUNTIME_DIR:-/run/user/0}
export WAYLAND_DISPLAY=${WAYLAND_DISPLAY:-wayland-0}
mkdir -p "$XDG_RUNTIME_DIR" && chmod 700 "$XDG_RUNTIME_DIR"
if [ $# -eq 0 ]; then
	pidof weston >/dev/null || rm -f "$XDG_RUNTIME_DIR/$WAYLAND_DISPLAY" "$XDG_RUNTIME_DIR/$WAYLAND_DISPLAY.lock"
	set -- /opt/pvr/bin/weston --config=/opt/pvr/weston.ini --tty=2 --socket="$WAYLAND_DISPLAY"
fi
exec "$@"
EOF
	chmod +x "$O/opt/pvr/weston-run"
	cat > "$O/etc/init.d/weston" <<'EOF'
#!/sbin/openrc-run
# weston on the SGX (GL renderer) on tty2. Not enabled by default: it takes over the display.

description="Weston Wayland compositor (PowerVR SGX)"
command=/opt/pvr/weston-run
command_background=yes
pidfile=/run/weston.pid
output_log=/var/log/weston.log
error_log=/var/log/weston.log

depend() {
	need pvr
}
EOF
	chmod +x "$O/etc/init.d/weston"
fi

REL="$(tablet_ssh uname -r)"
VERMAGIC="$(modinfo -F vermagic "$GPU_WORK/pvrsrvkm.ko" | cut -d' ' -f1)"
[ "$REL" = "$VERMAGIC" ] || { echo "pvrsrvkm.ko is for $VERMAGIC, the tablet runs $REL" >&2; exit 1; }
mkdir -p "$O/lib/modules/$REL/extra"
cp "$GPU_WORK/pvrsrvkm.ko" "$O/lib/modules/$REL/extra/"

tar czf "$O.tgz" -C "$O" .
tablet_scp "$O.tgz" "$TABLET:/root/pvr-install.tgz"
rm -f "$O.tgz"
tablet_ssh "rm -rf /opt/pvr && tar xzf /root/pvr-install.tgz -C / && rm /root/pvr-install.tgz && depmod $REL && rc-update add pvr default >/dev/null"
[ "$WESTON" = 1 ] && tablet_ssh 'ln -sf /opt/pvr/lib/ld-linux-armhf.so.3 /lib/ld-linux-armhf.so.3'
if [ -n "$DTB" ]; then
	tablet_scp "$DTB" "$TABLET:/root/gpu-default.dtb"
	tablet_ssh 'D=/boot/sun6i-a31s-exper-t8e-drm-new.dtb; mount -o remount,rw /boot && { [ -e $D.pre-gpu ] || cp $D $D.pre-gpu; } && cp /root/gpu-default.dtb $D && sync; mount -o remount,ro /boot'
fi
echo "installed; reboot (or: rc-service pvr start), then try: /opt/pvr/pvr-run /opt/pvr/bin/kms-gles 10"
