# Technical Findings & Diagnostics Log — Exper Easypad 8 (T8E)

**Hardware:** Exper Easypad 8 (Model T8E / `fiber_exper`)  
**SoC:** Allwinner A31s (`sun6i`, 4× Cortex-A7 @ 1.2 GHz)  
**RAM:** 1 GB DDR3 @ 432 MHz (ZQ 251)  
**PMIC:** AXP221s via P2WI @ 0x68 (switched from I2C to P2WI mode by U-Boot)  
**Storage Target:** External MicroSD (`mmc0`), internal NAND untouched  
**Display:** 1024×768 single-channel 24-bit LVDS panel (PG12 power, PH13 PWM0, PA25 BL_EN), driven by `sun4i-drm` (SimpleFB DTB kept as fallback)  
**Target:** Minimal Alpine Linux booted from SD card → Android Auto terminal

---

## Executive Summary

During the bring-up process, the tablet transitioned through multiple boot blockers:
```
BROM → boot0 → U-Boot (LCD/SPL) → Linux (Mainline 7.2) → Alpine Linux → OpenRC getty / sshd
                                                               ↳ usb0 (172.16.42.1) → SSH root@172.16.42.1 [OK]
```

A total of 50 technical findings were diagnosed and documented (49 resolved; Finding 48 is a documented negative result).

---

## Findings 1 to 50

### Finding 1: ARM32 HighMem & DTB Relocation Conflict
* **Mechanism:** By default, U-Boot's `bootz` relocates the Flattened Device Tree (FDT) to the top of accessible physical RAM (`0x7fbe0000` / ~1020 MB on 1 GB boards).
* **Conflict:** Early 32-bit ARM Linux kernel initialization maps only LowMem (under `0x70000000`). Accessing the DTB in HighMem before `paging_init()` caused a kernel crash in `setup_machine_fdt()`.
* **Fix:** Added `setenv fdt_high 0x50000000` and `setenv bootm_size 0x10000000` to `boot.cmd`.

### Finding 2: Hardware Assembly Probe for Kernel Execution Verification
* **Experiment:** Inserted a 10-instruction raw assembly probe in `arch/arm/boot/compressed/head.S` to toggle LCD backlight GPIO `PA25` for 200 ms.
* **Result:** Screen backlight physically flickered right after `Starting kernel ...`, proving that U-Boot successfully handed execution to the decompressed zImage.

### Finding 3: Secure SVC vs Non-Secure / PSCI / HYP Mode
* **Issue:** Legacy Android U-Boot defconfig set `CONFIG_ARMV7_BOOT_SEC_DEFAULT=y` and disabled `CONFIG_ARMV7_NONSEC` and `CONFIG_ARMV7_PSCI`.
* **Impact:** Modern mainline ARM kernels (`CONFIG_ARM_VIRT_EXT=y`) expect to boot in Non-Secure HYP mode. Booting in Secure SVC left GICv2 unbanked, CNTVOFF uninitialized, and failed during `__hyp_stub_install`.
* **Fix:** Enabled native `CONFIG_ARMV7_NONSEC=y` and `CONFIG_ARMV7_PSCI=y` in U-Boot `exper_t8e_defconfig`.

### Finding 4: SimpleFB Clocks and CCU Clock Gating
* **Mechanism:** When Allwinner CCU driver probed, `clk_disable_unused` gated all clocks not explicitly claimed by an active driver.
* **Impact:** If `clocks` were missing from `simplefb_lcd`, CCU disabled `CLK_AHB1_BE0`, `CLK_AHB1_LCD0`, and `CLK_BE0`, freezing the LVDS display on the last frame displayed by U-Boot (`Starting kernel ...`).
* **Fix:** Retained full CCU clock definitions on `simplefb_lcd`.

### Finding 5: Device Tree Sync Across Build Stages
* **Issue:** `30-build-kernel.sh` previously copied the minimal U-Boot DTS instead of the full kernel device tree.
* **Fix:** Synchronized `dts/sun6i-a31s-exper-t8e-kernel.dts` to `arch/arm/boot/dts/allwinner/sun6i-a31s-exper-t8e.dts`.

### Finding 6: `arm,cpu-registers-not-fw-configured` and Arch Timer Exception
* **Mechanism:** Upstream `sun6i-a31.dtsi` timer node specified `arm,cpu-registers-not-fw-configured`. In `arm_arch_timer.c`, this property forces `ARCH_TIMER_PHYS_SECURE_PPI`.
* **Conflict:** When booting in Non-Secure / PSCI mode, reading the secure physical timer generates an Undefined Instruction exception in `time_init()`, hanging the CPU before console init.
* **Fix:** Removed `arm,cpu-registers-not-fw-configured;` from `sun6i-a31.dtsi`.

### Finding 7: SD Card Detection & Always-On Rail
* **Details:** Vendor FEX specified `sdc_regulator = "none"` with detection pin `PA08`. Added `broken-cd;` to prevent missed card insertion events on loose sockets.

### Finding 8: Console Output Routing
* **Details:** Added `fbcon=map:0` for framebuffer console, `earlycon=uart8250,mmio32,0x01c28000` for early serial output, and configured `&uart0` on PH20/PH21.

### Finding 9: SimpleFB Deferred Probe and SMP Isolation
* **Details:** Removed `vcc-lcd-supply` and `vcc-pg-supply` regulator dependencies from `&simplefb_lcd` via `/delete-property/` to prevent infinite `-EPROBE_DEFER` before PMIC initialization.

### Finding 10: Enforcing Non-Secure Boot Mode
* **Details:** Changed `setenv bootm_boot_mode sec` to `setenv bootm_boot_mode nonsec` in boot scripts to align with PSCI/HYP requirements.

### Finding 11: `printascii()` / `CONFIG_DEBUG_UART_VIRT` Unmapped MMIO Fault
* **Root Cause:** Custom `early_print()` called `printascii()` (`CONFIG_DEBUG_LL`), which accessed virtual address `0xf1c28000`. On modern kernels without static IO mapping, the first store to this unmapped address after MMU enable triggered a silent Data Abort before console registration.
* **Fix:** Replaced `printascii()` with buffered `printk()` in `setup.c` and disabled `CONFIG_EARLY_PRINTK`.

### Finding 12: `serial8250_config_port()` Probe Lockup
* **Details:** Early boot serial output abruptly halted during `serial8250_config_port()` autoconfiguration, locking the CPU and triggering the watchdog.

### Finding 13: 32-bit MMIO Alignment Requirement
* **Details:** Allwinner APB UART registers are 32-bit word-aligned (`mmio32`, stride of 4 bytes). Any unaligned byte access (e.g. `uart + 0x03` instead of `uart + 0x0C` for LCR) triggers an immediate kernel Data Abort.

### Finding 14: Hardware Watchdog as Deterministic Loop Probe
* **Details:** Configured the A31s hardware watchdog (base `0x01c20ca0`) with an 8-second timeout, allowing automated reboot loops during debugging without manual power cycling.

### Finding 15: Autoconfig Hang in `CONFIG_SERIAL_8250_16550A_VARIANTS`
* **Root Cause:** Probing extended 16550A variants (`autoconfig_16550a`) performed loopback and FIFO sizing tests that hung the Synopsys DesignWare APB UART.
* **Fix:** Disabled `CONFIG_SERIAL_8250_16550A_VARIANTS` in kernel `.config`.

### Finding 16: Synopsys DesignWare 8250 APB Bus Lockup Quirks
* **Root Cause:**
  1. Reading `DW_UART_USR` (0x1F / 0x7C) during `dw8250_idle_enter()` BUSY polling permanently hung the A31s APB bus.
  2. Reading RBR (`UART_RX`) to flush FIFO during idle entry also locked up the APB bus.
  3. Floating UART RX line (PH21) created ghost characters due to crosstalk.
* **Fix:** Added bias pull-up on PH21; bypassed `DW_UART_USR` BUSY polling and RBR flush in `8250_dw.c`; bounded `frame_time` delay ceiling with `min_t(u32, p->frame_time, 200000u)`.

### Finding 17: Runtime PM UART Clock Gating During Console Handover
* **Root Cause:** When `dw-apb-uart` runtime suspended, it gated `baudclk`. If `earlycon` or boot console printed subsequent messages to the gated UART, the APB bus hung.
* **Fix:** Added `pm_runtime_forbid(dev);` in `dw8250_probe()` to prevent gating baudclk while boot console is active.

### Finding 18: Invalid FEX TWI Pin Definitions
* **Details:** Vendor FEX specified TWI pins on PF4/PF5, which mainline sunxi pinctrl does not support ("unsupported function i2c1 on pin PF4"). Restored default PH16/PH17 (I2C1) and PH18/PH19 (I2C2) routing.

### Finding 19: Touchscreen Goodix GT911 Identification
* **Details:** Confirmed Goodix GT911 capacitive touch controller on TWI1 @ 0x5d (INT=PA3, RST=PA2) using upstream `goodix.c` driver with `touchscreen-swapped-x-y`.

### Finding 20: Accelerometer LIS3DH I2C Address
* **Details:** I2C bus scan revealed ST LIS3DH accelerometer at address 0x19 (`SA0=1`) rather than the FEX-specified 0x18.

### Finding 21: AXP221s PMIC Interrupt Mask Bus Hang
* **Mechanism:** AXP221s probe hung during `regmap_add_irq_chip` writing interrupt masks (register 0x40), returning `-ENXIO` (bus NACK) and causing deferred probe cascade for all regulators.
* **Fix:** Omitted the `interrupts` property from the PMIC DT node, triggering the safe regulator-only cells code path in `axp20x.c`.

### Finding 22: MicroSD Pin Routing Verification (PF0-PF5 vs PD23-PD28)
* **Details:** Vendor FEX listed SD card on PD23-PD28, which mainline pinctrl rejected ("unsupported function mmc0 on pin PD23"). Verified physical routing to PF0-PF5 as confirmed by working U-Boot SPL read.

### Finding 23: Inverted `get_cycles()` Return Value
* **Root Cause:** In `arch/arm/include/asm/timex.h`, `#define get_cycles()` used `delay_read_timer(&c) ? 0 : c`. Because `delay_read_timer()` returns `true` on success, `get_cycles()` returned 0 instead of cycle count.
* **Fix:** Corrected macro condition to `delay_read_timer(&c) ? c : 0`.

### Finding 24: `sunxi-mmc` Unregulated SD Slot Fallback
* **Root Cause:** MicroSD slot power rail is hardwired always-on without an active regulator node. Without `vmmc-supply`, `host->ocr_avail` remained 0, causing `mmc_select_voltage()` to fail card init with error -22.
* **Fix:** Added fallback in `drivers/mmc/host/sunxi-mmc.c` setting `ocr_avail = MMC_VDD_32_33 | MMC_VDD_33_34` when regulator is absent.

