#!/usr/bin/env bash
# Build the small helpers in gpu/tools against the staged Mesa (run 20-build-mesa.sh first):
#   libpvr-sunxi-shim.so  presents sun4i-drm as "omapdrm" to TI's libsrv_um
#   libgles-unpack-shim.so emulates GL_EXT_unpack_subimage (weston >= 10 needs it on GLES 2.0)
#   libsegv-report.so     preload crash reporter (no gdb on the tablet)
#   gles-probe, kms-gles  offscreen and on-screen GLES tests
set -euo pipefail
. "$(dirname "$0")/gpu-env.sh"

T="$GPU_DIR/tools"
O="$STAGE"
mkdir -p "$O/lib" "$O/bin"
CC="$CC_GLIBC --sysroot=$SYSROOT -O2 -Wall"
INC="-I$SYSROOT/opt/pvr/include -I$SYSROOT/usr/include/libdrm"
LIBS="-L$SYSROOT/opt/pvr/lib -Wl,-rpath-link,$SYSROOT/opt/pvr/lib"

# shellcheck disable=SC2086
{
$CC -shared -fPIC -o "$O/lib/libpvr-sunxi-shim.so" "$T/pvr-sunxi-shim.c" $INC -ldl
$CC -shared -fPIC -o "$O/lib/libgles-unpack-shim.so" "$T/gles-unpack-shim.c" $INC -ldl
$CC -shared -fPIC -funwind-tables -o "$O/lib/libsegv-report.so" "$T/segv-report.c" -ldl
$CC -o "$O/bin/gles-probe" "$T/gles-probe.c" $INC $LIBS -lEGL -lGLESv2 -lgbm
$CC -o "$O/bin/kms-gles" "$T/kms-gles.c" $INC $LIBS -lEGL -lGLESv2 -lgbm -ldrm -lm
}
echo "tools built into $O"
