#include <stdio.h>
#include "pico/stdlib.h"
#include "hardware/gpio.h"

#define BUS_PIN_P 2
#define BUS_PIN_N 3

int main(void) {
    stdio_init_all();
    sleep_ms(1200);

    gpio_init(BUS_PIN_P);
    gpio_init(BUS_PIN_N);
    gpio_set_dir(BUS_PIN_P, GPIO_IN);
    gpio_set_dir(BUS_PIN_N, GPIO_IN);

    printf("SLAVE BIT TEST\n");

    while (true) {
        int p = gpio_get(BUS_PIN_P);
        int n = gpio_get(BUS_PIN_N);

        int bit = (p == 1 && n == 0) ? 1 :
                  (p == 0 && n == 1) ? 0 : -1;

        if (bit != -1) {
            printf("RX_BIT=%d\n", bit);
        } else {
            printf("RX_INVALID\n");
        }

        sleep_us(100);
    }
}