# shellcheck shell=bash
# Common settings for the GPU scripts. Source it, do not run it.
#
# Environment (all optional):
#   GPU_WORK      work directory for clones, sysroot, builds (default: gpu/work, git-ignored)
#   KERNEL_BUILD  out-of-tree build directory of the running kernel (default: kernel/build, as in scripts/30-build-kernel.sh)
#   TABLET        ssh target (default: root@172.16.42.1)
#   TABLET_PASS   ssh password for sshpass (default: exper); empty to use keys
#   TOOLCHAIN_DIR / CROSS_COMPILE as in scripts/00-env.sh; it must be a *glibc* armhf toolchain
#   DEBIAN_MIRROR Debian mirror for the bookworm packages (default: http://deb.debian.org/debian)

GPU_SCRIPTS="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
# shellcheck source=../../scripts/00-env.sh
. "$GPU_SCRIPTS/../../scripts/00-env.sh" >/dev/null
export GPU_DIR="$PRJ/gpu"
export GPU_WORK="${GPU_WORK:-$GPU_DIR/work}"
export KERNEL_BUILD="${KERNEL_BUILD:-$PRJ/kernel/build}"
export TABLET="${TABLET:-root@172.16.42.1}"
export TABLET_PASS="${TABLET_PASS-exper}"
export DEBIAN_MIRROR="${DEBIAN_MIRROR:-http://deb.debian.org/debian}"

# pinned upstream sources
export KM_REPO=https://git.ti.com/git/graphics/omap5-sgx-ddk-linux.git
export KM_BRANCH=1.17.4948957/mesa/k6.1
export KM_REV=9ae0fa4998b1c624408945e062bf8fb0ea7efb9d
export MESA_REPO=https://gitlab.freedesktop.org/StaticRocket/mesa.git
export MESA_BRANCH=powervr/24.0.1
export MESA_REV=7c82c1eebc67f5a62a347a84d42fe795cf7f523b

export SYSROOT="$GPU_WORK/sysroot"
export STAGE="$GPU_WORK/stage"		# what goes to /opt/pvr on the tablet
export DEBS="$GPU_WORK/debs"

CC_GLIBC="${CROSS_COMPILE}gcc"
TC_SYSROOT="$($CC_GLIBC -print-sysroot 2>/dev/null)"
export CC_GLIBC TC_SYSROOT

tablet_ssh() {
	if [ -n "$TABLET_PASS" ]; then sshpass -p "$TABLET_PASS" ssh -o ConnectTimeout=5 "$TABLET" "$@"
	else ssh -o ConnectTimeout=5 "$TABLET" "$@"; fi
}
tablet_scp() {
	if [ -n "$TABLET_PASS" ]; then sshpass -p "$TABLET_PASS" scp -q "$@"
	else scp -q "$@"; fi
}

# fetch_debs [-n] pkg...: download bookworm armhf packages (and dependencies unless -n) into $DEBS
fetch_debs() {
	local f
	mkdir -p "$DEBS"
	if [ ! -f "$DEBS/Packages" ]; then
		curl -sfL "$DEBIAN_MIRROR/dists/bookworm/main/binary-armhf/Packages.xz" | xz -d > "$DEBS/Packages.tmp"
		mv "$DEBS/Packages.tmp" "$DEBS/Packages"
	fi
	python3 -I "$GPU_DIR/tools/deb-closure.py" "$DEBS/Packages" "$@" | while read -r _ _ f; do
		[ -f "$DEBS/$(basename "$f")" ] || curl -sfL -o "$DEBS/$(basename "$f")" "$DEBIAN_MIRROR/$f"
		echo "$DEBS/$(basename "$f")"
	done
}

# extract_deb FILE DIR: unpack a .deb's data into DIR
extract_deb() {
	local t
	t="$(mktemp -d)"
	bsdtar -xf "$1" -C "$t" && bsdtar -xf "$t"/data.tar.* -C "$2"
	rm -rf "$t"
}
