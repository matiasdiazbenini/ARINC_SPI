#include <stdio.h>
#include "pico/stdlib.h"
#include "hardware/gpio.h"

#define BUS_PIN_P 2
#define BUS_PIN_N 3

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
        sleep_ms(500);
    }
}

int main(void) {
    stdio_init_all();
    sleep_ms(1200);

    gpio_init(BUS_PIN_P);
    gpio_init(BUS_PIN_N);
    gpio_set_dir(BUS_PIN_P, GPIO_OUT);
    gpio_set_dir(BUS_PIN_N, GPIO_OUT);

    printf("MASTER SYNC TEST\n");

    while (true) {

        bus_idle();
        sleep_ms(1000);

        printf("---- FRAME ----\n");

        send_byte(0b11110000);  // SYNC
        send_byte(0b10101010);  // DATA
        bus_idle();

        sleep_ms(2000);
    }
}