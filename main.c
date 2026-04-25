#include <stdio.h>

#include "hardware/gpio.h"
#include "pico/stdlib.h"

#include "bus/bus.h"

static bool has_valid_start(void) {
    const int p = gpio_get(BUS_PIN_P);
    const int n = gpio_get(BUS_PIN_N);
    return (p == 1 && n == 0) || (p == 0 && n == 1);
}

int main(void) {
    stdio_init_all();
    sleep_ms(1200);

    bus_init();
    bus_set_rx_mode();

    while (true) {
        while (!has_valid_start()) {
            sleep_ms(10);
        }

        sleep_ms(BIT_PERIOD_MS / 4u);

        uint8_t sync = 0;
        if (!bus_read_byte(&sync) || sync != 0xF0u) {
            printf("SYNC_ERROR|SYNC=0x%02X\n", sync);
            continue;
        }

        printf("SYNC DETECTADO\n");

        for (uint8_t i = 0; i < 8u; ++i) {
            uint16_t word = 0;
            if (!bus_read_word16_parity(&word)) {
                printf("RX_WORD_ERROR[%u]\n", (unsigned)i);
                break;
            }

            printf("RX_WORD[%u]=0x%04X\n", (unsigned)i, word);
        }
    }
}
