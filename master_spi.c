#include <stdio.h>
#include <stdint.h>

#include "pico/stdlib.h"
#include "bus/bus.h"

int main(void) {
    stdio_init_all();
    sleep_ms(1200);

    bus_init();

    const uint16_t cmd = 0x1823;
    const uint16_t d0  = 0xBEEF;
    const uint16_t d1  = 0xCAFE;
    const uint16_t d2  = 0x55AA;

    while (true) {
        bus_set_tx_mode();

        for (int i = 0; i < 100; i++) {
            uint16_t chk = cmd ^ d0 ^ d1 ^ d2;

            // SYNC CMD/STATUS
            bus_send_byte(0xF0);
            bus_send_word16(cmd);

            // SYNC DATA
            bus_send_byte(0x0F);
            bus_send_word16(d0);
            bus_send_word16(d1);
            bus_send_word16(d2);

            // CHECKSUM
            bus_send_word16(chk);
        }
    }
}