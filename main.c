#include <stdio.h>

#include "hardware/gpio.h"
#include "pico/stdlib.h"

#include "bus/bus.h"

#define STATUS_WORD 0x1800u

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

        uint16_t cmd = 0;
        uint16_t data = 0;
        if (!bus_read_word16(&cmd) || !bus_read_word16(&data)) {
            continue;
        }

        printf("CMD=0x%04X\n", cmd);
        printf("DATA=0x%04X\n", data);

        bus_set_tx_mode();
        bus_idle();
        sleep_ms(BIT_PERIOD_MS / 2u);
        bus_send_word16(STATUS_WORD);
        printf("TX_STATUS=0x%04X\n", STATUS_WORD);
        bus_idle();
        sleep_ms(1000);
        bus_set_rx_mode();
    }
}
