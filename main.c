#include <stdio.h>
#include "pico/stdlib.h"
#include "hardware/gpio.h"

#define BUS_PIN_P 2
#define BUS_PIN_N 3
#define BIT_PERIOD_MS 500
#define STATUS_WORD 0x1800

static void bus_idle(void) {
    gpio_put(BUS_PIN_P, 0);
    gpio_put(BUS_PIN_N, 0);
}

static void set_bus_input(void) {
    gpio_set_dir(BUS_PIN_P, GPIO_IN);
    gpio_set_dir(BUS_PIN_N, GPIO_IN);
}

static void set_bus_output(void) {
    gpio_set_dir(BUS_PIN_P, GPIO_OUT);
    gpio_set_dir(BUS_PIN_N, GPIO_OUT);
}

static bool read_bit(bool *bit) {
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

static void send_bit(bool bit) {
    if (bit) {
        gpio_put(BUS_PIN_P, 1);
        gpio_put(BUS_PIN_N, 0);
    } else {
        gpio_put(BUS_PIN_P, 0);
        gpio_put(BUS_PIN_N, 1);
    }
}

static bool read_byte(uint8_t *value) {
    uint8_t byte = 0;

    for (int i = 0; i < 8; i++) {
        bool bit = false;
        if (!read_bit(&bit)) {
            return false;
        }

        byte = (uint8_t)((byte << 1) | (bit ? 1 : 0));
        sleep_ms(BIT_PERIOD_MS);
    }

    *value = byte;
    return true;
}

static bool read_word16(uint16_t *value) {
    uint16_t word = 0;

    for (int i = 0; i < 16; i++) {
        bool bit = false;
        if (!read_bit(&bit)) {
            return false;
        }

        word = (uint16_t)((word << 1) | (bit ? 1u : 0u));
        sleep_ms(BIT_PERIOD_MS);
    }

    *value = word;
    return true;
}

static void send_word(uint16_t word) {
    for (int i = 15; i >= 0; i--) {
        send_bit(((word >> i) & 1u) != 0u);
        sleep_ms(BIT_PERIOD_MS);
    }
}

int main(void) {
    stdio_init_all();
    sleep_ms(1200);

    gpio_init(BUS_PIN_P);
    gpio_init(BUS_PIN_N);
    set_bus_input();

    while (true) {
        while (true) {
            int p = gpio_get(BUS_PIN_P);
            int n = gpio_get(BUS_PIN_N);
            bool valid_start = (p == 1 && n == 0) || (p == 0 && n == 1);

            if (valid_start) {
                break;
            }

            sleep_ms(10);
        }

        sleep_ms(BIT_PERIOD_MS / 2);

        uint8_t sync = 0;
        if (!read_byte(&sync) || sync != 0xF0) {
            printf("SYNC_ERROR\n");
            continue;
        }

        printf("SYNC DETECTADO\n");

        uint16_t cmd = 0;
        uint16_t data = 0;

        if (!read_word16(&cmd) || !read_word16(&data)) {
            continue;
        }

        printf("CMD=0x%04X\n", cmd);
        printf("DATA=0x%04X\n", data);

        set_bus_output();
        bus_idle();
        sleep_ms(BIT_PERIOD_MS / 2);
        send_word(STATUS_WORD);
        printf("TX_STATUS=0x%04X\n", STATUS_WORD);
        bus_idle();
        set_bus_input();
    }
}
