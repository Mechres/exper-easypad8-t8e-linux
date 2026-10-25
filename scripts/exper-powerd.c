#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <unistd.h>
#include <fcntl.h>
#include <linux/input.h>
#include <signal.h>
#include <sys/types.h>
#include <sys/stat.h>

#define PEK_DEV "/dev/input/by-path/platform-1f03400.i2c-platform-axp221-pek-event"

static volatile int running = 1;

static void handle_sig(int sig) {
    running = 0;
}

int main(void) {
    signal(SIGTERM, handle_sig);
    signal(SIGINT, handle_sig);

    int fd = open(PEK_DEV, O_RDONLY);
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
            if (screen_on) {
                system("exper-backlight 80 > /dev/null 2>&1");
            } else {
                system("exper-backlight off > /dev/null 2>&1");
            }
        }
    }

    close(fd);
    return 0;
}
