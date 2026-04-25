#include <stdio.h>
#include "pico/stdlib.h"
#include "hardware/gpio.h"

#define BUS_PIN_P 2
#define BUS_PIN_N 3
#define BIT_PERIOD_MS 500

void set_bus_output(void) {
    gpio_set_dir(BUS_PIN_P, GPIO_OUT);
    gpio_set_dir(BUS_PIN_N, GPIO_OUT);
}

void set_bus_input(void) {
    gpio_set_dir(BUS_PIN_P, GPIO_IN);
    gpio_set_dir(BUS_PIN_N, GPIO_IN);
}

void bus_idle(void) {
    gpio_put(BUS_PIN_P, 0);
    gpio_put(BUS_PIN_N, 0);
}

void send_bit(bool bit) {
    const uint32_t half_period_ms = BIT_PERIOD_MS / 2;

    if (bit) {
        // bit 1: HIGH -> LOW
        gpio_put(BUS_PIN_P, 1);
        gpio_put(BUS_PIN_N, 0);
        sleep_ms(half_period_ms);

        gpio_put(BUS_PIN_P, 0);
        gpio_put(BUS_PIN_N, 1);
        sleep_ms(half_period_ms);
    } else {
        // bit 0: LOW -> HIGH
        gpio_put(BUS_PIN_P, 0);
        gpio_put(BUS_PIN_N, 1);
        sleep_ms(half_period_ms);

        gpio_put(BUS_PIN_P, 1);
        gpio_put(BUS_PIN_N, 0);
        sleep_ms(half_period_ms);
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

bool read_bit(bool *bit) {
    int p = gpio_get(BUS_PIN_P);
    int n = gpio_get(BUS_PIN_N);

    if (p == 1 && n == 0) {
        *bit = true;
        return true;
    }

    if (p == 0 && n == 1) {
        *bit = false;
        return true;
    }

    return false;
}

bool read_word(uint16_t *word) {
    uint16_t value = 0;

    for (int i = 0; i < 16; i++) {
        bool bit = false;
        if (!read_bit(&bit)) {
            return false;
        }

        value = (uint16_t)((value << 1) | (bit ? 1u : 0u));
        sleep_ms(BIT_PERIOD_MS);
    }

    *word = value;
    return true;
}

int main(void) {
    stdio_init_all();
    sleep_ms(1200);

    gpio_init(BUS_PIN_P);
    gpio_init(BUS_PIN_N);
    set_bus_output();

    while (true) {
        bus_idle();
        sleep_ms(1000);

        send_byte(0xF0);
        send_word(0x1821);
        send_word(0xA5A5);

        set_bus_input();
        sleep_ms(BIT_PERIOD_MS / 2);
        uint16_t status = 0;
        if (read_word(&status)) {
            printf("RX_STATUS=0x%04X\n", status);
        }

        set_bus_output();
        bus_idle();

        sleep_ms(2000);
    }
}
