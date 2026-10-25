# Technical Findings & Diagnostics Log — Exper Easypad 8 (T8E)

**Hardware:** Exper Easypad 8 (Model T8E / `fiber_exper`)  
**SoC:** Allwinner A31s (`sun6i`, 4× Cortex-A7 @ 1.2 GHz)  
**RAM:** 1 GB DDR3 @ 432 MHz (ZQ 251)  
**PMIC:** AXP221s via TWI1 (I2C1) @ 0x34 (P2WI unused on this board)  
**Storage Target:** External MicroSD (`mmc0`), internal NAND untouched  
**Display:** 1024×768 single-channel LVDS panel  
**Target:** Minimal Alpine Linux booted from SD card → Android Auto terminal

---

## Executive Summary

During the bring-up process, the tablet transitioned through multiple boot blockers:
```
BROM → boot0 → U-Boot (LCD/SPL) → Linux (Mainline 7.2) → Alpine Linux → OpenRC getty / sshd
                                                               ↳ usb0 (172.16.42.1) → SSH root@172.16.42.1 [OK]
```

A total of 32 technical findings were diagnosed, documented, and resolved.

---

## Findings 1 to 32

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
