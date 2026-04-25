#include "bus.h"

#include "hardware/gpio.h"
#include "pico/stdlib.h"

static int bus_read_diff_level(void) {
    const int p = gpio_get(BUS_PIN_P);
    const int n = gpio_get(BUS_PIN_N);

    if (p == 1 && n == 0) {
        return 1; // HIGH
    }

    if (p == 0 && n == 1) {
        return 0; // LOW
    }

    return -1; // invalid or idle
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
    const uint32_t half_period_ms = BIT_PERIOD_MS / 2u;

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

bool bus_read_bit(bool *bit) {
    int first_half = 0;
    int second_half = 0;

    if (bit == NULL) {
        return false;
    }

    first_half = bus_read_diff_level();
    if (first_half < 0) {
        return false;
    }

    sleep_ms(BIT_PERIOD_MS / 2u);

    second_half = bus_read_diff_level();
    if (second_half < 0) {
        return false;
    }

    if (first_half == 1 && second_half == 0) {
        *bit = true;
    } else if (first_half == 0 && second_half == 1) {
        *bit = false;
    } else {
        return false;
    }

    sleep_ms(BIT_PERIOD_MS / 2u);
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

uint8_t bus_compute_odd_parity(uint16_t word) {
    uint8_t parity = 0u;

    for (int i = 0; i < 16; i++) {
        parity ^= (uint8_t)((word >> i) & 1u);
    }

    // Si la palabra tiene paridad par (parity=0), el bit de paridad debe ser 1.
    // Si la palabra tiene paridad impar (parity=1), el bit de paridad debe ser 0.
    return (uint8_t)(parity ^ 1u);
}

void bus_send_word16_parity(uint16_t word) {
    bus_send_word16(word);
    bus_send_bit(bus_compute_odd_parity(word) != 0u);
}

bool bus_read_word16_parity(uint16_t *word) {
    uint16_t value = 0;
    bool parity_bit = false;

    if (word == NULL) {
        return false;
    }

    if (!bus_read_word16(&value)) {
        return false;
    }

    if (!bus_read_bit(&parity_bit)) {
        return false;
    }

    if ((parity_bit ? 1u : 0u) != bus_compute_odd_parity(value)) {
        return false;
    }

    *word = value;
    return true;
}
