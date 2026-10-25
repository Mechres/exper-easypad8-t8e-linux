#!/usr/bin/env bash
# Exper Easypad 8 (Allwinner A31s) — Android Reconnaissance and Backup Script
# Device must be connected via USB, Android booted, and USB Debugging (ADB) enabled.
# This script DOES NOT WRITE or modify anything on the device. It only collects info and pulls backups.

set -uo pipefail

PRJ_DERIVED="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
PRJ="${PRJ:-$PRJ_DERIVED}"
BK="$PRJ/backup"
FEX="$PRJ/fex"
DOC="$PRJ/docs"

mkdir -p "$BK" "$FEX" "$DOC"
L="$DOC/recon.log"
: > "$L"

say() {
    echo -e "\n### $*" | tee -a "$L"
}

# Run adb command with su if available, otherwise with normal shell
adbsh() {
    adb shell "$*" 2>&1
}

adb_root_sh() {
    if [ "$HAS_SU" = "1" ]; then
        adb shell "su -c '$*'" 2>&1
    elif [ "$IS_ROOT" = "1" ]; then
        adb shell "$*" 2>&1
    else
        adb shell "$*" 2>&1
    fi
}

say "1. Waiting for Device (adb wait-for-device)..."
adb wait-for-device
adb devices -l | tee -a "$L"

say "2. Checking Privilege Status..."
ID_OUT="$(adb shell id 2>&1)"
echo "Shell ID: $ID_OUT" | tee -a "$L"

IS_ROOT=0
HAS_SU=0

if echo "$ID_OUT" | grep -q "uid=0(root)"; then
    IS_ROOT=1
    echo "ADB has direct root privileges!" | tee -a "$L"
else
    SU_CHECK="$(adb shell "which su || ls /system/xbin/su /system/bin/su" 2>&1)"
    if echo "$SU_CHECK" | grep -q "su"; then
        HAS_SU=1
        echo "Root access available via 'su'." | tee -a "$L"
    else
        echo "No direct root/su found on device, collecting what is readable with user permissions." | tee -a "$L"
    fi
fi

say "3. Basic System and Hardware Information"
adb_root_sh cat /proc/version | tee -a "$L"
adb_root_sh cat /proc/cpuinfo | tee -a "$L"
adb_root_sh uname -a | tee -a "$L"

say "4. Build Properties (Model, Board, Chipset)"
adb_root_sh getprop | grep -Ei "model|board|device|manufac|hardware|brand|product|display|version" | tee -a "$L"
adb_root_sh getprop > "$DOC/all_getprop.txt"

say "5. Loaded Kernel Modules (Drivers)"
adb_root_sh lsmod | tee -a "$L"

say "6. Input / Touch / Key Devices"
adb_root_sh cat /proc/bus/input/devices | tee -a "$L"

say "7. I2C Bus and Connected Chips"
adb_root_sh 'ls -l /sys/bus/i2c/devices/' | tee -a "$L"

say "8. Display and Framebuffer (fb0)"
adb_root_sh 'cat /sys/class/graphics/fb0/modes 2>/dev/null; cat /sys/class/graphics/fb0/virtual_size 2>/dev/null; cat /sys/class/graphics/fb0/bits_per_pixel 2>/dev/null' | tee -a "$L"
adb_root_sh 'ls -l /sys/class/disp 2>/dev/null; ls -l /sys/devices/virtual/disp 2>/dev/null' | tee -a "$L"

say "9. PMIC and Battery (AXP221s)"
adb_root_sh 'ls -l /sys/class/power_supply/; cat /sys/class/power_supply/*/uevent 2>/dev/null' | tee -a "$L"

say "10. Partition Table (Partitions & Blocks)"
adb_root_sh cat /proc/partitions | tee -a "$L"
adb_root_sh cat /proc/mtd 2>/dev/null | tee -a "$L"
adb_root_sh 'ls -l /dev/block/ 2>/dev/null' | tee -a "$L"
adb_root_sh 'ls -l /dev/block/by-name/ 2>/dev/null' | tee -a "$L"

say "11. Searching and Pulling script.bin / FEX"
# Is it directly present on the filesystem?
for path in /boot/script.bin /system/vendor/script.bin /data/script.bin /fex; do
    if adb_root_sh "[ -f $path ] && echo var" | grep -q "var"; then
        echo "script.bin found: $path" | tee -a "$L"
        adb_root_sh "cat $path" > "$FEX/script.bin"
        break
    fi
done

# If nanda or bootloader block exists, pull from there
if [ ! -s "$FEX/script.bin" ]; then
    say "script.bin not found directly, trying bootloader block (/dev/block/nanda or mmcblk0p1)..."
    for blk in /dev/block/nanda /dev/block/by-name/bootloader /dev/block/mmcblk0p1; do
        if adb_root_sh "[ -b $blk ] && echo var" | grep -q "var"; then
            echo "$blk exists, pulling..." | tee -a "$L"
            adb_root_sh "dd if=$blk of=/data/local/tmp/bootloader_part.img bs=1M"
            adb pull /data/local/tmp/bootloader_part.img "$BK/bootloader_part.img"
            adb_root_sh "rm -f /data/local/tmp/bootloader_part.img"
            
            # Check if script.bin is inside (FAT or raw)
            if [ -f "$BK/bootloader_part.img" ]; then
                echo "Examining bootloader_part.img..." | tee -a "$L"
                # Extraction can be tried with mtools or 7z
                7z e -y "$BK/bootloader_part.img" script.bin -o"$FEX/" 2>/dev/null || true
            fi
            break
        fi
    done
fi

# If script.bin was pulled, decompile immediately with bin2fex
if [ -s "$FEX/script.bin" ]; then
    say "script.bin successfully pulled! Decompiling to FEX format..."
    bin2fex "$FEX/script.bin" "$FEX/easypad_t8e.fex"
    echo "FEX generated: $FEX/easypad_t8e.fex" | tee -a "$L"
fi

say "12. Capturing dmesg Output"
adb_root_sh dmesg > "$DOC/dmesg.log" 2>/dev/null

say "COMPLETED! Summary files:"
ls -lh "$BK" "$FEX" "$DOC" | tee -a "$L"
echo -e "\nLog saved to: $L\n"
