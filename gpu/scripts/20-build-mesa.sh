#!/usr/bin/env bash
# Build TI's Mesa fork (the "sgx" Gallium driver on top of TI's libpvr_dri_support.so) for glibc armhf:
# EGL (GBM + Wayland platforms), GBM, GLES1/2, with "sun4i-drm" as a driver alias.
#   - sysroot: the toolchain's glibc sysroot + Debian bookworm armhf -dev packages
#   - host: meson, ninja, python3 (a venv with mako is created), wayland-scanner, curl, xz, bsdtar
# Output: $STAGE (= /opt/pvr on the tablet), $SYSROOT/opt/pvr (for building clients)
set -euo pipefail
. "$(dirname "$0")/gpu-env.sh"

DEV_PKGS="libdrm2 libdrm-dev libwayland-client0 libwayland-server0 libwayland-cursor0 libwayland-egl1 libwayland-dev
	libwayland-egl-backend-dev wayland-protocols libffi8 libffi-dev zlib1g zlib1g-dev"

# --- sysroot ---
if [ ! -f "$SYSROOT/.done" ]; then
	[ -n "$TC_SYSROOT" ] && [ -f "$TC_SYSROOT/usr/include/stdio.h" ] || { echo "cannot find the toolchain's glibc sysroot" >&2; exit 1; }
	rm -rf "$SYSROOT"
	cp -a "$TC_SYSROOT" "$SYSROOT"
	X="$(mktemp -d)"
	# shellcheck disable=SC2086
	for d in $(fetch_debs -n $DEV_PKGS); do extract_deb "$d" "$X"; done
	cp -a "$X"/usr/include/. "$SYSROOT"/usr/include/
	[ -d "$X/usr/include/arm-linux-gnueabihf" ] && cp -an "$X"/usr/include/arm-linux-gnueabihf/. "$SYSROOT"/usr/include/
	for l in "$X"/usr/lib/arm-linux-gnueabihf "$X"/lib/arm-linux-gnueabihf; do
		[ -d "$l" ] && cp -a "$l"/. "$SYSROOT"/usr/lib/
	done
	mkdir -p "$SYSROOT"/usr/share
	cp -a "$X"/usr/share/pkgconfig "$X"/usr/share/wayland-protocols "$SYSROOT"/usr/share/
	rm -rf "$X"
	# Debian's absolute .so links and multiarch libdir in the .pc files -> sysroot-relative
	(cd "$SYSROOT/usr/lib" && for l in $(find . -maxdepth 1 -type l); do
		case "$(readlink "$l")" in /*) ln -sf "$(basename "$(readlink "$l")")" "$l";; esac; done)
	sed -i 's|^libdir=.*|libdir=${prefix}/lib|; s|^includedir=.*/arm-linux-gnueabihf$|includedir=${prefix}/include|' \
		"$SYSROOT"/usr/lib/pkgconfig/*.pc
	rm -f "$SYSROOT/usr/lib/pkgconfig/wayland-scanner.pc"	# use the build machine's
	touch "$SYSROOT/.done"
fi

# --- python venv for mesa's build-time scripts ---
[ -x "$GPU_WORK/venv/bin/python" ] || python3 -m venv "$GPU_WORK/venv"
"$GPU_WORK/venv/bin/pip" -q install mako pyyaml packaging
export PATH="$GPU_WORK/venv/bin:$PATH"

# --- cross file ---
CROSS="$GPU_WORK/cross-armhf.txt"
cat > "$CROSS" <<EOF
[binaries]
c = '$(command -v "${CROSS_COMPILE}gcc")'
cpp = '$(command -v "${CROSS_COMPILE}g++")'
ar = '$(command -v "${CROSS_COMPILE}ar")'
strip = '$(command -v "${CROSS_COMPILE}strip")'
pkg-config = 'pkg-config'

[properties]
sys_root = '$SYSROOT'
pkg_config_libdir = '$SYSROOT/opt/pvr/lib/pkgconfig:$SYSROOT/usr/lib/pkgconfig:$SYSROOT/usr/share/pkgconfig'

[built-in options]
c_args = ['--sysroot=$SYSROOT', '-I$SYSROOT/opt/pvr/include', '-march=armv7-a', '-mfpu=neon', '-mfloat-abi=hard']
cpp_args = ['--sysroot=$SYSROOT', '-I$SYSROOT/opt/pvr/include', '-march=armv7-a', '-mfpu=neon', '-mfloat-abi=hard']
c_link_args = ['--sysroot=$SYSROOT', '-Wl,-rpath-link,$SYSROOT/opt/pvr/lib']
cpp_link_args = ['--sysroot=$SYSROOT', '-Wl,-rpath-link,$SYSROOT/opt/pvr/lib']

[host_machine]
system = 'linux'
cpu_family = 'arm'
cpu = 'armv7'
endian = 'little'
EOF

# --- mesa ---
MESA="$GPU_WORK/mesa"
[ -d "$MESA/.git" ] || git clone -q --depth 1 -b "$MESA_BRANCH" "$MESA_REPO" "$MESA"
cd "$MESA"
git cat-file -e "$MESA_REV^{commit}" 2>/dev/null || git fetch -q --depth 1 origin "$MESA_REV"
git checkout -q -B t8e "$MESA_REV"
git -c user.name=build -c user.email=build@localhost am -q "$GPU_DIR"/mesa/*.patch

rm -rf "$GPU_WORK/build-mesa"
meson setup "$GPU_WORK/build-mesa" --cross-file "$CROSS" --prefix=/opt/pvr --libdir=lib --buildtype=release \
	-Dgallium-drivers=sgx -Dgallium-sgx-alias=sun4i-drm -Dvulkan-drivers= -Dplatforms=wayland \
	-Dglx=disabled -Degl=enabled -Dgbm=enabled -Dgles1=enabled -Dgles2=enabled -Dopengl=true \
	-Dllvm=disabled -Dshared-glapi=enabled -Dxmlconfig=disabled -Dzstd=disabled -Dexpat=disabled \
	-Dvalgrind=disabled -Dlibunwind=disabled -Dlmsensors=disabled -Dbuild-tests=false \
	-Dgallium-vdpau=disabled -Dgallium-va=disabled -Dgallium-xa=disabled -Dgallium-nine=false \
	-Dgallium-opencl=disabled -Dandroid-libbacktrace=disabled
ninja -C "$GPU_WORK/build-mesa"
rm -rf "$GPU_WORK/mesa-stage"
DESTDIR="$GPU_WORK/mesa-stage" meson install -C "$GPU_WORK/build-mesa" --quiet
rm -rf "$SYSROOT/opt/pvr"
mkdir -p "$SYSROOT/opt" "$STAGE/lib"
cp -a "$GPU_WORK/mesa-stage/opt/pvr" "$SYSROOT/opt/pvr"
cp -a "$GPU_WORK/mesa-stage/opt/pvr/lib/." "$STAGE/lib/"
rm -rf "$STAGE/lib/pkgconfig"
echo "mesa staged in $STAGE"
