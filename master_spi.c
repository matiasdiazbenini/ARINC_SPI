#include <stdio.h>
#include "pico/stdlib.h"
#include "hardware/gpio.h"

#define BUS_PIN_P 2
#define BUS_PIN_N 3
#define BIT_PERIOD_MS 500

void bus_idle(void) {
    gpio_put(BUS_PIN_P, 0);
    gpio_put(BUS_PIN_N, 0);
}

void send_bit(bool bit) {
    if (bit) {
        gpio_put(BUS_PIN_P, 1);
        gpio_put(BUS_PIN_N, 0);
    } else {
        gpio_put(BUS_PIN_P, 0);
        gpio_put(BUS_PIN_N, 1);
    }
}

void send_byte(uint8_t byte) {
    for (int i = 7; i >= 0; i--) {
        send_bit((byte >> i) & 1);
        sleep_ms(BIT_PERIOD_MS);
    }
}

void send_word(uint16_t word) {
    for (int i = 15; i >= 0; i--) {
        send_bit((word >> i) & 1);
        sleep_ms(BIT_PERIOD_MS);
    }
}

int main(void) {
    stdio_init_all();
    sleep_ms(1200);

    gpio_init(BUS_PIN_P);
    gpio_init(BUS_PIN_N);
    gpio_set_dir(BUS_PIN_P, GPIO_OUT);
    gpio_set_dir(BUS_PIN_N, GPIO_OUT);

    while (true) {
        bus_idle();
        sleep_ms(1000);

        printf("---- FRAME ----\n");

        send_byte(0xF0);
        send_word(0x1821);
        send_word(0xA5A5);

        printf("CMD=0x1821\n");
        printf("DATA=0xA5A5\n");

        bus_idle();

        sleep_ms(2000);
    }
}
