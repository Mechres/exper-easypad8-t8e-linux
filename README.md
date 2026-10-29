
# Exper Easypad 8 (Model T8E) — Mainline Linux & Alpine Bring-up

[![SoC: Allwinner A31s](https://img.shields.io/badge/SoC-Allwinner%20A31s%20(sun6i)-blue.svg)](#hardware-specifications)
[![Kernel: Linux 7.2](https://img.shields.io/badge/Kernel-Mainline%20Linux%207.2-green.svg)](#hardware-support-status)
[![OS: Alpine Linux 3.20](https://img.shields.io/badge/OS-Alpine%20Linux%203.20%20ARMv7-purple.svg)](#quick-start)
<p align="center">
<img width="500" height="500" alt="img" src="https://github.com/user-attachments/assets/590b9d5f-0111-4ed2-9946-685f5c4f2527" />
</p>
Standalone bring-up project and reproducible build tree for running modern mainline Linux and Alpine Linux on the **Exper Easypad 8 (Model T8E)** quad-core tablet.

Internal NAND/eMMC flash remains completely untouched; the entire system boots cleanly from an external MicroSD card. Removing the SD card restores the factory stock Android system.

**Built on this port:** [exper-t8e-android-auto](https://github.com/Mechres/exper-t8e-android-auto), an experimental wireless Android Auto head unit for this tablet (DRM video output with a NEON scaler, touch forwarding, audio over the 3.5 mm jack, power-key daemon).

---

## Hardware Specifications

| Component | Specification | Details / Bus |
| :--- | :--- | :--- |
| **SoC** | Allwinner A31s (`sun6i`) | Quad-core ARM Cortex-A7 @ 1.2 GHz |
| **RAM** | 1 GiB DDR3 | Clocked at 432 MHz (ZQ 251) |
| **PMIC** | X-Powers AXP221s | Connected via **P2WI @ 0x68** (switched to P2WI mode by U-Boot) |
| **Display** | 1024×768 LVDS | Single-channel 24-bit LVDS panel (VESA mapping, 60 MHz pixel clock); PG12 panel supply, PH13 PWM0 + PA25 backlight |
| **Touchscreen** | Goodix GT911 | Capacitive Multi-touch on **TWI1 @ 0x5d** (INT=PA3, RST=PA2) |
| **Storage** | MicroSD (`mmc0`) | Routed to **PF0–PF5**; always-on 3.3V rail |
| **USB** | OTG / MUSB | Micro-USB connector; supports Device / Gadget mode |
| **Accelerometer**| ST LIS3DH | 3-axis motion sensor on **TWI2 @ 0x19** |
| **Wireless** | Broadcom AP6210 (BCM43362) | SDIO WiFi (`mmc1`) + Bluetooth UART (`uart1`) |
| **Debug Port** | UART0 (115200 8N1) | Test pads on mainboard; routed to **PH20 (TX) / PH21 (RX)** |

---

## Hardware Support Status

| Subsystem | Driver / Mechanism | Status | Notes |
| :--- | :--- | :--- | :--- |
| **BROM & SPL** | Allwinner boot0 / U-Boot SPL | **Working** | Boots reliably from MicroSD |
| **U-Boot** | Mainline U-Boot 2024+ | **Working** | PSCI, Non-Secure mode, SimpleFB video |
| **PMIC & Fuel Gauge** | `axp20x` MFD / battery / ac / usb | **Working** | P2WI @ 0x68 + NMI IRQ; battery capacity, voltage, USB/AC sensing, PEK power key |
| **Serial Console** | Synopsys DW APB UART (`ttyS0`) | **Working** | Earlycon + interactive getty shell |
| **MicroSD Storage** | `sunxi-mmc` (`mmc0`) | **Working** | Rootfs mounted on `mmcblk0p2` |
| **USB Gadget** | `musb-sunxi` + `g_ether` (`usb0`)| **Working** | CDC Ethernet / RNDIS (172.16.42.1/24) |
| **SSH Server** | OpenSSH on Alpine Linux | **Working** | Direct root login via USB cable |
| **WiFi** | Broadcom `brcmfmac` (`wlan0`) | **Working** | SDIO Ampak AP6181 (BCM43362) on mmc1; live scanning verified |
| **Touchscreen** | `goodix` driver (`input0`) | **Working** | 1024×768 1:1 aligned coordinate space, 5-point multi-touch validated |
| **Sensors** | ST `lis3dh` IIO driver (`iio:device0`) | **Working** | 3-axis motion & tilt vector sensing with PA9 IRQ |
| **Display (LCD)**| `sun4i-drm` (DE1 backend + TCON0 LVDS) + fbdev emulation | **Working** | `card0` / `LVDS-1` 1024×768 @ 54.87 Hz, XRGB overlay planes, `fbcon` on `sun4i-drmdrmfb`; the SimpleFB DTB stays as fallback (Findings 47, 49) |
| **HDMI** | `sun4i-drm-hdmi` (`hdmi@1c16000`, `HDMI-A-1`) | **Working** | EDID read, 1920×1080 @ 60 Hz on its own CRTC next to the LVDS panel, hot-plug restores the picture; HDMI audio and CEC not tested (Finding 53) |
| **Backlight** | `pwm-backlight` (PH13 PWM0, PA25 enable, PG12 supply) | **Working** | `/sys/class/backlight/backlight` (0–56), `bl_power` for on/off |
| **DE1 hardware scaler** | `sun4i-frontend` (A31 DEFE) | **Not working** | Frontend never produces output on the A31s, see Finding 48; scale in software instead |
| **Video decoder** | `cedrus` (A31 Cedar VE, `/dev/video0`) | **Decodes H.264 and VP8, with a known H.264 chroma defect** | Bound to the A20 variant (MPEG-2, H.264, VP8; `NV12_32L32` output); the engine reports `VE_VERSION` 0x1633. VP8 is bit-exact; H.264 (GStreamer `v4l2slh264dec`) decoded three real 640x360 High-profile clips bit-exactly in 120 of 120 runs on a well-charged battery (intermittent errors seen earlier on a nearly empty battery, cause unverified) and differs only in chroma samples at the 0/255 limits of very saturated synthetic patterns (the real clips never leave the 16-240 chroma range, so this is unlikely to matter for natural video); MPEG-2 is within 5 levels of FFmpeg (Finding 51) |
| **GPU** | PowerVR SGX544 | **Not available** | The A31s has an SGX GPU, not a Mali; there is no mainline driver, so Lima cannot be used (Finding 52) |
| **Thermal** | `sun4i-ts` sensor (`cpu-thermal`, `&rtp`) | **Working** | `/sys/class/thermal/thermal_zone0/temp` reads the SoC temperature (about 36 °C idle) |
| **Audio** | Allwinner `sun4i-codec` (`sun6i-a31-codec`) | **Working** | Stereo DAC, Line Out (Speaker amp PL4), Headphone & Mic |
| **Bluetooth** | `btusb` (External) | **N/A (Not Fitted)** | Internal AP6181 is Wi-Fi only; external USB Bluetooth supported via OTG |

---

### Known gaps and open work

| Area | State | Notes |
|---|---|---|
| **GPU** | No mainline driver | The A31s has a PowerVR SGX544 MP2, not a Mali; Lima does not apply (Finding 52) |
| **cpufreq / DVFS** | Disabled | The CPU runs at the U-Boot setting (1008 MHz, 1.2 V). `sun6i-a31s-exper-t8e.dts` deletes `cpu-supply` and `operating-points`, which prints `missing clock-frequency` and `OPP table can't be empty` at boot. Removing that block and adding `clock-frequency` hung the boot without any log output; which part causes it is unknown. Retry step by step with the serial console attached (Findings 46, 55) |
| **Idle power** | High | About 490 mA on the battery with the screen off, about 425 mA with the panel off and one core online; the source of most of it is not identified (Finding 55) |
| **Suspend** | s2idle only | Works with the Wi-Fi chip kept powered (patch 0015); the benefit is unmeasured, a resume always turns the backlight on, the USB gadget does not always return, and `exper-powerd` does not suspend the system (Finding 54) |
| **Battery charging** | Partly solved | The AXP221 VBUS limit was 500 mA; lifting it halves the discharge on a PC USB port, but with the screen on the tablet still draws more than the port gives. It charges with the screen blanked (about +150 mA); a 2 A adapter is the intended supply (Finding 56) |
| **HDMI audio** | Missing | Video works (Finding 53). Mainline `sun4i-hdmi` has no audio support; the A31 user manual documents the audio registers, so a driver is possible |
| **Camera, ambient light/compass/gyro, vibration motor** | Not applicable or not present | No camera is fitted to the tablet and no sensors or vibration motor are expected; not investigated further |

## Boot Architecture

```
[BROM] → [boot0 / SPL] (MicroSD offset 8 KB)
           ↓
[U-Boot 2024.x] (MicroSD offset 40 KB, PSCI / Non-Secure HYP)
  ├── LCD init & splash screen (SimpleFB @ 1024x768x32)
  └── Loads boot.scr → sun6i-a31s-exper-t8e-drm.dtb, zImage (bootargs: cma=64M loglevel=4 ...)
        ↓
[Linux Kernel 7.2 Mainline] (init at ~1.8 s)
  ├── 4× Cortex-A7 (SMP), 8250_dw serial console (PH20/PH21)
  ├── AXP221s PMIC on P2WI @ 0x68 (regulators, battery, power key)
  ├── sunxi-mmc root mount (root=/dev/mmcblk0p2), mmc1 SDIO Wi-Fi (BCM43362 -> wlan0)
  ├── goodix touchscreen (I2C1 0x5d), musb-sunxi USB gadget (usb0 CDC Ethernet)
  └── modules-load: sun4i-backend, sun6i-drc, sun4i-drm → /dev/dri/card0, fbcon (DRM ready at ~4.5 s)
        ↓
[Alpine Linux 3.20 (ARMv7 OpenRC)]
  ├── /sbin/getty 115200 ttyS0
  ├── sshd listening on 172.16.42.1:22
  └── wlan0 ready for wireless networking
```

## Verified Boot Output

Excerpt of a real boot of the clean kernel (`dmesg`, DRM DTB), timestamps in seconds:

```text
[    0.000000] Linux version 7.2.0-gdbd482bca8d3 (arm-buildroot-linux-gnueabihf-gcc ... 13.3.0 ...)
[    0.000000] OF: fdt: Machine model: Exper Easypad T8E (DRM)
[    0.059785] smp: Brought up 1 node, 4 CPUs
[    0.059809] SMP: Total of 4 processors activated (192.00 BogoMIPS).
[    0.278061] 1c28000.serial: ttyS0 at MMIO 0x1c28000 (irq = 235, base_baud = 1500000) is a 16550A
[    0.312364] Goodix-TS 0-005d: ID 911, version: 1060
[    0.337058] axp20x-i2c 2-0068: AXP20x variant AXP221 found
[    0.417063] g_ether gadget.0: g_ether ready
[    0.432351] sunxi-mmc 1c0f000.mmc: initialized, max. request size: 16384 KB
[    0.515296]  mmcblk0: p1 p2
[    0.678736] mmc1: new high speed SDIO card at address 0001
[    1.758376] Run /sbin/init as init process
[    4.522070] sun4i-drm display-engine: bound 1c0c000.lcd-controller (ops sun4i_tcon_platform_driver_exit [sun4i_tcon])
[    4.524119] [drm] Initialized sun4i-drm 1.0.0 for display-engine on minor 0
[    4.572536] sun4i-drm display-engine: [drm] fb0: sun4i-drmdrmfb frame buffer device
[    4.918416] brcmfmac: brcmf_c_preinit_dcmds: Firmware: BCM43362/1 wl0: Jul  5 2013 17:55:22 version 5.90.195.89.10 FWID 01-f4d31c13
```

```text
exper-t8e:~# modetest -M sun4i-drm -c | grep -A2 LVDS
53	52	connected	LVDS-1         	162x121		1	52
  modes:
  #0 1024x768 54.87 1024 1030 1052 1350 768 772 780 810 60000 flags: ; type: preferred, driver
exper-t8e:~# ip addr show dev usb0
2: usb0: <BROADCAST,MULTICAST,UP,LOWER_UP> mtu 1500 qdisc pfifo_fast state UP
    inet 172.16.42.1/24 brd 172.16.42.255 scope global usb0
```

---

## Quick Start

### 1. Build Everything
```bash
# Set cross toolchain
source scripts/00-env.sh

# Build U-Boot bootloader
bash scripts/20-build-uboot.sh

# Linux kernel: apply the patch series to a mainline tree, then build out of tree
# (configs/exper_t8e_defconfig → zImage, both DTBs, modules)
git -C "$KERNEL_SRC" am "$PWD"/patches/kernel/*.patch
KERNEL_SRC=/path/to/linux bash scripts/30-build-kernel.sh

# Generate Alpine Linux Rootfs container
bash scripts/40-alpine-rootfs.sh
```

### 2. Flash to MicroSD Card
```bash
# Insert SD card into workstation (e.g. /dev/sdX)
sudo bash scripts/50-sdcard-alpine.sh /dev/sdX
```

### 3. Connect via USB Networking & SSH
Once booted with the USB cable connected to your PC:
```bash
sudo ip addr add 172.16.42.2/24 dev usb0
ssh root@172.16.42.1
```

---

## Serial Console & Hardware Debugging

The tablet exposes UART0 on mainboard test pads:
- **Baudrate:** `115200 8N1`
- **Signals:** TX (PH20), RX (PH21), GND
- **Helper tool:** `scripts/serial-capture.py` provides timestamped logging, automatic baud rate detection, and auto-reconnect on port disconnects.

```bash
python3 scripts/serial-capture.py /dev/ttyUSB0
```

---

## Documentation

- [docs/BUILD.md](docs/BUILD.md) — Detailed build prerequisites, compilation steps, and flashing instructions.
- [docs/FINDINGS.md](docs/FINDINGS.md) — Comprehensive technical reference detailing all 50 hardware and software bring-up findings.
- [patches/README.md](patches/README.md) — Overview of U-Boot and Linux kernel patches.

---

## License

Device trees and kernel patches are licensed under the GPL-2.0-or-later. Build scripts and documentation are available under the MIT License.