### Finding 25: USB Gadget Deferred Probe Deadlock
* **Root Cause:** `phy-sun4i-usb` requested `usb0_vbus_power-supply`, `usb0_vbus-supply`, and `usb1_vbus-supply` linked to AXP221s. Without PMIC regulator probe, USB PHY deferred indefinitely (`-EPROBE_DEFER`), preventing `g_ether` UDC binding.
* **Fix:** Deleted VBUS supply properties from `&usbphy` in Device Tree for device/gadget mode.

### Finding 26: Complete USB Gadget Ethernet Bring-Up
* **Details:** Enabled `CONFIG_USB_MUSB_SUNXI=y`, `CONFIG_USB_ETH=y`, and `CONFIG_USB_ETH_RNDIS=y`. Gadget enumerates as `usb0` (CDC Ethernet / RNDIS) with static IP `172.16.42.1/24`.

### Finding 27: Kernel Init Handoff & Alpine Linux Boot
* **Details:** Rootfs mounted via `root=/dev/mmcblk0p2 rootfstype=ext4 rw rootwait`. OpenRC successfully spawned login getty on serial console.

### Finding 28: Alpine OpenSSH `/var/empty` Permission Fix
* **Issue:** OpenSSH server refused connections with `Privilege separation user sshd does not own /var/empty`.
* **Fix:** Added `chown root:root /var/empty` and `chmod 755 /var/empty` in rootfs setup script (`40-alpine-rootfs.sh`).

### Finding 29: OpenSSH `PermitRootLogin` Configuration
* **Issue:** OpenSSH defaulted to `PermitRootLogin prohibit-password`, blocking initial password-based root login over USB network.
* **Fix:** Added `/etc/ssh/sshd_config.d/10-rootpw.conf` with `PermitRootLogin yes`.

### Finding 30: Read-Write Boot Partition Mount
* **Issue:** `/mnt/boot` was mounted read-only due to shared superblock flags during kernel update script execution.
* **Fix:** Added `mount -o remount,rw /mnt/boot` in `51-update-kernel-sdcard.sh`.

### Finding 31: Loopback Interface Auto-Configuration
* **Issue:** Alpine network service lacked a default loopback definition, leaving `lo` in DOWN state.
* **Fix:** Injected `auto lo \n iface lo inet loopback` into `/etc/network/interfaces`.

### Finding 32: System Time & RTC Initialization
* **Details:** Tablet lacks a functional RTC battery, causing clock to reset to 1970 on reboot. Implemented automatic time synchronization over USB SSH session.

### Finding 33: AXP221s Communication Bus (P2WI Mode @ 0x68 vs I2C1 @ 0x34)
* **Background:** Vendor FEX specified `pmu_twi_id = 1` and `pmu_twi_addr = 0x34`, suggesting I2C1 communication.
* **Mechanism:** In reality, the A31s U-Boot bootloader switches the AXP221s from standard I2C into P2WI (Push-Pull 2-Wire Interface) mode at address `0x68` (controller at `0x01f03400`).
* **Fix:** Enabled `&p2wi { status = "okay"; }` in the board Device Tree. The `i2c-sunxi-p2wi` driver probed successfully (`axp20x-i2c 2-0068: AXP20x variant AXP221 found`), establishing full communication with the PMIC and registering all power rails (`reg_dldo1`, `reg_dldo2`, `reg_dldo4`, `reg_dc1sw`, etc.).

### Finding 34: Allwinner Linear GPIO Bank Offset Rosetta Stone
* **Root Cause:** Allwinner vendor kernel drivers use a flat linear pin numbering scheme in FEX configurations.
* **Discovery:** Reverse-engineered `arch/arm/mach-sun6i/gpio/gpio_init.c` from the stock Android `zImage` (struct table at `0x8243b8`):
  * `GPA: 0 (28)`, `GPB: 30 (8)`, `GPC: 40 (28)`, `GPD: 70 (28)`, `GPE: 100 (17)`, `GPF: 119 (6)`, `GPG: 127 (19)`, `GPH: 148 (31)`, `GPL: 181 (9)`, `GPM: 192 (8)`
* **Decoded Pin Assignments:**
  * WiFi `WL_REG_ON` (FEX 186): `186 - 181 = 5` $\rightarrow$ **`PL5`** (`<&r_pio 0 5 GPIO_ACTIVE_LOW>`)
  * WiFi `WL_HOST_WAKE` (FEX 187): `187 - 181 = 6` $\rightarrow$ **`PL6`** (`<&r_pio 0 6 ...>`)
  * Backlight PWM (FEX 161): `161 - 148 = 13` $\rightarrow$ **`PH13`** (`SUN6I_GPH_PWM` / `PWM0`)
  * LCD Panel Power (FEX 139): `139 - 127 = 12` $\rightarrow$ **`PG12`**

### Finding 35: Broadcom AP6210 / BCM43362 SDIO WiFi Bring-Up
* **Mechanism:** The tablet contains an Ampak AP6210 module containing a Broadcom BCM43362 SDIO WLAN core and BCM20710 Bluetooth core.
* **Prerequisites:**
  1. Main power: `reg_dldo1` (3.3V)
  2. I/O power: `reg_dldo2` (1.8V, powers Port G pins PG0-PG5)
  3. Clock power: `reg_dldo4` (3.3V)
  4. LPO sleep clock: 32.768 kHz fanout from RTC (`0x01f00000`) routed to `PM7` (`function = "rtc"`)
  5. Reset sequence: `mmc-pwrseq-simple` on `PL5` with `post-power-on-delay-ms = <200>`
* **Result:** `mmc1` successfully enumerates the card at `0x02d0:0xa962` (BCM43362). The `brcmfmac` driver loads firmware `brcm/brcmfmac43362-sdio.bin` and registers `wlan0`. Live air scanning (`iw dev wlan0 scan`) verified.

### Finding 36: Kernel NULL Pointer Dereference in `clk_pm_runtime_get` (PID 38 `sugov:0`)
* **Mechanism:** During CPU frequency scaling, the schedutil governor (`sugov:0`) invoked `clk_pm_runtime_get()`.
* **Root Cause:** In `drivers/clk/clk.c`, `clk_pm_runtime_get()` invoked `pm_runtime_resume_and_get(core->dev)`. Allwinner CCU clock providers do not assign a `struct device` (`core->dev == NULL`), causing a NULL pointer dereference of `dev->power` at offset `0x119`.
* **Fix:** Added NULL check `if (!core->rpm_enabled || !core->dev) return 0;` in `clk_pm_runtime_get()` and `clk_pm_runtime_put()`.

### Finding 37: Upstream SimpleFB Device Tree Population Bug (`drivers/of/platform.c`)
* **Mechanism:** In `drivers/of/platform.c`, `of_platform_default_populate_init()` used `node = of_get_compatible_child(of_chosen, "simple-framebuffer")`.
* **Root Cause:** `of_get_compatible_child()` returns the first child matching the compatible string without checking whether the node is enabled (`status = "okay"`). In `sun6i-a31.dtsi`, `framebuffer-lcd0-hdmi` (`status = "disabled"`) precedes `framebuffer-lcd0` in `/chosen`. The kernel attempted to populate the disabled HDMI node, failed, and completely skipped the active LCD framebuffer.
* **Fix:** Replaced single lookup with `for_each_child_of_node()` checking `of_device_is_available()`.

### Finding 38: LCD Panel Power & Hardware Backlight PWM Automation
> **SimpleFB boot only.** With the DRM DTB the kernel owns the panel supply and the PWM backlight (Findings 47 and 50); do not run `exper-backlight` there.

* **Mechanism:**
  * Panel power: `PG12` must be driven HIGH to supply power to the LVDS panel electronics (`CONFIG_VIDEO_LCD_POWER="PG12"` in U-Boot).
  * Backlight enable: `PA25` must be driven HIGH (`CONFIG_VIDEO_LCD_BL_EN="PA25"`).
  * Backlight PWM: `PH13` muxed to `PWM0` (50 kHz, active high).
* **Implementation:** Built [`exper-backlight`](../scripts/exper-backlight.c) and configured an OpenRC startup service ([`01-backlight.start`](../rootfs/alpine/etc/local.d/01-backlight.start)) to power the panel and enable backlight on boot, bringing up the Alpine Linux login prompt on screen.

### Finding 39: Internal Audio Codec Bring-Up (`sun6i-a31-codec`)
* **Mechanism:** Allwinner A31s includes an on-chip analog audio codec at `0x01c22c00` paired with digital audio DMA channel 15.
* **Pin Routing:**
  * Speaker Power Amplifier enable (`audio_pa_ctrl`): FEX linear GPIO 185 $\rightarrow$ **`PL4`** (`<&r_pio 0 4 GPIO_ACTIVE_HIGH>`). Configured as `allwinner,pa-gpios` on the codec node.
* **Audio Routing:** Configured `"Headphone", "HP"`, `"Speaker", "LINEOUT"`, `"MIC1", "Mic"`, `"MIC2", "Headset Mic"`.
* **Result:** ALSA card `#0: A31 Audio Codec` registered with `/dev/snd/pcmC0D0p` (stereo playback) and `/dev/snd/pcmC0D0c` (microphone capture). Playback validated via `speaker-test` (48 kHz 16-bit stereo PCM). ALSA volume and routing state persisted via `alsactl store`.

### Finding 40: PMIC NMI Interrupt, Battery Fuel Gauge & Power Button (`axp20x-battery`, `axp20x-pek`)
* **Mechanism:**
  * Re-enabled the NMI interrupt (`interrupt-parent = <&r_intc>`, `interrupts = <GIC_SPI 32 IRQ_TYPE_LEVEL_LOW>`) on `&axp22x` now that P2WI communication at `0x68` is established.
  * Added DT aliases (`aliases { mmc0 = &mmc0; mmc1 = &mmc1; };`) to prevent probe race conditions where SDIO WiFi probing before SD card swapped root device from `/dev/mmcblk0p2` to `/dev/mmcblk1p2`.
* **Result:**
  * Sub-devices registered under `/sys/class/power_supply/`:
    * `axp20x-battery`: Real-time telemetry reporting voltage (`4.008 V`), current draw (`-149 mA`), and capacity (`87%`).
    * `axp20x-usb` & `axp22x-ac`: Online detection for external chargers.
  * Physical power button (`axp20x-pek`) registered as an input device (`/dev/input/event1`).

