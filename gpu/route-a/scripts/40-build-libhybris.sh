#!/usr/bin/env bash
# Build libhybris for glibc armhf so that ordinary Linux programs can use the stock Android GPU libraries,
# plus the glibc runtime, hybris-bionic-init (see tools/hybris-bionic-init.c), libgbm on gralloc with its
# libhybris EGL platform (gbm-hybris/, for weston's DRM backend), the GL_EXT_unpack_subimage shim and
# test programs.
# Output: $HYBRIS_STAGE (installed as /opt/hybris by 30-install-tablet.sh)
set -euo pipefail
. "$(dirname "$0")/route-a-env.sh"

SRC="$RA_WORK/libhybris" HDR="$RA_WORK/android-headers" B="$RA_WORK/hybris-build" S="$HYBRIS_STAGE"
mkdir -p "$RA_WORK"
[ -d "$SRC/.git" ] || git clone -q "$HYBRIS_REPO" "$SRC"
git -C "$SRC" checkout -q "$HYBRIS_REV"
[ -d "$HDR/.git" ] || git clone -q --filter=blob:none --no-checkout "$AHDR_REPO" "$HDR"
git -C "$HDR" cat-file -e "$AHDR_REV^{commit}" 2>/dev/null || git -C "$HDR" fetch -q origin
rm -rf "$RA_WORK/headers-19" && mkdir -p "$RA_WORK/headers-19"
git -C "$HDR" archive "$AHDR_REV" 19 | tar -x -C "$RA_WORK/headers-19" --strip-components=1

# Wayland (client, server, wayland-egl) and libdrm from Debian bookworm, the same versions as weston's
D="$RA_WORK/deb-root"
rm -rf "$D" && mkdir -p "$D"
for d in $(fetch_debs -n libwayland-dev libwayland-egl-backend-dev libwayland-client0 libwayland-server0 libwayland-cursor0 libwayland-egl1 \
		libffi8 libffi-dev libdrm-dev libdrm2); do
	extract_deb "$d" "$D"
done
DL="$D/usr/lib/arm-linux-gnueabihf"

(cd "$SRC/hybris" && NOCONFIGURE=1 ./autogen.sh)
rm -rf "$B" && mkdir -p "$B"
# Wayland support: weston's clients get their buffers through it, and the compositor side (EGL_WL_bind_wayland_display)
# is in the platform code shared by all libhybris EGL platforms. The host's wayland-scanner generates the protocol code,
# into the build directory, which the Wayland platform does not search on its own (CPPFLAGS).
(cd "$B" && PKG_CONFIG_LIBDIR="$DL/pkgconfig:$D/usr/share/pkgconfig" PKG_CONFIG_SYSROOT_DIR="$D" \
	LDFLAGS="-Wl,-rpath-link,$DL" CPPFLAGS="-I$B/platforms/common" "$SRC/hybris/configure" --host="${CROSS_COMPILE%-}" --prefix=/opt/hybris \
	--with-android-headers="$RA_WORK/headers-19" --enable-arch=arm --enable-wayland \
	--with-default-hybris-ld-library-path=/system/vendor/lib:/system/lib --with-default-egl-platform=null)
# a failed pkg-config check makes this configure exit with status 0
[ -f "$B/Makefile" ] || { echo "libhybris configure failed, see $B/config.log" >&2; exit 1; }
# the libraries only: the tests and utils do not link against this glibc
# (a SUBDIRS= override on the command line would reach every sub-make, so build the directories one by one)
for d in include common properties hardware ui gralloc libsync platforms egl glesv1 glesv2; do
	make -C "$B/$d" -j"$(nproc)"
done

