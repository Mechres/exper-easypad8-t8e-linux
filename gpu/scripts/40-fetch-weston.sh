#!/usr/bin/env bash
# Download Debian bookworm's armhf weston 10 and its runtime libraries (prebuilt, glibc) and unpack
# them into $GPU_WORK/weston-root. FreeRDP, PipeWire and GStreamer are left out: only optional weston
# backends/plugins use them.
set -euo pipefail
. "$(dirname "$0")/gpu-env.sh"

W="$GPU_WORK/weston-root"
rm -rf "$W"
mkdir -p "$W"
for d in $(fetch_debs -x libfreerdp2-2,libwinpr2-2,libpipewire-0.3-0,libgstreamer1.0-0,libgstreamer-plugins-base1.0-0,libspa-0.2-modules,libwebrtc-audio-processing1 \
		weston libwayland-egl1) $(fetch_debs -n fonts-dejavu-core); do
	extract_deb "$d" "$W"
done
echo "weston unpacked in $W ($(du -sh "$W" | cut -f1))"
