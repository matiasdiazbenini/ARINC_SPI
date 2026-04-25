#include <stdio.h>
#include "pico/stdlib.h"
#include "hardware/gpio.h"

#define BUS_PIN_P 2
#define BUS_PIN_N 3

void send_bit(bool bit) {
    if (bit) {
        gpio_put(BUS_PIN_P, 1);
        gpio_put(BUS_PIN_N, 0);
    } else {
        gpio_put(BUS_PIN_P, 0);
        gpio_put(BUS_PIN_N, 1);
    }
}

int main(void) {
    stdio_init_all();
    sleep_ms(1200);

    gpio_init(BUS_PIN_P);
    gpio_init(BUS_PIN_N);
    gpio_set_dir(BUS_PIN_P, GPIO_OUT);
    gpio_set_dir(BUS_PIN_N, GPIO_OUT);

    printf("MASTER BIT TEST\n");

    while (true) {
        uint8_t pattern = 0b10101010;

        for (int i = 7; i >= 0; i--) {
            bool bit = (pattern >> i) & 1;
            send_bit(bit);

            printf("TX_BIT=%d\n", bit);
            sleep_us(1000);  // tiempo de bit
        }

        sleep_ms(1000);
    }
}