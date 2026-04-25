#include "bus.h"

#include "hardware/gpio.h"
#include "pico/stdlib.h"

static int bus_read_level(void) {
    const int p = gpio_get(BUS_PIN_P);
    const int n = gpio_get(BUS_PIN_N);

    if (p == 1 && n == 0) {
        return 1; // HIGH
    }
    if (p == 0 && n == 1) {
        return 0; // LOW
    }

    return -1; // invalid/idle
}

void bus_init(void) {
    gpio_init(BUS_PIN_P);
    gpio_init(BUS_PIN_N);
    bus_set_rx_mode();
}

void bus_set_tx_mode(void) {
    gpio_set_dir(BUS_PIN_P, GPIO_OUT);
    gpio_set_dir(BUS_PIN_N, GPIO_OUT);
}

void bus_set_rx_mode(void) {
    gpio_set_dir(BUS_PIN_P, GPIO_IN);
    gpio_set_dir(BUS_PIN_N, GPIO_IN);
}

void bus_idle(void) {
    gpio_put(BUS_PIN_P, 0);
    gpio_put(BUS_PIN_N, 0);
}

void bus_send_bit(bool bit) {
    const uint32_t half = BIT_PERIOD_MS / 2u;

    if (bit) {
        // Manchester: 1 = HIGH -> LOW
        gpio_put(BUS_PIN_P, 1);
        gpio_put(BUS_PIN_N, 0);
        sleep_ms(half);

        gpio_put(BUS_PIN_P, 0);
        gpio_put(BUS_PIN_N, 1);
        sleep_ms(half);
    } else {
        // Manchester: 0 = LOW -> HIGH
        gpio_put(BUS_PIN_P, 0);
        gpio_put(BUS_PIN_N, 1);
        sleep_ms(half);

        gpio_put(BUS_PIN_P, 1);
        gpio_put(BUS_PIN_N, 0);
        sleep_ms(half);
    }
}

bool bus_read_bit(bool *bit) {
    const uint32_t half = BIT_PERIOD_MS / 2u;
    int first = 0;
    int second = 0;

    if (bit == NULL) {
        return false;
    }

    first = bus_read_level();
    if (first < 0) {
        return false;
    }

    sleep_ms(half);

    second = bus_read_level();
    if (second < 0) {
        return false;
    }

    if (first == 1 && second == 0) {
        *bit = true;
    } else if (first == 0 && second == 1) {
        *bit = false;
    } else {
        return false;
    }

    sleep_ms(half);
    return true;
}

void bus_send_byte(uint8_t byte) {
    for (int i = 7; i >= 0; i--) {
        bus_send_bit(((byte >> i) & 1u) != 0u);
    }
}

bool bus_read_byte(uint8_t *byte) {
    uint8_t value = 0;

    if (byte == NULL) {
        return false;
    }

    for (int i = 0; i < 8; i++) {
        bool bit = false;

        if (!bus_read_bit(&bit)) {
            return false;
        }

        value = (uint8_t)((value << 1) | (bit ? 1u : 0u));
    }

    *byte = value;
    return true;
}

void bus_send_word16(uint16_t word) {
    for (int i = 15; i >= 0; i--) {
        bus_send_bit(((word >> i) & 1u) != 0u);
    }
}

bool bus_read_word16(uint16_t *word) {
    uint16_t value = 0;

    if (word == NULL) {
        return false;
    }

    for (int i = 0; i < 16; i++) {
        bool bit = false;

        if (!bus_read_bit(&bit)) {
            return false;
        }

        value = (uint16_t)((value << 1) | (bit ? 1u : 0u));
    }

    *word = value;
    return true;
}
