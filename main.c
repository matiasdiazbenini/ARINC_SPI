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

    printf("SLAVE RX CHECKED PACKET TEST\n");

    while (true) {
        uint16_t cmd = 0;
        uint16_t data[MAX_DATA_WORDS] = {0};

        /*
         * Por ahora seguimos leyendo 3 porque el paquete transmitido tiene 3.
         * En el paso siguiente hacemos lectura automática real.
         */
        if (bus_read_packet_checked_pio(&cmd, data, 3)) {
            uint8_t wc = (uint8_t)(cmd & 0x00FFu);

            printf("PACKET_OK|CMD=0x%04X|WC=%u", cmd, wc);

            for (uint8_t i = 0; i < wc && i < MAX_DATA_WORDS; i++) {
                printf("|D%u=0x%04X", i, data[i]);
            }

            printf("\n");
        }

        sleep_ms(50);
    }
}