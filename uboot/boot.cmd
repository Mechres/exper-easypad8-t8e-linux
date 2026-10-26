echo ">>> Exper Easypad 8 LowMem Bootloader <<<"
setenv fdt_high 0x50000000
setenv initrd_high 0x50000000
setenv bootm_size 0x10000000
setenv bootm_boot_mode nonsec
setenv bootargs console=tty0 console=ttyS0,115200 earlycon=uart8250,mmio32,0x01c28000 root=/dev/mmcblk0p2 rootwait rw panic=0 fbcon=map:0 loglevel=8 ignore_loglevel consoleblank=0 debug initcall_debug
echo ">>> DTB yukleniyor..."
load mmc 0:1 0x43000000 sun6i-a31s-exper-t8e.dtb
fdt addr 0x43000000
fdt resize 65536
echo ">>> zImage yukleniyor..."
load mmc 0:1 0x42000000 zImage
echo ">>> Framebuffer ve Chosen durumu:"
fdt print /chosen
echo ">>> Linux baslatiliyor..."
bootz 0x42000000 - 0x43000000
