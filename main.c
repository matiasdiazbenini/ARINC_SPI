#include <stdio.h>
#include <stdint.h>

#include "pico/stdlib.h"
#include "bus/bus.h"

#define MAX_DATA_WORDS 8u

int main(void) {
    stdio_init_all();
    sleep_ms(1200);

    bus_init();
    bus_set_rx_mode();

    printf("SLAVE RX AUTO WC PACKET TEST\n");

    while (true) {
        uint16_t cmd = 0;
        uint16_t data[MAX_DATA_WORDS] = {0};
        uint8_t wc = 0;

        if (bus_read_packet_checked_auto_pio(&cmd, data, MAX_DATA_WORDS, &wc)) {
            printf("PACKET_OK|CMD=0x%04X|WC=%u", cmd, wc);

            for (uint8_t i = 0; i < wc; i++) {
                printf("|D%u=0x%04X", i, data[i]);
            }

            printf("\n");
        } else {
            printf("AUTO_NOT_FOUND\n");
        }

        sleep_ms(100);
    }
}