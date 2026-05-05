#include <stdio.h>
#include <stdint.h>

#include "pico/stdlib.h"
#include "bus/bus.h"

#define MAX_DATA_WORDS 8u
#define MY_RT_ADDR     3u

int main(void) {
    stdio_init_all();
    sleep_ms(1200);

    bus_init();
    bus_set_rx_mode();

    printf("SLAVE RT RX 1553-LIKE TEST\n");

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

            /*
             * Si el comando era para este RT y era BC->RT,
             * respondemos Status.
             */
            if (rt == MY_RT_ADDR && tr == BUS_1553_TR_BC_TO_RT) {
                sleep_us(3000);      // pequeña guarda antes de responder
                bus_set_tx_mode();

                bus_send_status_word(MY_RT_ADDR, false);

                sleep_us(20 * BIT_PERIOD_US);

                bus_idle();
                bus_set_rx_mode();

                printf("STATUS_SENT|RT=%u|MSG_ERROR=0\n", MY_RT_ADDR);
            }
        }

        sleep_ms(20);
    }
}