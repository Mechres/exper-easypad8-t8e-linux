#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <unistd.h>

#define PAGE_BASE 0x01c20000
#define PIO_OFFSET 0x800
#define PWM_OFFSET 0x1400
#define MMAP_SIZE 0x3000

#define PORT_A 0x00
#define PORT_G 0xd8
#define PORT_H 0xfc

static void set_pin_cfg(volatile uint32_t *pio, uint32_t bank_offset, int pin, int fn) {
    int cfg_reg_idx = (bank_offset + (pin / 8) * 4) / 4;
    int cfg_shift = (pin % 8) * 4;
    uint32_t cfg = pio[cfg_reg_idx];
    cfg &= ~(0x7 << cfg_shift);
    cfg |= ((fn & 0x7) << cfg_shift);
    pio[cfg_reg_idx] = cfg;
}

static void set_pin_val(volatile uint32_t *pio, uint32_t bank_offset, int pin, int val) {
    int data_reg_idx = (bank_offset + 0x10) / 4;
    uint32_t data = pio[data_reg_idx];
    if (val)
        data |= (1 << pin);
    else
        data &= ~(1 << pin);
    pio[data_reg_idx] = data;
}

static void show_usage(const char *prog) {
    printf("Usage: %s [on|off|<0-100>]\n", prog);
    printf("  on       Turn on panel power (PG12) and backlight (PA25, PWM0 @ 80%%)\n");
    printf("  off      Turn off backlight (PA25, PWM0) and panel power (PG12)\n");
    printf("  <0-100>  Set backlight brightness percentage\n");
}

int main(int argc, char **argv) {
    int level = 80; /* default 80% */
    int power_on = 1;

    if (argc > 1) {
        if (strcmp(argv[1], "off") == 0) {
            power_on = 0;
            level = 0;
        } else if (strcmp(argv[1], "on") == 0) {
            power_on = 1;
            level = 80;
        } else if (strcmp(argv[1], "--help") == 0 || strcmp(argv[1], "-h") == 0) {
            show_usage(argv[0]);
            return 0;
        } else {
            level = atoi(argv[1]);
            if (level < 0) level = 0;
            if (level > 100) level = 100;
            if (level == 0) power_on = 0;
            else power_on = 1;
        }
    }

    int fd = open("/dev/mem", O_RDWR | O_SYNC);
    if (fd < 0) {
        perror("open /dev/mem");
        return 1;
    }

    void *map = mmap(NULL, MMAP_SIZE, PROT_READ | PROT_WRITE, MAP_SHARED, fd, PAGE_BASE);
    if (map == MAP_FAILED) {
        perror("mmap");
        close(fd);
        return 1;
    }

    volatile uint32_t *pio = (volatile uint32_t *)((char *)map + PIO_OFFSET);
    volatile uint32_t *pwm = (volatile uint32_t *)((char *)map + PWM_OFFSET);

    if (power_on) {
        /* 1. PG12 = Output HIGH (LCD Panel Power) */
        set_pin_cfg(pio, PORT_G, 12, 1);
        set_pin_val(pio, PORT_G, 12, 1);

        /* 2. Configure PWM0 at 0x01c21400 */
        /* Period = 0x04af (1199 -> 50 kHz), Duty = (1199 * level) / 100 */
        uint32_t total_period = 0x04af;
        uint32_t active_cycles = (total_period * level) / 100;
        pwm[1] = (total_period << 16) | (active_cycles & 0xffff);
        pwm[0] = (1 << 4) | (1 << 5) | (1 << 6) | 0xf; /* enable + active high + prescaler */

        /* 3. PH13 muxed to PWM0 (fn 2) */
        set_pin_cfg(pio, PORT_H, 13, 2);

        /* 4. PA25 = Output HIGH (Backlight Enable) */
        set_pin_cfg(pio, PORT_A, 25, 1);
        set_pin_val(pio, PORT_A, 25, 1);

        printf("Display & Backlight ON: brightness %d%%\n", level);
    } else {
        /* Backlight Disable (PA25 -> LOW) */
        set_pin_cfg(pio, PORT_A, 25, 1);
        set_pin_val(pio, PORT_A, 25, 0);

        /* Disable PWM0 */
        pwm[0] = 0;
        pwm[1] = 0;
        set_pin_cfg(pio, PORT_H, 13, 0); /* Input */

        /* LCD Panel Power (PG12 -> LOW) */
        set_pin_cfg(pio, PORT_G, 12, 1);
        set_pin_val(pio, PORT_G, 12, 0);

        printf("Display & Backlight OFF (Low-Power Panel Standby)\n");
    }

    munmap(map, MMAP_SIZE);
    close(fd);
    return 0;
}