### Finding 41: Absence of On-Board Bluetooth Hardware (Ampak AP6181 vs AP6210)
* **Investigation:** The tablet's wireless module was evaluated for Bluetooth support over `uart1`.
* **Hardware Verification:**
  1. The stock Android firmware configuration (`nvram_ap6181.txt` and `fw_bcm40181a2.bin`) confirms the physical module installed on the PCB is an **Ampak AP6181**, not an AP6210.
  2. The AP6181 contains only the Broadcom BCM43362 (802.11 b/g/n Wi-Fi over SDIO) without Bluetooth circuitry.
  3. The stock FEX script (`easypad_t8e.fex` and sysfs dump) explicitly defines `[bt_para]` with `bt_used = 0` and unassigned UART/wake/reset GPIOs.
  4. The factory Android operating system (`init.sun6i.rc`, `build.prop`) had no Bluetooth services, drivers, or daemon configured.
  5. Retail specifications for the Exper Easypad 8 (T8E) confirm Bluetooth was not included in this model.
* **Alternative:** Standard USB Bluetooth dongles work plug-and-play via USB OTG using the active kernel USB host stack (`ehci-platform` / `musb-sunxi`) and `btusb`.

### Finding 42: Goodix GT911 Capacitive Touchscreen Bring-Up & Coordinate Verification
* **Mechanism:** The tablet uses a Goodix GT911 (`ID 911, version 1060`) 5-point capacitive multi-touch controller attached to `&i2c1` at address `0x5d`.
* **Hardware Pins:**
  * Interrupt: `PA3` (`<&pio 0 3 IRQ_TYPE_LEVEL_HIGH>`)
  * Reset: `PA2` (`<&pio 0 2 GPIO_ACTIVE_HIGH>`)
* **Axis & Coordinate Verification:**
  * Native Resolution: $1024 \times 768$ mapped to screen framebuffer.
  * Property: `touchscreen-swapped-x-y;` (matching FEX `ctp_exchange_x_y_flag = 1`).
  * Live touch sampling via `evtest /dev/input/event0` verified:
    * Top-Left: $X \approx 36, Y \approx 21$ (approaching origin $(0, 0)$).
    * Bottom-Right: $X \approx 933, Y \approx 721$ (approaching maximum $(1024, 768)$).
    * Center: $X \approx 457, Y \approx 442$.
  * Confirmed: Zero inversion required (`revert_x = 0, revert_y = 0`). The touch matrix is 1:1 aligned with the display panel.

### Finding 43: STMicroelectronics LIS3DH 3-Axis Accelerometer Bring-Up (`st_accel` / IIO Subsystem)
* **Mechanism:** The tablet is equipped with an STMicroelectronics LIS3DH MEMS digital motion sensor attached to `&i2c2` at I2C address `0x19`.
* **Hardware Wiring:**
  * I2C Bus: `&i2c2` (`1c2b400.i2c`)
  * Address: `0x19`
  * Interrupt 1: `PA9` (`<&pio 0 9 IRQ_TYPE_EDGE_RISING>`), corresponding to FEX linear pin configuration `gsensor_int1 = port:PA09<6><1>`.
* **Kernel Subsystem Configuration:**
  * Legacy hardware monitoring driver `CONFIG_SENSORS_LIS3_I2C` was disabled to resolve symbol and binding conflicts with the modern Industrial I/O (IIO) framework.
  * Enabled built-in IIO drivers: `CONFIG_IIO_ST_ACCEL_3AXIS=y`, `CONFIG_IIO_ST_ACCEL_I2C_3AXIS=y`, `CONFIG_IIO_TRIGGER=y`, and `CONFIG_IIO_TRIGGERED_BUFFER=y`.
* **Verification:**
  * Driver initialized cleanly as `/sys/bus/iio/devices/iio:device0` (`name: lis3dh`).
  * IRQ thread `irq/50-lis3dh-t` successfully registered on IRQ 50 (`PA9`).
  * Live sampling confirmed valid gravitational vector:
    * $X \approx 860$, $Y \approx -48$, $Z \approx -568$ at scale $0.009806\text{ m/s}^2$
    * Euclidean magnitude: $\sqrt{X^2 + Y^2 + Z^2} \times \text{scale} \approx 10.1\text{ m/s}^2 \approx 1.0\text{ g}$.

### Finding 44: Automated Wi-Fi Association, DHCP & Internet Connectivity
* **Mechanism:** Configured WPA supplicant and OpenRC network services for automated connection on boot.
* **Network Credentials:** Configured `/etc/wpa_supplicant/wpa_supplicant.conf` with WPA-PSK authentication.
* **Permissions:** Fixed directory permissions on `/var` and symlink `/var/run` to satisfy OpenRC `checkpath` security validation (`root:root`).
* **Services Enabled:**
  * `wpa_supplicant`: Automatically associates with access point on boot (`wpa_state=COMPLETED`, WPA2-PSK CCMP).
  * `net.wlan0`: Built custom OpenRC service in `default` runlevel running `udhcpc` in background lease renewal mode.
* **Verification:**
  * Interface assigned local IP: `192.168.10.103/24` with default gateway `192.168.10.1`.
  * Real-world air latency: ~37ms to `8.8.8.8`, ~66ms to `google.com`.
  * Direct SSH access verified over local WLAN: `ssh root@192.168.10.103`.
  * Package manager `apk update` synchronized 22,694 Alpine packages directly over Wi-Fi.

### Finding 45: Power Management & Physical Power Button Screen Toggle Daemon
> With the DRM DTB `exper-powerd` toggles the kernel backlight class (`bl_power`) instead of calling `exper-backlight` (Finding 50).

* **Investigation:** Evaluated power states (`freeze`, `mem_sleep: [s2idle]`) and physical button events.
* **Findings:**
  1. Low-power idle / `freeze` state executes cleanly at kernel/SoC level with RTC wakeup (`rtcwake`).
  2. However, SDIO Wi-Fi desynchronizes on raw sleep without explicit interface down / up teardown, and MUSB gadget requires soft-reconnect.
  3. Active operational power consumption is exceptionally low: $195\text{ mA}$ at $3.95\text{V}$ ($\approx 0.77\text{ W}$) with LCD active at 80% brightness and Wi-Fi associated.
     **Correction (see Finding 55):** that figure was read while the tablet was on a USB supply, where `current_now` only shows the battery's share. Measured on the battery alone, a screen-off idle draws about 490 mA.
* **Implementation:**
  * Enhanced [`exper-backlight`](../scripts/exper-backlight.c) with brightness scaling ($0-100\%$) and clean low-power standby mode (`off` / `on`), shutting off PWM, `PA25` backlight enable, and `PG12` panel electronics.
  * Developed [`exper-powerd`](../scripts/exper-powerd.c) background daemon listening on `/dev/input/by-path/platform-1f03400.i2c-platform-axp221-pek-event`.
  * Single-press of the physical power button toggles the LCD panel and backlight off / on instantly, providing real tablet power-key behavior and maximizing battery endurance.

### Finding 46: Allwinner A31s 4-Core SMP Multi-Core Activation
* **Root Cause of Single-Core State:** During initial Linux 7.2 board bring-up, `nosmp` was set in `bootargs` and hardcoded into `CONFIG_CMDLINE` with `CONFIG_CMDLINE_FORCE=y` to prevent early `cpufreq-dt` binding panics on boards lacking an OPP table.
* **Resolution:**
  1. Updated `dts/sun6i-a31s-exper-t8e-kernel.dts` to delete `operating-points` and `cpu-supply` under `&cpu0`. This cleanly causes `cpufreq-dt` to report `cpu cpu0: OPP table can't be empty` and exit safely (return code 19) without blocking SMP initialization. **Superseded by Finding 58:** the real cpufreq problem was a PLL settle race, and this block was removed again.
  2. Removed `nosmp` from `CONFIG_CMDLINE`, enabled `CONFIG_CMDLINE_EXTEND`, and disabled `CONFIG_CMDLINE_FORCE`.
  3. Recompiled `zImage` and updated `uboot/boot.cmd` and `uboot/boot.scr`.
* **Verification:**
  * Secondary CPUs brought up during kernel boot:
    ```text
    smp: Bringing up secondary CPUs ...
    smp: Brought up 1 node, 4 CPUs
    SMP: Total of 4 processors activated (192.00 BogoMIPS).
    ```
  * Verified live sysfs and cpuinfo:
    * `/sys/devices/system/cpu/online`: `0-3`
    * `nproc`: `4`
    * All 4 cores (`processor 0, 1, 2, 3`) fully active and scheduling user tasks.

### Finding 47: `sun4i-drm` (DRM/KMS) Display Bring-Up for the LVDS Panel
* **Goal:** Replace the U-Boot SimpleFB framebuffer with a real display driver (planes, vsync, kernel-managed panel power and backlight).
* **Root causes found, in the order they showed up:**
  1. `display-engine` is `status = "disabled"` in `sun6i-a31.dtsi`, so `sun4i-drm` never bound. The board DTS must set `&de { status = "okay"; }`.
  2. The component master only completes when `sun4i-backend`, `sun6i-drc`, `sun4i-tcon` and `sun4i-drm` are all loaded (`/etc/modules-load.d/exper-drm.conf`).
  3. `sun6i_a31s_quirks` in `sun4i_tcon.c` lacked `supports_lvds`, `has_lvds_alt` and `setup_lvds_phy`, so TCON0 failed to bind with `-22` as soon as a `panel-lvds` was attached. Fixed by kernel patch 0008 (same LVDS PHY setup as the A33).
  4. The A31 dtsi has no PWM controller node and no LVDS pin group; `sun6i-a31s-exper-t8e-drm.dts` (patch 0010) adds `pwm` (PH13 = PWM0), `lcd_lvds_pins` (PD0–PD9, `lvds0`), the `panel-lvds` node, a `pwm-backlight` (50 kHz, PA25 enable), a fixed regulator for PG12 and the `tcon0_out` endpoint to the panel.
* **Panel parameters** (FEX `[lcd0_para]` and U-Boot `CONFIG_VIDEO_LCD_MODE`): `lcd_lvds_colordepth = 0` means 24 bit and `lcd_lvds_mode = 0` means VESA mapping, so `data-mapping = "vesa-24"`. Totals 1350×810 with `hback = 298`, `hfront = 6`, `hsync = 22`, `vback = 30`, `vfront = 4`, `vsync = 8` (the FEX `hbp`/`vbp` include the sync length) at a 60 MHz pixel clock, giving 54.87 Hz.
* **Misleading first attempt:** `jeida-18` with a 65.6 MHz pixel clock lit the panel and showed readable console text, but "black" was a washed-out grey-green. A pure bit-order mismatch cannot do that (black is all zeros), so suspect the electrical/lane side: 18-bit mode drives only three of the four LVDS data lanes of this 24-bit panel. The fix changed the data mapping/depth and the pixel clock together, so which of the two mattered was not isolated.
* **PHY verification:** TCON0 reads back `LVDS_ANA0 = 0xc1f40320` and `LVDS_IF = 0x80100000`, exactly what U-Boot's sequence produces for 24 bit (`0x40040320 | EN_MB | DRVC | DRVD(0xf)`).
* **CMA:** The default 16 MB CMA pool cannot hold the fbdev console buffer plus four 1024×614 dumb buffers; boot with `cma=64M`.
* **Result:** `/sys/class/drm/card0-LVDS-1` is `connected`/`enabled`, `modetest` reports `1024x768 54.87`, SMPTE colour bars are correct, fbcon runs on `sun4i-drmdrmfb`, and an XRGB8888 overlay plane (`modetest -P`) displays 1:1 (no scaling).
* **Boot DTBs:** `sun6i-a31s-exper-t8e-drm.dtb` (DRM, default) and `sun6i-a31s-exper-t8e.dtb` (SimpleFB fallback).

