#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
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

int main(int argc, char **argv) {
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

    printf("=== Activating Hardware LCD Power, Backlight PWM & EN ===\n");
    
    /* 1. PG12 = Output HIGH (LCD Panel Power) */
    set_pin_cfg(pio, PORT_G, 12, 1);
    set_pin_val(pio, PORT_G, 12, 1);
    printf("1. PG12 (LCD_POWER) -> HIGH\n");

    /* 2. Configure PWM0 at 0x01c21400 */
    /* Prescale = 0xf (divide by 120 or 240), Enable = 1, Polarity = 1 */
    pwm[1] = 0x04af03c0; /* 80% duty cycle */
    pwm[0] = (1 << 4) | (1 << 5) | (1 << 6) | 0xf;
    printf("2. PWM0 configured (CTRL: 0x%08x, PERIOD: 0x%08x)\n", pwm[0], pwm[1]);

    /* PH13 function = 2 (PWM0) */
    set_pin_cfg(pio, PORT_H, 13, 2);
    printf("3. PH13 muxed to PWM0 (fn 2)\n");

    /* 3. PA25 = Output HIGH (Backlight Enable) */
    set_pin_cfg(pio, PORT_A, 25, 1);
    set_pin_val(pio, PORT_A, 25, 1);
    printf("4. PA25 (BL_EN) -> HIGH\n");

    printf("\nAll display power rails and backlight signals are ACTIVE!\n");

    munmap(map, MMAP_SIZE);
    close(fd);
    return 0;
}
