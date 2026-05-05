#include <stdio.h>
#include <stdint.h>

#include "pico/stdlib.h"
#include "bus/bus.h"

int main(void) {
    stdio_init_all();
    sleep_ms(1200);

    bus_init();
    bus_set_rx_mode();

    printf("SLAVE RX PIO TEST\n");

    while (true) {
        uint16_t cmd = 0;
        uint16_t data[3] = {0};

        if (bus_read_test_frame_pio(&cmd, data, 3)) {
            printf("FRAME_OK|CMD=0x%04X|D0=0x%04X|D1=0x%04X|D2=0x%04X\n",
                   cmd,
                   data[0],
                   data[1],
                   data[2]);
        } else {
            printf("FRAME_NOT_FOUND\n");
        }

        sleep_ms(100);
    }
}