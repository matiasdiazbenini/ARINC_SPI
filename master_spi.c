#include <stdio.h>
#include <stdint.h>

#include "pico/stdlib.h"
#include "bus/bus.h"

int main(void) {
    stdio_init_all();
    sleep_ms(1200);

    bus_init();

    const uint16_t data[2] = {
        0x1234,
        0xABCD
    };

    const uint8_t wc = 2;
    const uint16_t cmd = BUS_CMD_MAKE(BUS_PKT_TYPE_DATA, 8, wc);

    while (true) {
        bus_set_tx_mode();

        for (int i = 0; i < 100; i++) {
            bus_send_packet_checked(cmd, data, wc);
        }
    }
}