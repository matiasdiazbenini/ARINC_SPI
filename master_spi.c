#include <stdio.h>
#include <stdint.h>

#include "pico/stdlib.h"
#include "bus/bus.h"

int main(void) {
    stdio_init_all();
    sleep_ms(1200);

    bus_init();

    const uint8_t rt_addr = 3;
    const uint8_t subaddr = 2;
    const uint8_t wc = 3;

    const uint16_t cmd = BUS_1553_CMD_MAKE(
        rt_addr,
        BUS_1553_TR_BC_TO_RT,
        subaddr,
        wc
    );

    const uint16_t data[3] = {
        0x1234,
        0xABCD,
        0x55AA
    };

    while (true) {
        bus_set_tx_mode();

        for (int i = 0; i < 100; i++) {
            bus_send_packet_checked(cmd, data, wc);
        }
    }
}