### Finding 48: DE1 Display Frontend (Hardware Scaler) Produces No Output on the A31s
* **Goal:** Scale an 800×480 video plane to the 1024×768 panel in hardware (zero CPU).
* **What was done:** `allwinner,sun6i-a31-display-frontend` is missing from `sun4i_frontend_of_table` (mainline has the DTS nodes and binding but no driver match), so it was added (A10 and A33 quirk data both tried); both frontends bind and `sun4i-backend` finds its frontend. A `frontend_idx` module parameter was used to feed BE0 from FE0 or FE1.
* **Evidence that the registers are right:** read back through `/dev/mem`: input/output sizes, scale factors (`0xc800` = 800/1024), buffer addresses, line strides, input/output formats, the full coefficient tables, `FRM_CTRL = 0x10003` (start, `REG_RDY`, `COEF_RDY`); BE layer 1 has `VDOEN` set with the right size and coordinates; clocks (`fe0` 300 MHz, `dram-fe0`) and resets are enabled.
* **Result:** the overlay stays empty. A scaled RGB plane showed a black area with a thin vertical line, an unscaled NV12 plane showed nothing; no visible picture with either FE0 or FE1. `FRM_CTRL` keeps `REG_RDY`/`COEF_RDY` set and never self-clears, i.e. the frontend never latches the configuration. Manually toggling the unused bits of `BE0_ATTCTL1`, hammering `FRM_CTRL`, and swapping FE0/FE1 changed nothing. No A31 DEFE/DEBE register documentation or BSP source was available.
* **Conclusion:** unresolved; the backend path without the frontend (XRGB overlay planes) works. The frontend patches are deliberately not part of this patch series. Software scaling (NEON bilinear) reaches ~70 fps for 800×480 → 1024×614, FFmpeg `swscale` only 25–40 fps.

### Finding 49: Clean Kernel, Debug-Free Command Line and Boot Time
* **Change:** The bring-up kernel (probes in `head.S`, `setup.c`, `8250_*`, `i2c-mv64xxx`, `printk`, `dd.c`, `brk_raw.h`) was replaced by the patch series in `patches/kernel/` and `configs/exper_t8e_defconfig`: `CONFIG_DEBUG_LL`/`DEBUG_UNCOMPRESS` off, log buffer 128 KB, and `debug initcall_debug ignore_loglevel loglevel=8` replaced by `loglevel=4`.
* **Gotcha:** With `CONFIG_CMDLINE_EXTEND` the built-in command line is appended **after** the bootloader's, so it can not be overridden from `boot.scr`; the debug flags had to be removed from `CONFIG_CMDLINE` itself.
* **Measured on the tablet:** `Run /sbin/init` at 1.76 s and `sun4i-drm` initialised at 4.5 s (the debug kernel took 51.3 s to reach the same point); `dmesg` shrank from thousands of lines to about 300 and the bring-up `Call trace:` dumps are gone. The speed-up was not bisected between `initcall_debug`, `ignore_loglevel` and the probe prints, which all went away together.
* **Build:** `scripts/30-build-kernel.sh` builds out of tree from the defconfig and installs a new modules directory; keep the previous release directory on the card while testing a new kernel, so the old kernel can still be booted by pointing `boot.scr` back at it.

### Finding 50: Userspace `exper-backlight` Must Not Run Next to the Kernel Backlight
* **Mechanism:** `exper-backlight` programs the PIO and PWM registers directly through `/dev/mem`. With the DRM DTB the kernel already owns the PG12 supply (`regulator-fixed`), the PH13 PWM (`pwm-sun4i`) and the PA25 enable (`pwm-backlight`), so touching those registers behind its back can corrupt the PWM state.
* **Fix:** `rootfs/alpine/etc/local.d/01-backlight.start` now runs the helper only when `/sys/class/backlight/backlight` does not exist (SimpleFB boot). `scripts/exper-powerd.c` writes `bl_power` (0 = on, 4 = off) on the backlight class when it is available and falls back to `exper-backlight` otherwise; `EXPER_POWERD_INPUT` and `EXPER_POWERD_BACKLIGHT_DIR` override the paths for testing.

