#!/usr/bin/env bash
# For a DDK source tree, print the ioctl index of every bridge call in the stock dispatch table and count mismatches.
# usage: compare-ids.sh <services4 dir> [extra -D flags...]
set -e
S=$1; shift
H=$(dirname "$0")
T=$(mktemp -d)
trap 'rm -rf $T' EXIT
names=$(awk '{gsub(/"/,"",$3); print $3}' "$H/stock-dispatch.txt" | grep PVRSRV_BRIDGE)
{
	echo '#include <stdio.h>'
	echo '#include <linux/ioctl.h>'
	echo 'typedef unsigned int IMG_UINT32; typedef int IMG_INT32; typedef void *IMG_HANDLE; typedef void IMG_VOID;'
	echo '#define PVRSRV_IOWR(INDEX) _IOWR(PVRSRV_IOC_GID, INDEX, PVRSRV_BRIDGE_PACKAGE)'
	echo '#include "pvr_bridge_ids.h"'
	echo 'int main(void) {'
	for n in $names; do
		echo "#ifdef $n"
		echo "printf(\"%-55s %d\\n\", \"$n\", _IOC_NR($n));"
		echo "#else"
		echo "printf(\"%-55s missing\\n\", \"$n\");"
		echo "#endif"
	done
	echo 'return 0; }'
} > $T/ids.c
# only the #define lines of the bridge headers (the structs need the whole DDK)
for f in pvr_bridge.h sgx_bridge.h; do
	p=$(find "$S" -name $f | head -1)
	[ -n "$p" ] && grep -E '^\s*#\s*(define|if|ifdef|ifndef|else|elif|endif)' "$p" | grep -v '_H_\b\|__PVR_BRIDGE_H__\|__SGX_BRIDGE_H__\|#define PVRSRV_IOWR' >> $T/pvr_bridge_ids.h
done
echo 'typedef struct { int x; } PVRSRV_BRIDGE_PACKAGE;' | cat - $T/pvr_bridge_ids.h > $T/h && mv $T/h $T/pvr_bridge_ids.h
gcc -w "$@" -I$T -o $T/ids $T/ids.c
$T/ids > $T/out
paste <(awk '{gsub(/"/,"",$3); print $1, $3}' "$H/stock-dispatch.txt" | grep PVRSRV_BRIDGE) $T/out | awk '{ if ($1 != $4) bad++; print } END { printf "mismatches: %d of %d\n", bad, NR > "/dev/stderr" }' > $T/cmp
cat $T/cmp
