#include <stdio.h>

#include "pico/stdlib.h"
#include "hardware/pio.h"
#include "bus/bus.h"

#include "manchester_rx.pio.h"

#define RX_PIN_BASE 2u
#define RX_PIO pio0
#define RX_SM  0u

static void rx_diff_init(PIO pio, uint sm, uint pin_base) {
    uint offset = pio_add_program(pio, &manchester_rx_program);
    pio_sm_config c = manchester_rx_program_get_default_config(offset);

    sm_config_set_in_pins(&c, pin_base);
    sm_config_set_in_shift(&c, false, false, 32);
    sm_config_set_fifo_join(&c, PIO_FIFO_JOIN_RX);

    pio_gpio_init(pio, pin_base);
    pio_gpio_init(pio, pin_base + 1u);

    pio_sm_set_consecutive_pindirs(pio, sm, pin_base, 2, false);

    pio_sm_init(pio, sm, offset, &c);
    pio_sm_set_enabled(pio, sm, true);
}

static uint8_t read_pn_gpio(void) {
    int p = gpio_get(2);
    int n = gpio_get(3);

    if (p == 1 && n == 0) return 0x01u;
    if (p == 0 && n == 1) return 0x02u;
    if (p == 0 && n == 0) return 0x00u;
    return 0x03u;
}

static bool read_manchester_bit(bool *bit) {
    uint8_t prev = read_pn_gpio();
    uint8_t curr = prev;

    // Esperar transición válida
    while (true) {
        curr = read_pn_gpio();

        if (curr != prev && curr != 0x00u) {
            break;
        }

        prev = curr;
    }

    // DECODIFICACIÓN DIRECTA
    if (prev == 0x01u && curr == 0x02u) {
        *bit = true;   // HIGH → LOW
        return true;
    }

    if (prev == 0x02u && curr == 0x01u) {
        *bit = false;  // LOW → HIGH
        return true;
    }

    printf("BIT_ERROR|PREV=0x%02X|CURR=0x%02X\n", prev, curr);
    return false;
}

int main(void) {
    stdio_init_all();
    sleep_ms(1200);

    rx_diff_init(RX_PIO, RX_SM, RX_PIN_BASE);

    while (true) {
        bool bit = false;

        if (read_manchester_bit(&bit)) {
            printf("RX_BIT=%u\n", bit ? 1u : 0u);
        } else {
            sleep_ms(100);
        }
    }
}