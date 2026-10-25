# Exper Easypad 8 (Model T8E) — Mainline Linux & Alpine Bring-up

[![SoC: Allwinner A31s](https://img.shields.io/badge/SoC-Allwinner%20A31s%20(sun6i)-blue.svg)](#hardware-specifications)
[![Kernel: Linux 7.2](https://img.shields.io/badge/Kernel-Mainline%20Linux%207.2-green.svg)](#hardware-support-status)
[![OS: Alpine Linux 3.20](https://img.shields.io/badge/OS-Alpine%20Linux%203.20%20ARMv7-purple.svg)](#quick-start)

Standalone bring-up project and reproducible build tree for running modern mainline Linux and Alpine Linux on the **Exper Easypad 8 (Model T8E)** quad-core tablet.

Internal NAND/eMMC flash remains completely untouched; the entire system boots cleanly from an external MicroSD card. Removing the SD card restores the factory stock Android system.

---

## Hardware Specifications

| Component | Specification | Details / Bus |
| :--- | :--- | :--- |
| **SoC** | Allwinner A31s (`sun6i`) | Quad-core ARM Cortex-A7 @ 1.2 GHz |
| **RAM** | 1 GiB DDR3 | Clocked at 432 MHz (ZQ 251) |
| **PMIC** | X-Powers AXP221s | Connected via **P2WI @ 0x68** (switched to P2WI mode by U-Boot) |
| **Display** | 1024×768 LVDS | Single-channel LVDS panel; PA25 backlight enable |
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
| **WiFi** | Broadcom `brcmfmac` (`wlan0`) | **Working** | SDIO AP6210 (BCM43362) on mmc1; live scanning verified |
| **Touchscreen** | `goodix` driver (`input0`) | **Working** | Event reports validated via evtest |
| **Sensors** | `st_lis3dh` I2C driver | **Detected** | Probed on I2C2 @ 0x19 |
| **Display (LCD)**| SimpleFB (`/dev/fb0`) + `fbcon` | **Working** | 1024×768@60Hz single-channel LVDS; console login active |
| **Audio** | Allwinner `sun4i-codec` (`sun6i-a31-codec`) | **Working** | Stereo DAC, Line Out (Speaker amp PL4), Headphone & Mic |
| **Bluetooth** | `hci_uart` | *Planned* | Broadcom UART on uart1 |

---

## Boot Architecture

```
[BROM] → [boot0 / SPL] (MicroSD offset 8 KB)
           ↓
[U-Boot 2024.x] (MicroSD offset 40 KB, PSCI / Non-Secure HYP)
  ├── LCD init & splash screen (SimpleFB @ 1024x768x32)
  └── Loads boot.scr, sun6i-a31s-exper-t8e.dtb, zImage
        ↓
[Linux Kernel 7.2 Mainline]
  ├── earlycon / 8250_dw serial console (PH20/PH21)
  ├── p2wi @ 0x68 (AXP221s regulators DLDO1/2/4)
  ├── sunxi-mmc root mount (root=/dev/mmcblk0p2)
  ├── mmc1 SDIO WiFi (AP6210 / BCM43362 -> wlan0)
  ├── goodix touchscreen probe (I2C1 0x5d)
  └── musb-sunxi USB gadget (usb0 CDC Ethernet)
        ↓
[Alpine Linux 3.20 (ARMv7 OpenRC)]
  ├── /sbin/getty 115200 ttyS0
  ├── sshd listening on 172.16.42.1:22
  └── wlan0 ready for wireless networking
```

---

## Verified Boot Output

```text
U-Boot SPL 2024.04 (Oct 24 2026 - 20:30:10 +0300)
DRAM: 1024 MiB
Trying to boot from MMC1
## Executing script at 43100000
>>> Exper Easypad 8 LowMem Bootloader <<<
>>> Loading DTB...
>>> Loading zImage...
>>> Booting Linux...
Starting kernel ...

[    0.000000] Booting Linux on physical CPU 0x0
[    0.000000] Linux version 7.2.0-gf36b54d9862f-dirty (mechres@workstation)
[    0.000000] Machine model: Exper Easypad T8E
...
[    9.560124] 1c28000.serial: ttyS0 at MMIO 0x1c28000 (irq = 33, base_baud = 1500000) is a 16550A
[    9.862140] sunxi-mmc 1c0f000.mmc: initialized, max sections: 128
[   10.281600] axp20x-i2c 2-0068: AXP20x variant AXP221 found
[   10.387395] probe of axp20x-regulator.0 with driver axp20x-regulator returned 0
[   10.420188] mmc0: new high speed SDHC card at address aaaa
[   10.428510] mmcblk0: mmc0:aaaa SC32G 29.7 GiB
[   10.435210]  mmcblk0: p1 p2
[   10.942642] mmc1: new high speed SDIO card at address 0001
[   11.120550] Goodix-TS 1-005d: ID 911, Version: 1060
[   11.135240] input: Goodix Capacitive TouchScreen as /dev/input/input0
[   12.450120] using random self ethernet address
[   12.455200] usb0: HOST MAC 9a:8b:7c:6d:5e:4f
[   12.460110] usb0: MAC a6:b7:c8:d9:ea:fb
[   12.465220] g_ether gadget: Ethernet Gadget, version: Memorial Day 2008
[   12.470110] g_ether gadget: g_ether ready
...
[  114.881121] brcmfmac: brcmf_fw_alloc_request: using brcm/brcmfmac43362-sdio for chip BCM43362/1
[  115.205934] brcmfmac: brcmf_c_preinit_dcmds: Firmware: BCM43362/1 wl0: Jul  5 2013 17:55:22 version 5.90.195.89.10 FWID 01-f4d31c13
...
Welcome to Alpine Linux 3.20
Kernel 7.2.0-gf36b54d9862f-dirty on an armv7l (ttyS0)

exper-t8e login: root
Password: 
Welcome to Alpine!
exper-t8e:~# ip addr show dev usb0
2: usb0: <BROADCAST,MULTICAST,UP,LOWER_UP> mtu 1500 qdisc pfifo_fast state UP
    inet 172.16.42.1/24 brd 172.16.42.255 scope global usb0
exper-t8e:~# iw dev wlan0 scan | grep SSID
	SSID: SUPERONLINE_WiFi_C8AF
	SSID: SUPERONLINE_Wi-Fi_2319
```

---

## Quick Start

### 1. Build Everything
```bash
# Set cross toolchain
source scripts/00-env.sh

# Build U-Boot bootloader
bash scripts/20-build-uboot.sh

# Build Linux Kernel (zImage, DTB, modules)
bash scripts/30-build-kernel.sh

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
- [docs/FINDINGS.md](docs/FINDINGS.md) — Comprehensive technical reference detailing all 32 hardware and software bring-up findings.
- [patches/README.md](patches/README.md) — Overview of U-Boot and Linux kernel patches.

---

## License

Device trees and kernel patches are licensed under the GPL-2.0-or-later. Build scripts and documentation are available under the MIT License.
