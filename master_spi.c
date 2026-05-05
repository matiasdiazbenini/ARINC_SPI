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

        bus_send_byte(0x0F);

        bus_send_byte(0xBE);
        bus_send_byte(0xEF);

        bus_send_byte(0xCA);
        bus_send_byte(0xFE);

        bus_send_byte(0x55);
        bus_send_byte(0xAA);

        bus_send_byte(0x39);
        bus_send_byte(0xB8);
    }
}
}