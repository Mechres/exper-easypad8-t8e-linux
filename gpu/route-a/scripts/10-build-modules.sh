#!/usr/bin/env bash
# Build the route A kernel modules against the running kernel's build directory:
#   pvrsrvkm.ko   openpvrsgx DDK 1.14.3841212 services + gpu/route-a/kernel-module/*.patch: Linux 7.2 port,
#                 stock Allwinner DDK 1.12@2681055 user-space ABI, sunxi system layer, dma-buf ION import, /dev/pvr_sync
#   dcnohw.ko     display class without hardware (the stock services need one display-class device)
#   ion_compat.ko legacy Android /dev/ion on top of dma-buf (gpu/route-a/ion-compat)
# Output: $RA_WORK/{pvrsrvkm,dcnohw,ion_compat}.ko
set -euo pipefail
. "$(dirname "$0")/route-a-env.sh"

[ -f "$KERNEL_BUILD/include/config/kernel.release" ] || { echo "no kernel build in $KERNEL_BUILD (set KERNEL_BUILD)" >&2; exit 1; }
SRC="$RA_WORK/linux_openpvrsgx" KM="$RA_WORK/km114" ION="$RA_WORK/ion-compat"
mkdir -p "$RA_WORK"

# partial sparse clone: only the one driver directory of this kernel tree
if [ ! -d "$SRC/.git" ]; then
	git clone -q --filter=blob:none --no-checkout -b "$OPVR_BRANCH" "$OPVR_REPO" "$SRC"
	git -C "$SRC" sparse-checkout set "$OPVR_PATH"
fi
git -C "$SRC" cat-file -e "$OPVR_REV^{commit}" 2>/dev/null || git -C "$SRC" fetch -q origin "$OPVR_REV"
git -C "$SRC" checkout -q "$OPVR_REV"

rm -rf "$KM" && cp -a "$SRC/$OPVR_PATH" "$KM"
git -C "$KM" init -q
git -C "$KM" add -A
git -C "$KM" -c user.name=build -c user.email=build@localhost commit -qm "openpvrsgx 1.14.3841212 eurasia_km"
git -C "$KM" -c user.name=build -c user.email=build@localhost am -q "$RA_DIR"/kernel-module/*.patch

rm -rf "$ION" && cp -a "$RA_DIR/ion-compat" "$ION"
for d in "$KM" "$ION"; do
	make -C "$KERNEL_BUILD" M="$d" ARCH=arm CROSS_COMPILE="$CROSS_COMPILE" -j"$(nproc)" modules
done
cp "$KM/pvrsrvkm.ko" "$KM/dcnohw.ko" "$ION/ion_compat.ko" "$RA_WORK/"
echo "built $RA_WORK/{pvrsrvkm,dcnohw,ion_compat}.ko for $(modinfo -F vermagic "$RA_WORK/pvrsrvkm.ko" | cut -d' ' -f1)"