### Finding 51: Cedrus (Cedar Video Engine) on the A31s
* **Starting point:** mainline Cedrus has no A31 compatible and `sun6i-a31.dtsi` has neither a video-codec node nor an SRAM controller node.
* **Facts taken from the vendor `lichee/sun6i-dev` kernel (`platform.h`, `irqs-sun6i.h`, `sun6i_cedar.c`):** VE register base `0x01c0e000`, interrupt SPI 58, SRAM controller at `0x01c00000`; the vendor driver uses per-codec register windows (`0x100`, `0x200`, ...) and a 960 MHz PLL feeding the module clock, so the A20 variant (320 MHz, tiled output, MPEG-2/H.264/VP8) was reused. The SPI numbering was cross-checked against the tablet: LCD0 is SPI 86 and shows up as IRQ 118 in `/proc/interrupts`.
* **Change:** patches 0011-0013 (binding, `sun6i_a31_cedrus_variant`, `video-codec` and `system-control`/`sram-c1` nodes using `CLK_AHB1_VE`, `CLK_VE`, `CLK_DRAM_VE`, `RST_AHB1_VE`).
* **Verified on the tablet (release 7.2.0-gc6b3b99fbed5):** `cedrus 1c0e000.video-codec: Device registered as /dev/video0`, `/dev/media0` exists, interrupt 90 (32 + SPI 58) is registered, no SRAM/clock errors; `v4l2-ctl` lists the `MG2S`, `S264` and `VP8F` output formats, the `ST12` capture format and the stateless H.264/MPEG-2/VP8 controls. CMA stays at 64 MB with 61 MB free.
* **Decode test:** `gst-launch-1.0 filesrc ! qtdemux ! h264parse ! v4l2slh264dec ! ...` (Alpine `gst-plugins-bad` 1.24.10 ships the `v4l2codecs` plugin and finds `cedrus-proc`; it selects `NV12_32L32` 640x384). A 5 s, 640×360, 30 fps x264 clip (Main profile, no B-frames, GOP 30) reaches EOS in 0.27 s with a `fakesink`. Output converted to I420 was compared frame by frame with the FFmpeg software decode: all 150 frames are present, frames 0-59 are bit-exact, and from frame 60 (an I frame) the luma and the V plane are still identical but the U plane differs in a few hundred samples per frame, up to a few thousand later (e.g. frame 60: 135 samples in a 18×9 block, values off by up to 130). The result is identical across three runs, so it is a systematic decode difference, not a race. Cause not found.
* **Narrowing (raw `NV12_32L32` dumped without `videoconvert`, untiled by a script and compared with FFmpeg):** the same errors appear, so `videoconvert` is not responsible. Across six clips (x264 testsrc2 variants with CABAC off, deblocking off, chroma QP offset 0, intra-only, minimal motion search, and a mandelbrot clip) luma is always bit-exact; the mandelbrot clip (Main, B-frames) is exact in all 150 frames; every testsrc2 variant has chroma errors, including the intra-only one, and the High-profile B-frame clip has small V errors. So motion compensation, deblocking, CABAC/CAVLC and the chroma QP offset are not the trigger; the error is content dependent and chroma only.
* **Shape of the error:** in the first failing frame (60) the wrong samples are one U block (chroma rows 128-136, columns 143-160, i.e. around a 16x32 chroma tile corner), the V samples next to them are correct, and the wrong U values are the correct ones plus a nearly constant offset of about +96. That looks like a wrong chroma DC contribution for a block, not stale buffer data (the block matches no earlier frame) and not a displaced copy.
* **Ruled out:** the module clock rate. With `CLK_SET_RATE_PARENT` added to `ve_clk` (experiment, not part of the series) `pll-ve` and `ve` run at the requested 320 MHz instead of 297 MHz, and the decoded output of four of the five test clips is byte-identical to the 297 MHz output. The VE SRAM mapping is also not the cause as far as the register write goes: the vendor driver writes `0x7fffffff` into bits 0-30 of `0x01c00000`, the same write the mainline `sun4i` C1 `ve` mapping performs. One clip (High profile, B-frames) produced a different output on the first decode after boot and the same output as before in three later runs, so there is occasional run-to-run variation as well (cause unknown, possibly uninitialised buffer contents).
* **Register audit (debug build with temporary module parameters, not part of the series):** `VE_VERSION` (offset `0x0f0`, upper 16 bits) reads **0x1633** on the A31s. The userspace `libcedrus`/`libvdpau-sunxi` code knows 0x1625 (A13/A10s class, needs the intra-prediction and deblocking buffers in DRAM) and 0x1639 (A20), so the A31 engine is a separate generation and the "A20 variant" binding is an unverified assumption. The chroma output registers read back as 0 for the tiled `NV12_32L32` format, which is what mainline writes for it (not a missing setting). Moving the H.264 intra-prediction/deblocking buffers into DRAM with mainline's buffer sizes, flipping `VE_MODE` bit 20 (rec write mode) or bits 17:16 (DDR bandwidth) left the U-plane error of the `test` clip unchanged (the `VE_MODE` DDR bandwidth change only altered the `testb` clip slightly). Mainline's DRAM buffer sizes are smaller than the ones `libvdpau-sunxi` uses (for 640 pixels: about 7.7 KB vs 16 KB deblocking, 6.4 KB vs 8.3 KB intra); repeating the experiment with the `libvdpau-sunxi` sizes (confirmed by `VE_BUF_CTRL` reading `0xa`) again left the `test` clip unchanged and made the `testb` clip worse (luma errors appeared), so buffer placement is not the cause.
* **Other codecs:** VP8 decoding of the same `testsrc2` content is bit-exact in all 150 frames, so the VE's chroma output path, tiling and memory access are sound and the problem is specific to the H.264 engine. MPEG-2 differs from FFmpeg's decode in every frame by at most 5 levels; the standard does not require a bit-exact IDCT, so this alone is not evidence of a defect, but it was not investigated further.
* **What triggers the H.264 error:** solid saturated colours (yellow U=16, blue U=240, all at QP 12/26/40, intra only) are bit-exact, as are SMPTE/PAL bars, gradients, `life`, `rgbtestsrc`, `yuvtestsrc` and a mandelbrot clip. Only `testsrc` and `testsrc2` fail. In `testsrc` (intra only, QP 26) the wrong chroma samples sit in the same 7-8 chroma macroblocks in every frame (93 samples, V plane), and they cluster at the ends of the value range: software 16 vs hardware 15, 241 vs 240, or software 12-19 vs hardware 0. Re-encoding `testsrc` with reduced saturation shows the dependence: at saturation 1.0 (decoded chroma reaching 7-249 at QP 12, 5-252 at QP 26, 0-255 at QP 40) the V plane has 1000-1700 wrong samples in 10 frames; at saturation 0.8 and below, with decoded chroma ranges of 27-227 (QP 12/26) and even 8-245 (QP 40), there are no errors at all. So the error appears when the reconstructed chroma overshoots or comes close to the 0/255 limits, i.e. in the saturation/clipping behaviour of the H.264 chroma reconstruction on this engine generation. This is an observation, not a verified cause; no register or driver setting that influences it has been found.
* **Still open (unverified):** whether an A31-specific register setting (the vendor decoder library `libcedarv` is closed source and the vendor kernel driver only exposes the register window) controls chroma clipping, or whether the 0x1633 engine simply behaves this way for H.264 chroma. Testing on real footage (see above) showed that ordinary clips are not reliably decoded correctly either.
* **A31 User Manual V1.3 (checked later):** VE base `0x01C0E000` and VE interrupt 90 (= SPI 58) are confirmed. `SRAM_CTRL_REG0` (offset 0 at `0x01C00000`) bits 30:0 are "SRAM C1 map" (0 = CPU/DMA, 1 = VE, default `0x7fffffff`, i.e. already mapped to VE) and C1 is 50 KB; the memory map lists "SRAM Area C" as `0x01D00000`-`0x01DFFFFF` (shared by C1 = VE, C2 = ACE, C3 = ISP). So the SRAM claim is correct, and the `0x80000` window size in the DTS (copied from the A23) is not from the manual, but it is only used to name the claimable section. PLL4 (PLL-VE) must stay within 30-600 MHz (default 297 MHz); the vendor kernel's 960 MHz `pll4clk_rate` is outside that range, and the VE clock is PLL4 divided by 1-8. The manual has no register description for the video engine itself, so register-level behaviour cannot be checked against it.
* **linux-sunxi "VE Register guide" (Wayback copy):** the `VE_VERSION` table reads 0x16230055 (A10/A20), 0x16250055 (A13), **0x16330040 (A31s)**, 0x16390028 (A80), 0x16670040 (A33), 0x16800040 (H3), 0x16890040 (A64), 0x17180040 (H5), so the A31s value matches the tablet and the engine is a separate generation between the A13 and the A33 (an earlier statement here that 0x1639 is the A20 was wrong, it is the A80). The guide's H.264 register list matches what mainline programs. It states for `0x50`/`0x54`/`0x58` (intra-prediction buffer, deblocking buffer, with SRAM/mixed/DRAM selection) that the DRAM buffers are "required for A13+ SoC for H264 decoding" (on the A10 only for widths >= 2048); mainline Cedrus enables them only for width > 2048.
* **Real-content test (three 640x360 High-profile clips with B-frames from test-videos.co.uk, compared with FFmpeg's decode, default SRAM buffers):** Big Buck Bunny 300/300 frames bit-exact; Sintel 239/240 exact (frame 13 has 515 wrong luma samples, up to 167 levels); Jellyfish only 56/300 frames exact (244 frames wrong, luma errors up to 132 levels in small regions starting at frame 5, propagating through references). This corrects the earlier expectation that ordinary clips decode correctly. The errors are real decode errors (no frame matches any other FFmpeg frame), not reordering. Re-encoding the Jellyfish content with x264 options showed errors only in B-frame encodes (no-B, single-reference and intra-only encodes are exact), usually in one frame.
* **DRAM intra/deblock buffers (`VE_BUF_CTRL` = 0xa, buffer sizes as in `libvdpau-sunxi`):** the picture changes completely instead of improving: Jellyfish becomes 300/300 exact and the `dspatial`/`dtemporal` re-encodes become exact, but Big Buck Bunny (previously exact) gets 242 wrong frames, and the `nob` and `noweight` re-encodes are wrong from the first frame. A setting that fixes some streams and breaks others, with failures from the very first (intra) frame, looks like a buffer/memory problem rather than a decoding rule, but the intermittency of the stock behaviour (see below) means this single comparison cannot be trusted. * **Zeroed buffers (debug parameter that drops `DMA_ATTR_NO_KERNEL_MAPPING` so the kernel clears the memory) - not conclusive:** in the file-writing test flow (decode to a 110 MB file on the SD card, copy to the PC, compare with FFmpeg) the stock behaviour produced errors in most boots and runs, and they changed from run to run (Jellyfish 244 wrong frames, later 55, later 0; Big Buck Bunny 223 wrong frames in one run, 0 in the others; Sintel 1 wrong frame in some runs), whereas runs with zeroed buffers were bit-exact every time (about a dozen runs, including each buffer zeroed on its own). A second, faster flow (decode to a pipe and compare the MD5 with a verified golden output, 5 configurations x 45 decodes) then showed **no** errors for the stock behaviour either (0/45), and an earlier shorter run showed 2 failures in 15 stock decodes against 0 in 15 zeroed ones. Overall the stock behaviour failed in 2 of 60 pipe decodes and the zeroed one in 0 of 60, which is not a significant difference. So the errors are intermittent and depend on the test flow (they showed up mostly when large files were being written to the SD card); zeroing the buffers is **not** established as the fix, and uninitialised buffer memory is only one candidate (cache coherency of the decoded frames and system load are others).
* **Retest on a charged battery (stock behaviour, pipe/MD5 flow, `bbb`/`jelly`/`sintel` clips):** with the battery at 70-76 % (about 3.85 V under load) the stock decode matched the golden MD5 in 30 of 30 and then 90 of 90 decodes, and the zeroed-buffer mode in 30 of 30; dmesg shows no cedrus errors. The earlier failures (2 of 60 stock decodes, and the many errors in the file-writing flow) happened while the tablet ran from a nearly empty battery on a PC USB port, which cannot cover the load (battery shown as discharging at about 200-290 mA even on USB). This is consistent with a power-related cause but is **not proven**: the errors were not reproduced on purpose by lowering the supply, and 0/120 does not exclude a rare fault.
* **How relevant is the chroma limit defect for real footage:** the decoded chroma of the three real clips stays far from the limits (U/V range 36-187 for Big Buck Bunny, 72-210 for Jellyfish, 110-154 for Sintel; no sample outside the nominal 16-240 range in 34.6 M, 34.6 M and 27.6 M samples), whereas the synthetic `testsrc` clip has 153 369 of 17.3 M chroma samples outside 16-240 (range 0-255). Together with the earlier saturation sweep (no errors at saturation 0.8 and below), the defect only shows up for content whose chroma reaches or leaves the nominal range, which natural video does not do. This is a statement about three clips, not a proof that out-of-range chroma never occurs in real content (e.g. heavily graded or synthetic material).
* **A second, separate effect remains:** the deterministic chroma errors at the 0/255 limits of the synthetic `testsrc`/`testsrc2` patterns (saturation 1.0) are not changed by zeroed buffers; they are identical with and without them.
* **Not verified:** MPEG-2 decoding bit-exactness; the exact offset of C1 inside SRAM Area C (copied from the A23, `0x80000`); the module clock: `ve` stays at 297 MHz (the PLL keeps U-Boot's setting, `ve_clk` has no `CLK_SET_RATE_PARENT`), not the requested 320 MHz.
* **Userspace:** Alpine's FFmpeg does not use stateless V4L2 requests, so a decode test needs a request-capable client.

### Finding 52: The A31s GPU Is a PowerVR SGX, Not a Mali
* **Observation:** the vendor `lichee/sun6i-dev` kernel carries the PowerVR driver (`drivers/gpu/pvr/services4/system/sunxi`) and no Mali driver; mainline `sun6i-a31.dtsi` has no GPU node. Mali-400 (Lima) is found on the A10/A20, not here.
* **Consequence:** there is no mainline driver for this GPU, so hardware 3D acceleration is not available with this port; the `CONFIG_DRM_LIMA` option in the defconfig has nothing to bind to. The GPU register base (`0x01c40000`) and interrupt (starting at SPI 97) come from the vendor headers. The Allwinner A31 datasheet (v1.5 and v1.3) lists the GPU as "Power VR SGX544 MP2"; the SGX model was not read back from the tablet itself. The datasheet also gives the video engine feature list (H.264 BP/MP/HP, MPEG-1/2/4, VP6/8, JPEG decode up to 4096x2304) but no register map, and it does not state the size of SRAM C.

### Finding 53: HDMI Output on the A31s
* **Starting point:** `sun6i-a31.dtsi` has the `hdmi@1c16000` node (`allwinner,sun6i-a31-hdmi`) but it is disabled, the board DTS deletes `simplefb_hdmi`, `CONFIG_DRM_SUN4I_HDMI` was off and the connected hardware showed only `LVDS-1`. The vendor fex has no HDMI section and sets `screen0_output_type = 1` (LCD), so the existence of the connector was confirmed by the user, not by the vendor files.
* **Change:** patch 0014 (an `hdmi-connector` node and `&hdmi`/`&hdmi_out` in `sun6i-a31s-exper-t8e-drm.dts`, as on `sun6i-a31-hummingbird.dts`) and `CONFIG_DRM_SUN4I_HDMI=m`.
* **Verified on the tablet with a monitor attached:** `dmesg` shows `bound 1c16000.hdmi`; `card0-HDMI-A-1` is `connected` with EDID modes from 640×480 up to 1920×1080; the DRM state shows `crtc-0` on `LVDS-1` at 1024×768 and `crtc-1` on `HDMI-A-1` at 1920×1080 (148.5 MHz); the monitor shows the console and the tablet panel is not cropped. Unplugging and replugging the cable restores the picture.
* **Not tested:** HDMI audio (`/proc/asound/cards` lists only the A31 audio codec), CEC, resolutions other than the default, running a graphical client on HDMI.

### Finding 54: Suspend to Idle (s2idle) and the Wi-Fi Chip
* **State:** `/sys/power/mem_sleep` shows only `[s2idle]` (PSCI v0.1 from the DT, no platform suspend), so there is no deep sleep; `freeze` is the only suspend state. `/sys/power/state` lists `freeze mem` but `mem` falls back to s2idle.
* **Problem found (30 s `rtcwake -m freeze` with Wi-Fi down):** the kernel side succeeds (`suspend_stats` success 1, no failed phase) but `brcmfmac` logs `brcmf_ops_sdio_suspend: Failed to set pm_flags 1` on suspend and `brcmf_sdio_htclk: HT Avail request error: -110` plus scan errors after resume. Cause: the `sunxi-mmc` host gets `MMC_PM_KEEP_POWER` only from the `keep-power-in-suspend` DT property, which the `&mmc1` node lacked.
* **Fix:** patch 0015 adds `keep-power-in-suspend` to `&mmc1`.
* **Verified on the tablet:** with `wlan0` up, a 30 s s2idle shows no brcmfmac messages and a scan works afterwards (12 SSIDs before, 9 after in the first test, 13 in a later manual scan). A scan issued immediately after resume once returned 0 SSIDs without any dmesg error, and a scan a minute later found 13, so scan right after resume is not reliable. A 180 s RTC-alarm suspend ended after 63 s when the power button was pressed and the screen came back to the login immediately; the wake source counters did not show `axp221-pek` incrementing, so the button as the wake source is likely but not proven.
* **Not tested / open:** the Wi-Fi host-wake line as a wake source; power drawn during s2idle (the USB supply makes the battery current unreadable); the touch screen is not a wake source; the USB gadget link came back on its own after one resume but needed the cable to be replugged after two others, so it is unreliable; `exper-powerd` still only toggles the backlight and does not suspend the system.

### Finding 55: Idle Power Draw on the Battery
* **Method:** `/sys/class/power_supply/axp20x-battery/current_now` read every 2 s with the USB cable unplugged (with a USB supply the value is only the battery's share and means little). `exper-powerd` was paused with `SIGSTOP` for the runs, because its `loss_poweroff_s=300` rule (external power lost for 300 s) powers the tablet off; the first two attempts were cut short by it. Wi-Fi was down (`wlan0` DOWN) throughout.
* **Idle, backlight off (`bl_power=4`), 4 cores online:** about 490-520 mA at 3.70-3.76 V, i.e. roughly 1.8-1.9 W.
* **By component (15 samples each):** panel blanked through `fb0/blank` as well: 486 mA (-33 mA); additionally CPU1-3 offline: 425 mA (-61 mA); everything restored with the backlight still off: 506 mA. The drift between the first and last identical state was 13 mA.
* **s2idle:** after every resume `bl_power` reads 0 again (12 of 12 resumes), because `panel-lvds` enables the backlight when the panel is enabled; a wake therefore always turns the screen on. Samples taken 3 s and 8 s after a resume (backlight switched off again) averaged 495 mA and 482 mA, the same as the idle state. The current **while suspended** could not be measured this way, so the benefit of s2idle is still unknown.
* **Reading:** about 425 mA remain with the panel off and one core online, so most of the draw is elsewhere (not identified: DRAM, PLLs, the Wi-Fi chip's 3.3 V rail, USB gadget PHY, audio codec, touch controller are candidates). The three idle cores account for about 60 mA, which suggests that CPU idle states and DVFS (cpufreq is currently disabled, the CPU runs at 1008 MHz / 1.2 V) matter for the idle draw.
* **Not tested:** other component switch-offs, the Wi-Fi associated state, and a measurement while suspended (needs an external meter or a long suspend compared by battery gauge).

### Finding 56: Charging from a PC USB Port and the AXP221 VBUS Input Limit
* **Observation:** on a PC USB port the battery was reported as `Discharging` (about -370 to -410 mA, 41 %, 3.74 V) with the screen on. `axp20x-usb` and `axp22x-ac` both report `online=1`; PMIC register `0x00` reads `0xfb` (ACIN and VBUS present and usable, battery current direction = discharge). Register `0x30` (VBUS-IPSOUT path) read `0x61`, i.e. VBUS current limit 500 mA (bits 1:0 = `01`), and `axp20x-usb/input_current_limit` read `500000`. Registers were read through `/sys/kernel/debug/regmap/2-0068/registers`.
* **Change:** writing `900000` to `axp20x-usb/input_current_limit` sets register `0x30` to `0x63` (limit field `11`, no limit; sysfs then reads `-1`, and writing `500000` afterwards did not change it). VHOLD (bit 6, 4.4 V) stays enabled, so a weak host that sags below 4.4 V is throttled instead of browned out. Since the value is not kept across boots, it is written by a boot script (an OpenRC service in the tablet's userspace). It was checked that register `0x30` reads `0x63` again after a reboot.
* **Effect (same session, screen on, USB):** the battery discharge fell from about -400 mA to about -180 to -215 mA. With the backlight off (`brightness 0`) the battery was `Charging` at +150 to +200 mA, so the charge path works.
* **Reading:** the tablet with the screen on draws more than a PC USB port can provide (0.5 A at 5 V is 2.5 W; a USB 3 port about 4.5 W), so the battery still covers the difference. This is an inference from the measured currents, the port's actual limit was not measured; the AXP221 ADC driver here exposes only battery channels, so the input current is not readable. A 5 V / 2 A adapter on the same Micro-USB port is the case this board is meant for.
* **Userspace mitigation:** the power daemon dims the backlight after 60 s without touch and blanks it after 180 s, unless a video session is running, so an idle tablet on a weak USB port nets a charge (verified: `Charging`, +150 mA with the screen blanked, and the touch screen wakes it at normal brightness).
* **Not tested:** the charge current register `0x33` (reads `0xd8`) and the end-of-charge voltage were only read, not changed or checked against the battery; gauge calibration (earlier 0 % at 3.50 V) was not examined; a PC port that advertises a charging mode (CDP) was not tried.

### Finding 57: Idle Power, Second Round (what is not the cause)
* **Method:** same as Finding 55 (USB unplugged, `axp20x-battery/current_now` every 2 s, 12 samples per state after a 6 s settle, `exper-powerd`/`exper-auto` paused with `SIGSTOP`, a script on the tablet logging to a file). The log also records whether `axp20x-usb`/`axp22x-ac` were online during each state. The first state of the run (a baseline with the panel off) is **invalid**, because the cable was still being unplugged (external power seen), so the state after everything was restored is used as the baseline.
* **Results (backlight off, panel off via `fb0/blank=4`, `wlan0` down):** everything restored 472 mA (min 459, max 515); Wi-Fi SDIO host unbound 453 mA (-19); additionally the EHCI host unbound 442 mA (-11, close to the spread of the samples, so only a hint); additionally CPU1-3 offline 382 mA (-60); additionally `wpa_supplicant` stopped 378 mA (-4, within the noise). Battery 3.72-3.73 V throughout.
* **Switches that worked cleanly:** `echo 1c10000.mmc > /sys/bus/platform/drivers/sunxi-mmc/unbind` removes the card and drops the `vcc-wifi` rail (regulator use count 2 → 1); `bind` brings `wlan0` back with the firmware loaded (the usual `p2p-dev-wlan0 ... failed` message appears as at boot). The EHCI host (`1c1a000.usb`) can be unbound and bound the same way.
* **Not the cause:** the display pipeline. `fb0/blank=4` sets `crtc-0` to `active=0` and `lcd-power`/`panel-power` to use count 0 (read from `/sys/kernel/debug/dri/0/state` and the regulator summary), so the 33 mA of Finding 55 already includes the panel rail. Wake-ups are low: about 115 arch-timer interrupts per second over four cores, 79 IPIs and 44 `sunxi-mmc` interrupts per second, about 250 per second in total.
* **What remains:** about 378 mA (about 1.4 W at 3.73 V) with one core, no panel, no Wi-Fi rail and the USB host off. The contributors left are the SoC base consumption at the fixed 1.2 V / 1008 MHz (`pll-cpu` 1008 MHz, `pll-ddr` 864 MHz, `pll-periph` 600/1200 MHz), the DRAM at 432 MHz and the PMIC itself; these were **not separated** and the split is unknown. The kernel has no `cpuidle` (no `/sys/devices/system/cpu/cpuidle`; PSCI v0.1 only), so an idle core only executes WFI and s2idle cannot reach a deeper state than ordinary idle, which agrees with Finding 54 (suspend benefit unmeasured).
* **Hazard found:** `rmmod sun4i_drm` (with the tcon/backend/frontend modules) oopses in `drm_connector_free_work_fn` ("bad PC value"), removes `/dev/dri` and leaves `rmmod` and later `reboot` stuck; the kernel has no `CONFIG_MAGIC_SYSRQ` (`/proc/sysrq-trigger` is missing), so only a hard power cycle recovered it. Do not unload the DRM modules; use `fb0/blank` or DPMS instead.
* **Not tested:** the AXP221 coulomb counter (registers `0xB0`-`0xB7` are not in the regmap read table, only `0xB8`-`0xBA` are listed), the DRAM and CPU frequency/voltage (needs cpufreq, Finding 46), the audio codec and touch controller, and the draw while suspended.

### Finding 58: cpufreq / DVFS Works After a PLL Settle Delay (patches 0016, 0017)
* **Problem:** with the stock reference-design `cpu0` node (`operating-points` plus `cpu-supply = <&reg_dcdc3>`), the boot stopped without any message right after `cpufreq-dt` bound (about 3.5 s). Earlier attempts (Findings 46 and 55) had no serial console and could not see where.
* **Method:** a CH341 USB-UART on the debug pads (115200 on `ttyS0`) and a small script that interrupts U-Boot's autoboot and boots an alternative DTB and kernel from the SD card by hand (the default boot files are never touched, so a power cycle always returns to the old setup). Variants were made with `fdt rm` in U-Boot; debug kernels carried `pr_emerg` traces.
* **What the runs showed:** (1) stock node with `cpu-supply`: silent total hang (the screen froze on the console text); (2) the same without `cpu-supply`: one run ended in `sun6i-a31-ccu: Runtime PM usage count underflow!` followed by `stack-protector: Kernel stack is corrupted in _dev_warn` in `sugov:0` on the older `zImage-new`, but a later build without `cpu-supply` booted and scaled fine, so that panic was **not reproduced and its cause is unknown**; (3) traces in the OPP and regulator code showed the hang is in the CPU clock change, not in the voltage step: the first change (schedutil asks for 720 or 864 MHz) stops after `_set_opp` begins and before any `set_voltage` line; (4) with traces inside the PLL write and the CPU mux notifier the tablet booted and made 34 clean transitions between 480, 720, 864 and 1008 MHz in 35 s.
* **Cause (inferred, not proven):** `pll-cpu` (reg 0x0, N/K/M) is rewritten while the CPU runs from the 24 MHz bypass (the mux notifier switches `cpu` from `pll-cpu`, reg 0x050 value 0x20102, to `osc24M`, 0x10102, and back). The trace shows the lock bit (bit 28) already set right after the write (`0x90001110` straight after `0x90001410 -> 0x90001110`), so `ccu_helper_wait_for_lock()` returns at once and the mux switches back to a PLL that has not settled. The slow trace output kept the CPU on the bypass long enough, which is why the heavily traced kernel did not hang. The register values themselves were sane (for example 864 MHz = N=18, K=2, M=1; U-Boot's 1008 MHz is N=21, K=4, M=2).
* **Fix:** patch 0016 adds `CCU_FEATURE_LOCK_DELAY` and waits 2 ms before the lock bit is polled, for the A31 `pll-cpu` only; patch 0017 removes the `&cpu0` block from the board DTS. A 100 us delay was **not** enough (still hung), 2 ms works; the smallest working value between them was not determined, so 2 ms is empirical.
* **Verified on the tablet** (release `7.2.0-gcdce07b71f1c`, built with `scripts/30-build-kernel.sh`, booted by hand through U-Boot): schedutil with `scaling_available_frequencies` 480000 720000 864000 1008000; two stress runs of 3 and 5 minutes (4 busy loops in bursts, 180 and 300 frequency samples spread over all four OPPs) with no hang and no kernel warning; `vdd-cpu` read 1200/1100/1000 mV at 1008/720/480 MHz; display, touch and Wi-Fi modules came up normally.
* **Idle draw on the battery** (USB unplugged, panel off, Wi-Fi down, 4 cores online, 12 samples per state, frequency pinned through `scaling_min/max_freq`): 1008 MHz 478 mA, 720 MHz 424 mA, 480 MHz 389 mA, 1008 MHz again 465 mA (so the drift is about 13 mA), unpinned schedutil idle about 399 mA. That is about 80 mA (17 %) less than with the CPU fixed at 1008 MHz / 1.2 V.
* **Not tested:** a long soak (hours), suspend and resume with cpufreq, the Android Auto workload and video decoding with scaling, thermal behaviour, the lowest delay that is still safe, and the earlier stack-protector panic. These results come from a kernel that was booted by hand; whether the default boot switches to it is a separate step.

### Finding 59: HDMI Audio (patch 0018)
* **Problem:** mainline `sun4i-hdmi` had no audio path (no hdmi-codec, no DAI, no DMA), although the A31 `hdmi` node already lists `dmas = <&dma 14>` as `audio-tx` (DRQ port 14 is "HDMI AUDIO" in the A31 user manual).
* **Design:** the registers are those of the A31 user manual (base 0x01C16000): `AUD_CTRL` 0x040, `ADMA_CTRL` 0x044, `AUD_FMT` 0x048, `AUD_CTS` 0x050, `AUD_N` 0x054, channel status 0x058/0x05c, audio infoframe 0x0a0, FIFO port 0x400. The new `sun4i_hdmi_audio.c` follows the structure of `vc4_hdmi`: an hdmi-codec from `drm_connector_hdmi_audio_init()`, a CPU DAI plus `devm_snd_dmaengine_pcm_register()` on the FIFO port, and an in-driver sound card. `prepare` disables and resets the audio module, programs normal-DMA mode (16-bit: two samples per 32-bit word; 24-bit: one per word), channel count, N/CTS (`drm_hdmi_acr_get_n_cts()`), channel status (the IEC 60958 bytes map straight onto the two registers) and enables it. The packet arbiter (`PKT_CTRL0`) then sends the audio infoframe and the audio related packets (slots 1 and 2) next to the AVI infoframe. The hdmi-codec needs `drm_connector_hdmi_audio_plugged_notify()` after the EDID read, otherwise its ELD stays empty.
* **Bugs found on the tablet:** (1) `AUD_FIFO_CLEAR` (ADMA_CTRL bit 15) is a level, not a self-clearing pulse: with it left set `aplay` finished 5 s of audio in 0.04 s because the FIFO was held empty and the DMA never waited. Setting it once, then writing the final value without it, gives correct pacing. (2) The old `PKT_CTRL0` write in `sun4i_hdmi_enable()` reused `val` from the timing polarity register, so the HSYNC/VSYNC bits leaked into the first packet type field (a positive/positive sync mode would have sent packet type 3 instead of the AVI infoframe) and into the frequency fields; the new `sun4i_hdmi_update_packets()` builds the register from scratch.
* **Verified** (release `7.2.0-gb7cf3f7cd4c7`, monitor `24G2WG3` on the HDMI port, 1080p): the ELD shows LPCM, 2 channels, FL/FR; `aplay` pacing: 3 s of silence at 32, 44.1 and 48 kHz takes 3 s in S16_LE and S24_LE; a 440 Hz `speaker-test` sine at 48 kHz was heard clearly and cleanly on the monitor speakers; the registers during a stream read `AUD_CTRL` 0x80000000, `PKT_CTRL0` 0x0000f432, N 6144 and CTS 148500 (1080p, 48 kHz).
* **Rates above 48 kHz do not work:** 88.2 and 96 kHz streams take about 5 to 6 s for 3 s of audio, 176.4 and 192 kHz about 12 s, so the FIFO is consumed at a rate that does not follow N/CTS there (cause not found; the sink also only advertises up to 48 kHz). The driver only offers 32, 44.1 and 48 kHz.
* **Not tested:** audio quality beyond a sine (music, level, long streams, underrun recovery), 44.1 and 32 kHz by ear (only timed), unplug and replug during playback, suspend and resume with a stream open, more than two channels, IEC 61937 pass-through, a sink without audio (DVI), and any other A10/A20/A31 board (only the A31s tablet was used).

### Finding 60: GPU (SGX544 MP2) Bring-Up: Register Access (Solved After Four Rounds)
* **Goal:** see whether the PowerVR SGX544 MP2 (A31, rev 115) can be brought up on mainline. Mainline already documents it (`allwinner,sun6i-a31-gpu` in `img,powervr-sgx.yaml`: reg `0x01c40000`, IRQ SPI 97, clocks `core`/`mem`) but has no SGX driver, and the open `drm/imagination` driver only covers Rogue. A closed user-space stack would also be needed (not part of this repo).
* **What the stock system is:** the original Android 4.4.2 (kernel 3.3.0, still on the tablet's NAND) loads `pvrsrvkm.ko` reporting `SGX_DDK_Android_RSCompute sgxddk 1.12`, SGX revision 1.1.5. The linux-sunxi `lichee/sun6i-dev` tree only carries the older DDK 1.9.RC2 kernel part, so its source does not match the 1.12 user-space libraries. Read through adb on the stock system (BusyBox `devmem`) while the GPU was active: `CORE_ID` (0x01c40020) `0x01191201`, `CORE_REVISION` (0x24) `0x00010105`.
* **Stock power-up sequence** (from the disassembly of the unstripped stock `pvrsrvkm.ko`, functions `EnableSystemClocks`/`EnableSGXClocks`): enable the `axp22_dcdc2` regulator (the GPU rail, 1.2 V in the vendor config); clear bit 0 of PRCM `GPU_PWROFF_GATING` (0x01f01518); wait 2 ms; release the GPU reset (CCU 0x2c4 bit 20); enable the two PLLs; then enable the clock gates (AHB gate CCU 0x64 bit 20, core 0x1a0, mem 0x1a4, hyd 0x1a8). The core clock comes from PLL9 (CCU 0x44), memory and hyd from PLL8 (0x38). Working values on the stock system: PLL8 `0x93000d00` (about 336 MHz), PLL9 `0x93001801` (300 MHz), core `0x84000000`, mem and hyd `0x80000000`. Power-off is the reverse, with PRCM isolation set again and the regulator disabled. The stock driver also gates the clocks dynamically with activity; the reset stays released.
* **What was tried on Linux** (a throw-away module on a test DT node, not in the series): rail at 1.1 V and 1.2 V, clocks through the clock framework and as raw register writes with the stock values, reset before and after the clocks, PRCM isolation cleared, the system-control `GPU_SW_CLK_GATING` (0x01c00054) written, and the undocumented read/write bits 21 and 22 of CCU 0x64 and 0x2c4 (set on stock Android, clear on Linux) set. Everything up to the read matched the stock state (PLLs locked, clocks on, reset released, power status "on", isolation off).
* **Result:** in five runs the first read of a GPU register (`CORE_ID`) never returned and hung the whole tablet (only a hard power cycle helps). On the stock system the same read returns 0 with the GPU gated and the real ID when it is active, so the register block is reachable there. A diff of all CCU, system-control and PRCM registers (stock active vs Linux after the sequence) showed only display, DRAM-gating and unrelated PLL/gate differences, none that explains it. **The cause is not found.** Untested ideas: DRAM controller/MBUS port settings, the display engine state, the stock U-Boot/boot0 handover, and a JTAG or logic-level look at the bus.
* **Second round (bisecting, and the same test on stock Android):** (1) With the GPU untouched, a read of `CORE_ID` on Linux returns 0 and does not hang, like the stock system with the GPU gated; the hang starts only when the AHB gate (CCU 0x64 bit 20) is opened, whatever else is on (core clock first, then AHB; all four clocks; hyd off; core and mem only). (2) The stock order of the four gates from the disassembly is hyd, core, AHB, mem; replaying it did not help. (3) Keeping `vdd-gpu` on from boot (`regulator-always-on`, the kernel otherwise cuts it about 30 s after boot), booting Linux in the **secure** world (U-Boot `bootm_boot_mode=sec`, `maxcpus=1`; the TZPC register at 0x01c23400 then reads `0x10` like on Android, and `0` when non-secure) and setting the SDRAM AHB gate (CCU 0x60 bit 14, set on Android) all made no difference. (4) **The same hang happens on stock Android**: from its idle state (GPU gated, reset released), replaying the sequence (reset assert/release, core and mem clocks, AHB gate, then a read) hangs the stock system as well, so the Linux environment is not the cause. (5) A read of the MP master ID (`EUR_CR_MASTER_CORE_ID`, 0x4010) hangs too, while *writes* to the master block (0x4080 soft reset 0x5ff, 0x4c00, 0x4d00, 0x4d04, then 0x4080 = 0) complete. (6) While the stock driver itself drives the GPU (screen scrolling), reads never hang: they return real values (`CORE_ID` and `MASTER_CORE_ID` both `0x01191201`) or zeros, with the AHB gate and core clock on and mem/hyd toggling; `EUR_CR_CLKGATECTL` read `0x002AAAAA` in one catch and 0 in another. (7) The stock `SGXReset` writes, in order: master soft reset (0x4080) = 0x5ff, master BIF ctrl (0x4c00) = 0, master SLC ctrl (0x4d00) = 0x4c000, 0x4d04 = 0x4000000, wait, 0x4080 = 0, wait, then the core `EUR_CR_CLKGATECTL`/`CLKGATECTL2` (0x0, 0x4) with values from the user-space init library, wait, and a series of core registers at 0xc00..0xc84; none of these values are known to me beyond the master block. (8) Clearing `SW_IDLE_GATE` (system control 0x54 bit 0) before the read on Android did not help either.
* **State at the end of the investigation:** the missing piece is something the stock driver does between enabling the clocks and the first read that a manual replay does not reproduce (most likely the core clock-gating setup from the user-space init values, or a handshake that needs the real driver's timing). A sunxi watchdog reset recovered the tablet from some of these hangs but not reliably, so each experiment cost a manual power cycle. About fourteen hangs were spent in total; the work was stopped there. Next steps if ever resumed: extract the user-space init command list (`libsrv_init.so`) for the exact `CLKGATECTL` values, or trace the stock driver's register writes with a hardware or kernel tracer.
* **Third round (2026-10-31): the master bank reads.** (1) The user-space init library (`libsrv_init.so`, `SrvInit`) sets `CLKGATECTL`/`CLKGATECTL2` to `0x002AAAAA`/`0x0A8A8AAA` (every field AUTO, the default) or to `0x10155555`/`0x05454555` (every field ON) when the `DisableClockGating` app hint is set. It also names the master clock-gate status registers 0x4008 and 0x4024. The `0x002AAAAA` caught on stock Android is the AUTO value. (2) The SGX MP register window has banks: 0x0000-0x3fff is broadcast (a write reaches every core), 0x4000-0x7fff is the master, and core *n* is at `0x8000 + n * 0x4000` (`SGX_MP_CORE_SELECT` in the vendor headers, and the same `(n + 2) << 14` in the stock `SGXDumpDebugReg`). Every earlier probe read `CORE_ID` at 0x20, in the broadcast bank. (3) After the stock power-up sequence (PLL8 and PLL9 enabled explicitly, then the hyd, core, AHB and mem gates), reads of the **master bank work on Linux**, with or without the stock `SGXReset` writes: `MASTER_CORE_ID` (0x4010) `0x01191201`, `MASTER_CORE_REVISION` (0x4014) `0x00010105`, `MASTER_CORE` (0x4000) `3` (writable), 0x4008 `0x0000000c`, 0x4024 `0`. This is reproducible (eight runs). With the CCU core clock gated off, the same master read hangs, so the master runs on the core clock. (4) A read of broadcast 0x20 right after a working master read still hangs, and so do a read of core 0's bank (0x8020) and a *write* into a core bank (0x8000), even after `CLKGATECTL` = all ON was broadcast. 0x4008 stays `0x0000000c` whatever the clock-gate values, the `MASTER_CORE` value (3 or 1) and the core clock source (PLL9, PLL8 or PLL_PERIPH). This round's hangs were all recovered by the watchdog. The contradiction with item (5) of the second round (a hanging 0x4010 read) is unexplained. It may have been a different clock state (PLL9 not explicitly enabled). **Open problem:** the two cores do not respond while the master does. Next step: read the master registers (now known to be safe) on stock Android while the GPU is active, to see what 0x4000/0x4008/0x4024 look like when the cores work.
* **Stock comparison (2026-10-31):** a small static sampler on stock Android mapped the CCU and the GPU window and read only while the AHB, core and mem gates were all on, re-checking afterwards (the driver toggles the gates within milliseconds, too fast for shell `devmem` loops). With SurfaceFlinger forced to GLES composition and a swipe loop: `MASTER_CORE` `1` (sometimes 0), 0x4008 `0x0c` when idle and `0x0d`/`0x0f` while working, so its bits 0-1 are core activity and the Linux `0x0c` only means "idle". Core banks returned real values: core 0 `CLKGATECTL`/`CLKGATECTL2` `0x002aaaaa`/`0x0a8a8aaa` (the AUTO values from `libsrv_init`), `CORE_ID` `0x01191201` on both cores. All three good core-bank reads were taken while a core was busy, and the fourth hung the stock system. On Linux, `MASTER_CORE` = 1 (the stock value; Linux starts at 3) with the AUTO values still hangs at the first core-bank read. The theory at this point (a core bank answers only while that core is busy) turned out to be wrong; see the fourth round.
* **Fourth round (2026-10-31): solved.** The stock `SGXInitialise` runs an "init script part 1" **before** `SGXReset`. The user-space init library builds it as 12-byte `{op, offset, value}` records (op 1 write, 2 read, 4 halt): `0x4000` (`MASTER_CORE`) = number of cores − 1, read back; `0x4020` = `0x2aa`, read back; `0x4004` = `0xa`; halt. Neither 0x4004 nor 0x4020 is in the DDK 1.9 register headers. 0x4004 reads 0 after reset; `0xa` is two 2-bit "AUTO" fields, one per core, matching the per-core activity bits of status 0x4008, so it is most likely the master's per-core clock-gate control, OFF at reset. `0x2aa` (five AUTO fields) matches the 5-bit status register 0x4024. With this script inserted between the clock bring-up and the `SGXReset` writes (plus `EUR_CR_POWER` (0x1c) = 0 as the driver does), Linux reads both cores: `CORE_ID` `0x01191201` and `CORE_REVISION` `0x00010105` on core 0 (0x8000 bank) and core 1 (0xc000 bank), and their `CLKGATECTL`/`CLKGATECTL2` read back the broadcast AUTO values `0x002aaaaa`/`0x0a8a8aaa`. No hang. Which of the two writes is strictly required was not separated. Every earlier hang (on Linux and in the hand replay on stock Android, which reset the GPU) fits this: after a GPU reset the cores stay clock-gated until 0x4004 is written. The init script part 2, which runs after the reset, sets up the microkernel (USE code bases, event kicker, per-core and master BIF/event registers) and needs device memory and the proprietary microkernel programs.
* **Conclusion:** hardware 3D is not available with this port. Register access now works, but the way forward would be a port of an SGX 1.12-compatible kernel driver (for example from the openpvrsgx project) to 7.x plus the proprietary user-space libraries and an Android-bionic compatibility layer, which is out of scope here.

### Finding 61: SGX544 MP2 on Linux 7.2: Kernel Driver Port and First GLES2 Rendering
* **Route:** Imagination's DDK must match exactly between the kernel module and the user-space libraries (DDK build number, build options, shared structure layouts), so the stock Android 1.12@2681055 libraries would have needed a kernel driver patched to their layouts plus libhybris. Instead the port uses TI's DDK **1.17.4948957 "Mesa"** stack, where both halves are published: the GPL/MIT kernel driver (`git.ti.com/graphics/omap5-sgx-ddk-linux`, branch `1.17.4948957/mesa/k6.1`) and the prebuilt glibc user space (`omap5-sgx-ddk-um-linux`, branch `1.17.4948957/mesa/glibc-2.35`, target `ti572x`, i.e. DRA7/AM57 with the same SGX544 MP2, core revision 1.1.6).
* **Kernel side** (local, not in this repo yet): Linux 7.2 needed a kbuild fix (since 6.13 external modules are built from inside `M=`, which broke IMG's source-symlink rules), a small compat header (`in_irq`, `from_timer`, `del_timer_sync`, `strncpy`), the string form of `MODULE_IMPORT_NS("DMA_BUF")`, the new `vmf_insert_mixed()` signature, the removed `drm_driver.date`, and no `__GFP_HIGHMEM` for `__vmalloc`. A new `sunxi` system layer replaces TI's runtime-PM/OCP glue with the power-up from Finding 60 (rail, PRCM, reset, PLL8/PLL9, gates in stock order); TI's OCP wrapper registers at 0xFE00 must not be touched on the A31 (that offset is inside core 1's bank). The build keeps TI's options (SGX544, MP2, rev 116) so that it matches the user space; the core-revision check in `SGXDevInitCompatCheck` gets an exception for hardware rev 1.1.5 with software rev 1.1.6 (its exception loop also only ever checked the first pair, fixed).
* **User space on musl:** the TI binaries need glibc >= 2.34 and libdrm. They run on Alpine through the glibc loader directly (`ld-linux-armhf.so.3 --library-path ...`) with the toolchain's glibc 2.39 and Debian's libdrm2, no chroot.
* **Result:** the module loads, powers the GPU and registers DRM device `pvr 1.17.4948957` (`/dev/dri/renderD128`); `pvrsrvctl --start --no-module` succeeds ("UM DDK-(4948957) and KM DDK-(4948957) match", "HW core rev (10105), SW core rev (10106) check skipped"), the SGX interrupt fires, and the debug dump shows the kernel command buffer consumed by the microkernel (write and read offsets both 1). Stop and unload are clean. The init script part 1 from Finding 60 is executed by TI's user space by itself.
* **GLES2 renders (same day):** TI's Mesa fork (`gitlab.freedesktop.org/StaticRocket/mesa`, branch `powervr/24.0.1`, the source of meta-ti's `mesa-pvr`) cross-builds for glibc armhf with only the `sgx` Gallium driver, EGL, GBM and GLES; `-Dgallium-sgx-alias=sun4i-drm` needs a one-line fix (the alias becomes a C identifier, so it must be underscorified). TI's `libsrv_um` only accepts a display DRM device named `omapdrm` (hard-coded); a tiny `LD_PRELOAD`-style shim that reports `sun4i-drm` as `omapdrm` from `drmGetVersion()` is enough, and Mesa then loads the same `sgx` driver under the `omapdrm` name. A GBM/EGL test with a surfaceless GLES2 context and an offscreen framebuffer reports `GL_RENDERER` "PowerVR SGX 544MP", `GL_VERSION` "OpenGL ES 2.0 build 1.17@4948957", and reads back the clear colour and a shader-drawn triangle correctly, with no GL error and no hang.
* **On screen and under load:** a small GBM/EGL/KMS test (window surface, `drmModeSetCrtc` then page flips on `sun4i-drm`, LVDS panel 1024x768 at 55 Hz, XRGB8888 scanout buffers allocated through the display device) runs vsync-locked at 54.9 fps. With 20 blended, nearly full-screen triangles per frame it is fill-rate bound at 27.4 fps (about 430 Mpixel/s); three minutes at that load gave 4940 frames at a constant rate, about 32,000 SGX interrupts, no GPU lockup or hardware-recovery message, and the SoC settled at about 67 °C. So far the rev 1.1.6 microkernel shows no problem on the rev 1.1.5 hardware.
* **Next:** boot integration and packaging (module, `pvrsrvctl`, the shim and a glibc runtime in the Alpine image), real clients (glmark2-es2, a Wayland compositor), clock-framework power code and a proper DT node, suspend/resume.
