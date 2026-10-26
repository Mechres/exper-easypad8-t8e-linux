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
* **Implementation:**
  * Enhanced [`exper-backlight`](../scripts/exper-backlight.c) with brightness scaling ($0-100\%$) and clean low-power standby mode (`off` / `on`), shutting off PWM, `PA25` backlight enable, and `PG12` panel electronics.
  * Developed [`exper-powerd`](../scripts/exper-powerd.c) background daemon listening on `/dev/input/by-path/platform-1f03400.i2c-platform-axp221-pek-event`.
  * Single-press of the physical power button toggles the LCD panel and backlight off / on instantly, providing real tablet power-key behavior and maximizing battery endurance.

### Finding 46: Allwinner A31s 4-Core SMP Multi-Core Activation
* **Root Cause of Single-Core State:** During initial Linux 7.2 board bring-up, `nosmp` was set in `bootargs` and hardcoded into `CONFIG_CMDLINE` with `CONFIG_CMDLINE_FORCE=y` to prevent early `cpufreq-dt` binding panics on boards lacking an OPP table.
* **Resolution:**
  1. Updated `dts/sun6i-a31s-exper-t8e-kernel.dts` to delete `operating-points` and `cpu-supply` under `&cpu0`. This cleanly causes `cpufreq-dt` to report `cpu cpu0: OPP table can't be empty` and exit safely (return code 19) without blocking SMP initialization.
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
* **Change:** The bring-up kernel (probes in `head.S`, `setup.c`, `8250_*`, `i2c-mv64xxx`, `printk`, `dd.c`, `brk_raw.h`) was replaced by the 10-patch series in `patches/kernel/` and `configs/exper_t8e_defconfig`: `CONFIG_DEBUG_LL`/`DEBUG_UNCOMPRESS` off, log buffer 128 KB, and `debug initcall_debug ignore_loglevel loglevel=8` replaced by `loglevel=4`.
* **Gotcha:** With `CONFIG_CMDLINE_EXTEND` the built-in command line is appended **after** the bootloader's, so it can not be overridden from `boot.scr`; the debug flags had to be removed from `CONFIG_CMDLINE` itself.
* **Measured on the tablet:** `Run /sbin/init` at 1.76 s and `sun4i-drm` initialised at 4.5 s (the debug kernel took 51.3 s to reach the same point); `dmesg` shrank from thousands of lines to about 300 and the bring-up `Call trace:` dumps are gone. The speed-up was not bisected between `initcall_debug`, `ignore_loglevel` and the probe prints, which all went away together.
* **Build:** `scripts/30-build-kernel.sh` builds out of tree from the defconfig and installs a new modules directory; keep the previous release directory on the card while testing a new kernel, so the old kernel can still be booted by pointing `boot.scr` back at it.

### Finding 50: Userspace `exper-backlight` Must Not Run Next to the Kernel Backlight
* **Mechanism:** `exper-backlight` programs the PIO and PWM registers directly through `/dev/mem`. With the DRM DTB the kernel already owns the PG12 supply (`regulator-fixed`), the PH13 PWM (`pwm-sun4i`) and the PA25 enable (`pwm-backlight`), so touching those registers behind its back can corrupt the PWM state.
* **Fix:** `rootfs/alpine/etc/local.d/01-backlight.start` now runs the helper only when `/sys/class/backlight/backlight` does not exist (SimpleFB boot). `scripts/exper-powerd.c` writes `bl_power` (0 = on, 4 = off) on the backlight class when it is available and falls back to `exper-backlight` otherwise; `EXPER_POWERD_INPUT` and `EXPER_POWERD_BACKLIGHT_DIR` override the paths for testing.
