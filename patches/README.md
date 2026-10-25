# Patches for Exper Easypad 8 (T8E)

This directory contains standalone patch series for mainline U-Boot and Linux kernel trees.

---

## 1. U-Boot Patches (`patches/uboot/`)

Applied against mainline U-Boot (`v2024.x` / `master`):
- `0001-sunxi-Add-support-for-Exper-Easypad-T8E-tablet-A31s.patch`:
  - Adds board defconfig `configs/exper_t8e_defconfig`
  - Adds U-Boot device tree `arch/arm/dts/sun6i-a31s-exper-t8e.dts`
  - Enables PSCI, Non-Secure boot, SimpleFB video console, and MMC SPL

---

## 2. Kernel Patches (`patches/kernel/`)

Applied against mainline Linux (6.12+ / 7.x):
1. `0001-ARM-timex-Fix-inverted-get_cycles-return-value.patch`:
   Fixes inverted return condition in `get_cycles()` macro.
2. `0002-ARM-dts-sun6i-Drop-cpu-registers-not-fw-configured-f.patch`:
   Removes `arm,cpu-registers-not-fw-configured` quirk from arch timer node to use virtual timer PPI in Non-Secure mode.
3. `0003-mmc-sunxi-mmc-Fallback-ocr_avail-when-no-vmmc-regula.patch`:
   Sets default 3.2V-3.4V `ocr_avail` fallback when an SD card slot lacks a software-controllable `vmmc-supply` regulator.
4. `0004-serial-8250_dw-Work-around-Allwinner-A31s-APB-bus-lo.patch`:
   Bypasses `DW_UART_USR` BUSY polling and RBR flush during idle entry that freeze the A31s APB bus; bounds `frame_time`; forbids runtime PM clock gating during active console.
5. `0005-ARM-dts-sun6i-Add-Device-Tree-for-Exper-Easypad-T8E-.patch`:
   Adds board Device Tree `arch/arm/boot/dts/allwinner/sun6i-a31s-exper-t8e.dts` and Makefile target.
