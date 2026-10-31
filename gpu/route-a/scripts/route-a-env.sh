# shellcheck shell=bash
# Common settings for the route A scripts (stock Allwinner DDK 1.12 user space). Source it, do not run it.
#
# Environment (all optional, plus everything in gpu/scripts/gpu-env.sh):
#   RA_WORK   work directory (default: $GPU_WORK/route-a)
#   NDK_DIR   Android NDK (r25c tested) for the bionic test programs

RA_SCRIPTS="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
# shellcheck source=../../scripts/gpu-env.sh
. "$RA_SCRIPTS/../../scripts/gpu-env.sh"
export RA_DIR="$GPU_DIR/route-a"
export RA_WORK="${RA_WORK:-$GPU_WORK/route-a}"

# pinned upstream source: openpvrsgx DDK 1.14.3841212 kernel driver (Allwinner drop, dual MIT/GPLv2)
export OPVR_REPO=https://github.com/openpvrsgx-devgroup/linux_openpvrsgx.git
export OPVR_BRANCH=letux-pvrsrvkm-6.0-rc7
export OPVR_REV=17a0cbad71d3a44dffcee6a801ff5e8be329272a
export OPVR_PATH=drivers/gpu/drm/pvrsgx/1.14.3841212/eurasia_km

# libhybris (Apache-2.0) and the Android 4.4.2 headers it builds against
export HYBRIS_REPO=https://github.com/libhybris/libhybris.git
export HYBRIS_REV=7079712a42ea2754adf747e70c6cc75764c8596e
export AHDR_REPO=https://github.com/Halium/android-headers.git
export AHDR_REV=db53380e6ee133e81f0f831599bf956550173a7f	# directory 19/ = Android 4.4.2
export HYBRIS_STAGE="$RA_WORK/hybris-stage"	# what goes to /opt/hybris on the tablet
