#include <stdio.h>

#include "pico/stdlib.h"
#include "hardware/pio.h"

#include "manchester_rx.pio.h"

#define RX_PIN 2u
#define RX_PIO pio0
#define RX_SM  0u

static void manchester_rx_init(PIO pio, uint sm, uint pin) {
    uint offset = pio_add_program(pio, &manchester_rx_program);
    pio_sm_config c = manchester_rx_program_get_default_config(offset);

    sm_config_set_in_pins(&c, pin);
    sm_config_set_jmp_pin(&c, pin);
    sm_config_set_in_shift(&c, false, false, 32);
    sm_config_set_fifo_join(&c, PIO_FIFO_JOIN_RX);

    pio_gpio_init(pio, pin);
    pio_sm_set_consecutive_pindirs(pio, sm, pin, 1, false);

    pio_sm_init(pio, sm, offset, &c);
    pio_sm_set_enabled(pio, sm, true);
}

int main(void) {
    stdio_init_all();
    sleep_ms(1200);

    manchester_rx_init(RX_PIO, RX_SM, RX_PIN);

    while (true) {
        for (int i = 0; i < 3; i++) {
            const uint32_t raw = pio_sm_get_blocking(RX_PIO, RX_SM);
            const uint8_t byte = (uint8_t)(raw & 0xFFu);
            const uint8_t fixed = ~byte;

            printf("RX_PIO_BYTE[%d]=0x%02X\n", i, fixed);
        }
    }
}
