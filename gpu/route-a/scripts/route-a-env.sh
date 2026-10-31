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