rm -rf "$S" && mkdir -p "$S/lib/libhybris/linker" "$S/bin"
find "$B" -path '*/.libs/*.so.*' -type f -exec cp {} "$S/lib/" \;
cp "$B"/egl/platforms/*/.libs/eglplatform_*.so "$S/lib/libhybris/"
cp "$B/common/jb/.libs/jb.so" "$S/lib/libhybris/linker/"
for f in "$S"/lib/*.so.*; do
	soname="$("${CROSS_COMPILE}readelf" -d "$f" | sed -n 's/.*SONAME.*\[\(.*\)\]/\1/p')"
	[ "$soname" = "$(basename "$f")" ] || ln -sf "$(basename "$f")" "$S/lib/$soname"
	ln -sf "$soname" "$S/lib/${soname%%.so*}.so"
done

# glibc runtime from the toolchain (Alpine is musl)
for f in ld-linux-armhf.so.3 libc.so.6 libdl.so.2 libm.so.6 libpthread.so.0 librt.so.1 libgcc_s.so.1; do
	cp -L "$TC_SYSROOT/lib/$f" "$S/lib/"
done
cp -L "$TC_SYSROOT/usr/lib/libstdc++.so.6" "$S/lib/"

for f in libdrm.so.2 libwayland-client.so.0 libwayland-server.so.0 libwayland-cursor.so.0 libwayland-egl.so.1 libffi.so.8; do
	cp -L "$DL/$f" "$S/lib/"
done

CC="${CROSS_COMPILE}gcc -O2 -Wall" T="$RA_DIR/tools" G="$RA_DIR/gbm-hybris" LD="-L$S/lib -Wl,-rpath-link,$S/lib"
I="-I$SRC/hybris/include"
IGBM="$I -I$RA_WORK/headers-19 -I$SRC/hybris/egl -I$SRC/hybris/egl/platforms/common -I$G"
IDRM="-I$D/usr/include -I$D/usr/include/libdrm"
# shellcheck disable=SC2086
{
$CC -shared -fPIC -o "$S/lib/libhybris-bionic-init.so" "$T/hybris-bionic-init.c" $LD -lhybris-common
$CC -shared -fPIC $IGBM -Wl,-soname,libgbm.so.1 -o "$S/lib/libgbm.so.1" "$G/gbm-hybris.c" \
	$LD -lhybris-common -lgralloc -ldl -lpthread
ln -sf libgbm.so.1 "$S/lib/libgbm.so"
$CC -shared -fPIC $IGBM -o "$S/lib/libhybris/eglplatform_gbm.so" "$G/eglplatform_gbm.c" \
	$LD -lgbm -lhybris-eglplatformcommon -lgralloc
$CC -shared -fPIC $I -o "$S/lib/libgles-unpack-shim.so" "$GPU_DIR/tools/gles-unpack-shim.c" -ldl
$CC $I -o "$S/bin/hybris-gles-probe" "$T/stock-gles-probe.c" $LD -lEGL -lGLESv2
$CC $I -o "$S/bin/hybris-kms-gles" "$T/stock-kms-gles.c" $LD -lhardware -lEGL -lGLESv2 -lm
$CC $I -I"$G" $IDRM -o "$S/bin/kms-gles" "$GPU_DIR/tools/kms-gles.c" $LD -lEGL -lGLESv2 -lgbm "$S/lib/libdrm.so.2" -lm
}

cat > "$S/hybris-run" <<'EOS'
#!/bin/sh
# Run a glibc program on the stock Android GPU libraries through libhybris (start the stock services first:
# /opt/pvr-stock/stock-run true). GBM programs (weston, kms-gles) need HYBRIS_EGLPLATFORM=gbm.
# hybris-bionic-init must be the first preload: it reserves the Android TLS slots.
P="/opt/hybris/lib/libhybris-bionic-init.so /opt/hybris/lib/libgbm.so.1"
# the TI stack may already preload the same shim for every glibc program
grep -qs gles-unpack-shim /etc/ld.so.preload || P="$P /opt/hybris/lib/libgles-unpack-shim.so"
export LD_LIBRARY_PATH=/opt/hybris/lib:/usr/lib/arm-linux-gnueabihf
exec /opt/hybris/lib/ld-linux-armhf.so.3 --preload "$P" "$@"
EOS
chmod +x "$S/hybris-run"
echo "libhybris staged in $S"
