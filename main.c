#include <stdint.h>
#include <stdio.h>

#include "bus/bus.h"
#include "pico/stdlib.h"

#define TX_TEST_WORD        0x1800u
#define TX_START_DELAY_MS   1000u
#define TX_PERIOD_MS        500u

int main(void) {
    stdio_init_all();

    bus_init();

    // Espera inicial para estabilizar prueba y consola.
    sleep_ms(TX_START_DELAY_MS);

    bus_set_tx_mode();

    while (true) {
        bus_send_full_word(TX_TEST_WORD);
        printf("TX_TEST|WORD=0x%04X\n", TX_TEST_WORD);
        sleep_ms(TX_PERIOD_MS);
    }
}
