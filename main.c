#include <stdio.h>
#include "pico/stdlib.h"
#include "hardware/gpio.h"

#define BUS_PIN_P 2
#define BUS_PIN_N 3

int main(void) {
    stdio_init_all();
    sleep_ms(1200);

    printf("SLAVE RAW BUS MONITOR\n");

    gpio_init(BUS_PIN_P);
    gpio_init(BUS_PIN_N);

    gpio_set_dir(BUS_PIN_P, GPIO_IN);
    gpio_set_dir(BUS_PIN_N, GPIO_IN);

    while (true) {
        int p = gpio_get(BUS_PIN_P) ? 1 : 0;
        int n = gpio_get(BUS_PIN_N) ? 1 : 0;

        printf("RX_RAW|P=%d|N=%d\n", p, n);
        sleep_ms(100);
    }
}