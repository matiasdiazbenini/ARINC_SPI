#include <stdio.h>
#include "pico/stdlib.h"
#include "bus/bus.h"

int main(void) {
    stdio_init_all();
    sleep_ms(1200);

    bus_init();

    while (true) {
        bus_set_tx_mode();

        for (int i = 0; i < 100; i++) {
            bus_send_byte(0xF0);

            bus_send_byte(0x18);
            bus_send_byte(0x23);

            bus_send_byte(0xA0);
            bus_send_byte(0x00);

            bus_send_byte(0xA0);
            bus_send_byte(0x01);

            bus_send_byte(0xA0);
            bus_send_byte(0x02);
        }
    }
}