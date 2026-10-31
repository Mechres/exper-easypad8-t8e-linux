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
6. `0006-clk-Also-require-core-dev-in-the-runtime-PM-get-put-.patch`:
   Makes `clk_pm_runtime_get()` and `clk_pm_runtime_put()` return early when `core->dev` is NULL. Defensive: it was added while chasing a hang in the i2c1 runtime-resume path before the AXP221 probed, and the exact trigger was not isolated.
7. `0007-of-platform-Bind-the-first-enabled-simple-framebuffe.patch`:
   Iterates the `/chosen` children and binds the first *available* `simple-framebuffer` instead of the first match regardless of `status`. Not independently reproduced on mainline.
8. `0008-drm-sun4i-tcon-Enable-LVDS-output-on-the-A31s-TCON.patch`:
   Adds `supports_lvds`, `has_lvds_alt` and the A33-style `setup_lvds_phy` to `sun6i_a31s_quirks`; without it `sun4i-drm` fails to bind TCON0 with `-22` as soon as a `panel-lvds` is attached.
9. `0009-ARM-dts-sun6i-exper-t8e-Match-the-running-board-conf.patch`:
   Brings the T8E board Device Tree in line with what boots on the tablet (AXP221 on P2WI with its NMI GPIO, Wi-Fi power sequence on PL5, codec routing, mmc aliases, no cpufreq OPPs).
10. `0010-ARM-dts-sun6i-exper-t8e-Add-a-DRM-sun4i-drm-variant-.patch`:
    Adds `sun6i-a31s-exper-t8e-drm.dts` (display engine, 1024×768 24-bit VESA LVDS panel, PWM backlight, PG12 panel supply, LVDS pins) and its Makefile target.
11. `0011-dt-bindings-media-allwinner-Add-A31-video-engine-and.patch`:
    Adds the `allwinner,sun6i-a31-video-engine`, `allwinner,sun6i-a31-system-control` and `allwinner,sun6i-a31-sram-c1` compatibles.
12. `0012-media-cedrus-Add-A31-support.patch`:
    Adds an A31 Cedrus variant with the A20 capabilities (MPEG-2, H.264, VP8) and a 320 MHz module clock request.
13. `0013-ARM-dts-sun6i-a31-Add-system-control-SRAM-C-and-vide.patch`:
    Adds the `system-control@1c00000` (SRAM C1 claimed by the video engine) and `video-codec@1c0e000` nodes to `sun6i-a31.dtsi`. The SRAM C window size is copied from the A23 and unverified.
14. `0014-ARM-dts-sun6i-exper-t8e-Enable-HDMI-in-the-DRM-varia.patch`:
    Adds an `hdmi-connector` and enables `&hdmi` in `sun6i-a31s-exper-t8e-drm.dts`; needs `CONFIG_DRM_SUN4I_HDMI` (set to `m` in `configs/exper_t8e_defconfig`).
15. `0015-ARM-dts-sun6i-exper-t8e-Keep-the-SDIO-Wi-Fi-powered-.patch`:
    Adds `keep-power-in-suspend` to the Wi-Fi `&mmc1` node so `brcmfmac` can suspend and resume (without it: `Failed to set pm_flags 1` on suspend and `HT Avail request error: -110` on resume).
16. `0016-clk-sunxi-ng-sun6i-a31-Let-the-CPU-PLL-settle-after-.patch`:
    Adds `CCU_FEATURE_LOCK_DELAY` (a fixed 2 ms wait before the PLL lock bit is polled) and uses it for the A31 PLL-CPU. Without it the first cpufreq transition hangs the tablet: the lock bit is already set right after the factors are written and the CPU mux is switched back to an unsettled PLL. 2 ms is empirical (100 us was not enough, the minimum in between was not found).
17. `0017-ARM-dts-sun6i-exper-t8e-Enable-cpufreq-with-the-stoc.patch`:
    Removes the `&cpu0` block that deleted `cpu-supply` and `operating-points`, so cpufreq-dt uses the 480/720/864/1008 MHz table (1.0 to 1.2 V on the AXP221 DCDC3). Needs patch 0016.
18. `0018-drm-sun4i-hdmi-Add-HDMI-audio-support.patch`:
    Adds HDMI audio to `sun4i-drm-hdmi`: an hdmi-codec from the DRM HDMI audio helper, an ASoC CPU DAI with a dmaengine PCM on the audio FIFO port (DRQ 14), a sound card (`sun4i-hdmi`), the audio infoframe and the packet arbiter setup. Needs `CONFIG_DRM_SUN4I_HDMI_AUDIO=y` (set in `configs/exper_t8e_defconfig`); the existing `dmas`/`dma-names` of `hdmi@1c16000` are used, no DTS change. 2 channels, S16_LE/S24_LE, 32/44.1/48 kHz.
19. `0019-ARM-dts-sun6i-exper-t8e-Give-the-cpu-nodes-a-capacit.patch`:
    Adds `capacity-dmips-mhz = <1024>` to the four cpu nodes in the board DTS. Cosmetic: it silences the four `missing clock-frequency property` boot messages from `arch/arm/kernel/topology.c`; all cores are identical Cortex-A7 so the capacities were equal anyway.
20. `0020-ARM-dts-sun6i-Add-the-SGX544-GPU-node-and-enable-it-.patch`:
    Adds the PowerVR SGX544 MP2 node (`allwinner,sun6i-a31-gpu`, 0x01c40000, SPI 97, core and memory clocks, as in the existing binding) to `sun6i-a31.dtsi`, disabled, and enables it on the T8E. The board keeps vdd-gpu (DCDC2) at 1.2 V always on, since the binding has no supply property. Mainline has no SGX driver: the node is for the out-of-tree TI DDK 1.17 port described in Finding 61 (`docs/FINDINGS.md`); without that module it only reserves the rail.

The DE1 display frontend (hardware scaler) is intentionally not part of the series: it produces no output on the A31s (see Finding 48 in `docs/FINDINGS.md`).
