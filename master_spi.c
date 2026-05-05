#include <stdio.h>
#include <stdint.h>

#include "pico/stdlib.h"
#include "bus/bus.h"

int main(void) {
    stdio_init_all();
    sleep_ms(1200);

    bus_init();

    const uint16_t cmd = 0x1803;

    const uint16_t data[3] = {
        0xBEEF,
        0xCAFE,
        0x55AA
    };

    while (true) {
        bus_set_tx_mode();

        for (int i = 0; i < 100; i++) {
            bus_send_packet_checked(cmd, data, 3);
        }
    }
}