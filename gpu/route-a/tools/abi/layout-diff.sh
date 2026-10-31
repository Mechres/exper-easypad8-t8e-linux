#!/usr/bin/env bash
# Member-level layout comparison (name, offset, size of every member) between the stock pvrsrvkm.ko and a build,
# for the user-space and microkernel interface structures.
# usage: layout-diff.sh STOCK.ko OURS.ko
set -e
S=$1 N=$2
T=$(mktemp -d); trap 'rm -rf $T' EXIT
pat='BRIDGE_(IN|OUT)|^_?SGXMKIF|SGX_BRIDGE|SGX_CCB_KICK|_SGX_KICK|TRANSFER_SGX_KICK|_2D_SGX_KICK|CLIENT_MEM_INFO|CLIENT_SYNC_INFO|KERNEL_MEM_INFO|KERNEL_SYNC_INFO|SYNC_DATA|MISC_INFO|SGX_CLIENT_INFO|SGX_MISCINFO|HEAP_INFO|SGX_INIT|SGX_INTERNAL|PVRSRV_DEVICE_IDENTIFIER|SGX_HWPERF|DISPLAY_|BUFFER_INFO|DC_|BC_'
pahole --sizes "$S" 2>/dev/null | awk '{print $1}' | grep -E "$pat" | sort > $T/s
pahole --sizes "$N" 2>/dev/null | awk '{print $1}' | grep -E "$pat" | sort > $T/n
norm() { pahole -C "$1" "$2" 2>/dev/null | grep -E '/\* +[0-9]+ +[0-9]+ \*/' | sed -E 's/^\s+//; s/\s+/ /g' | sed -E 's/^(.*) (\w+)(\[[0-9]*\])?;.*\/\* ([0-9]+) +([0-9]+) \*\/.*/\2\3 @\4 +\5/'; }
bad=0 ok=0
for t in $(comm -12 $T/s $T/n); do
	if ! diff -q <(norm $t "$S") <(norm $t "$N") >/dev/null; then
		bad=$((bad+1)); echo "=== $t"; diff <(norm $t "$S") <(norm $t "$N") | grep '^[<>]' | head -8
	else ok=$((ok+1)); fi
done
echo "identical: $ok, different: $bad"
echo "only in stock: $(comm -23 $T/s $T/n | tr '\n' ' ')"
