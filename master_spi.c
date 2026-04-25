#include <stdio.h>

#include "pico/stdlib.h"

#include "bus/bus.h"

int main(void) {
    stdio_init_all();
    sleep_ms(1200);

    bus_init();
    bus_set_tx_mode();

    while (true) {
        bus_idle();
        sleep_ms(1000);

        bus_send_byte(0xF0);
        bus_send_word16_parity(0x1821);
        bus_send_word16_parity(0xA5A5);

        bus_idle();
        bus_set_rx_mode();
        sleep_ms(BIT_PERIOD_MS);

        uint16_t status = 0;
        if (bus_read_word16_parity(&status)) {
            printf("RX_STATUS=0x%04X\n", status);
        } else {
            printf("STATUS_PARITY_OR_READ_ERROR\n");
        }

        bus_set_tx_mode();
        bus_idle();
        sleep_ms(2000);
    }
}
