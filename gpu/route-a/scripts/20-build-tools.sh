#!/usr/bin/env bash
# Build the bionic (Android) test programs with the NDK. They link against the stock libEGL/libGLESv2
# (NDK stubs) and libhardware (a stub built here), so no stock files are needed at build time.
# Output: $RA_WORK/bin/{stock-gles-probe,fb-hal-probe,stock-window-probe,stock-kms-gles}
set -euo pipefail
. "$(dirname "$0")/route-a-env.sh"

: "${NDK_DIR:?set NDK_DIR to an Android NDK (r25c tested)}"
CC="$NDK_DIR/toolchains/llvm/prebuilt/linux-x86_64/bin/armv7a-linux-androideabi19-clang"
[ -x "$CC" ] || { echo "$CC not found" >&2; exit 1; }
B="$RA_WORK/bin" STUB="$RA_WORK/stub"
mkdir -p "$B" "$STUB"

# link-time stand-in for the stock /system/lib/libhardware.so (only the symbol is needed)
echo 'int hw_get_module(const char *id, const void **module) { return -1; }' > "$STUB/hardware.c"
"$CC" -shared -o "$STUB/libhardware.so" "$STUB/hardware.c"

T="$RA_DIR/tools"
"$CC" -O2 -Wall -o "$B/stock-gles-probe" "$T/stock-gles-probe.c" -lEGL -lGLESv2
"$CC" -O2 -Wall -o "$B/fb-hal-probe" "$T/fb-hal-probe.c" -L"$STUB" -lhardware
"$CC" -O2 -Wall -o "$B/stock-window-probe" "$T/stock-window-probe.c" -L"$STUB" -lhardware -lEGL -lGLESv2
"$CC" -O2 -Wall -o "$B/stock-kms-gles" "$T/stock-kms-gles.c" -L"$STUB" -lhardware -lEGL -lGLESv2 -lm
echo "built $(ls "$B" | tr '\n' ' ')in $B"
