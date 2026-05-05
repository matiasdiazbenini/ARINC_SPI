#include <stdio.h>
#include <stdint.h>

#include "pico/stdlib.h"
#include "bus/bus.h"

#define MAX_DATA_WORDS 31u

int main(void) {
    stdio_init_all();
    sleep_ms(1200);

    bus_init();
    bus_set_rx_mode();

    printf("SLAVE RX 1553-LIKE PACKET TEST\n");

    while (true) {
        uint16_t cmd = 0;
        uint16_t data[MAX_DATA_WORDS] = {0};
        uint8_t wc = 0;

        if (bus_read_packet_checked_auto_pio(&cmd, data, MAX_DATA_WORDS, &wc)) {
            uint8_t rt  = BUS_1553_CMD_RT(cmd);
            uint8_t tr  = BUS_1553_CMD_TR(cmd);
            uint8_t sub = BUS_1553_CMD_SUB(cmd);

            printf("CMD_OK|RAW=0x%04X|RT=%u|TR=%u|SUB=%u|WC=%u",
                   cmd,
                   rt,
                   tr,
                   sub,
                   wc);

            for (uint8_t i = 0; i < wc; i++) {
                printf("|D%u=0x%04X", i, data[i]);
            }

            printf("\n");
        }

        sleep_ms(50);
    }
}