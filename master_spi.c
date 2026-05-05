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
            uint16_t cmd = 0x1823;
            uint16_t d0  = 0xBEEF;
            uint16_t d1  = 0xCAFE;
            uint16_t d2  = 0x55AA;
            uint16_t chk = cmd ^ d0 ^ d1 ^ d2;

            bus_send_byte(0xF0);
            bus_send_word16(cmd);

            bus_send_byte(0x0F);
            bus_send_word16(d0);
            bus_send_word16(d1);
            bus_send_word16(d2);
            bus_send_word16(chk);
        }
    }
}