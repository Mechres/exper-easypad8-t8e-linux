#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <unistd.h>
#include <fcntl.h>
#include <linux/input.h>
#include <signal.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <string.h>

#define PEK_DEV "/dev/input/by-path/platform-1f03400.i2c-platform-axp221-pek-event"
#define BL_DIR "/sys/class/backlight/backlight"

static volatile int running = 1;

/*
 * Screen on/off.
 *  - DRM boot (sun6i-a31s-exper-t8e-drm.dtb): the kernel owns panel power (PG12 regulator) and the
 *    PWM backlight (pwm-backlight), so use the backlight class: bl_power 0 = on, 4 = off.
 *  - SimpleFB boot: fall back to the register-poking exper-backlight helper.
 * EXPER_POWERD_INPUT / EXPER_POWERD_BACKLIGHT_DIR override the paths (for testing).
 */
static void set_screen(int on) {
    const char *dir = getenv("EXPER_POWERD_BACKLIGHT_DIR");
    char path[256];
    snprintf(path, sizeof(path), "%s/bl_power", dir ? dir : BL_DIR);
    if (access(path, W_OK) == 0) {
        int fd = open(path, O_WRONLY);
        if (fd >= 0) {
            if (write(fd, on ? "0" : "4", 1) < 0) perror("bl_power");
            close(fd);
            return;
        }
    }
    system(on ? "exper-backlight 80 > /dev/null 2>&1" : "exper-backlight off > /dev/null 2>&1");
}

static void handle_sig(int sig) {
    running = 0;
}

int main(void) {
    signal(SIGTERM, handle_sig);
    signal(SIGINT, handle_sig);

    const char *dev = getenv("EXPER_POWERD_INPUT");
    int fd = open(dev ? dev : PEK_DEV, O_RDONLY);
    if (fd < 0) {
        perror("open axp221-pek");
        return 1;
    }

    int screen_on = 1;
    struct input_event ev;

    while (running) {
        ssize_t n = read(fd, &ev, sizeof(ev));
        if (n < (ssize_t)sizeof(ev)) {
            usleep(100000);
            continue;
        }

        /* Check for KEY_POWER press (type 1, code 116, value 1) */
        if (ev.type == EV_KEY && ev.code == KEY_POWER && ev.value == 1) {
            screen_on = !screen_on;
            set_screen(screen_on);
        }
    }

    close(fd);
    return 0;
}
