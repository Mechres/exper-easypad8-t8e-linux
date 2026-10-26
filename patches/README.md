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
6. `0006-clk-Avoid-NULL-pointer-dereference-in-clk_pm_runtime.patch`:
   Guards against NULL `core->dev` in `clk_pm_runtime_get()` and `clk_pm_runtime_put()` to avoid crash on clock providers lacking associated struct device (e.g. Allwinner CCU).
7. `0007-of-platform-Populate-first-available-simple-framebu.patch`:
   Iterates over `/chosen` nodes to bind the first available `simple-framebuffer` instead of stopping at disabled template nodes.
8. `0008-drm-sun4i-tcon-Enable-LVDS-output-on-the-A31s-TCON.patch`:
   Adds `supports_lvds`, `has_lvds_alt` and the A33-style `setup_lvds_phy` to `sun6i_a31s_quirks`; without it `sun4i-drm` fails to bind TCON0 with `-22` as soon as a `panel-lvds` is attached.
9. `0009-ARM-dts-sun6i-exper-t8e-Match-the-running-board-conf.patch`:
   Brings the T8E board Device Tree in line with what boots on the tablet (AXP221 on P2WI with its NMI GPIO, Wi-Fi power sequence on PL5, codec routing, mmc aliases, no cpufreq OPPs).
10. `0010-ARM-dts-sun6i-exper-t8e-Add-a-DRM-sun4i-drm-variant-.patch`:
    Adds `sun6i-a31s-exper-t8e-drm.dts` (display engine, 1024×768 24-bit VESA LVDS panel, PWM backlight, PG12 panel supply, LVDS pins) and its Makefile target.

The DE1 display frontend (hardware scaler) is intentionally not part of the series: it produces no output on the A31s (see Finding 48 in `docs/FINDINGS.md`).
