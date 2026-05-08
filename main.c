#include <stdio.h>
#include <stdint.h>

#include "pico/stdlib.h"
#include "bus/bus.h"

#define MY_RT_ADDR      3u
#define MAX_DATA_WORDS  8u

int main(void) {
    stdio_init_all();
    sleep_ms(1200);

    bus_init();
    bus_set_rx_mode();

    printf("SLAVE RT UNIFIED 1553-LIKE TEST\n");

    /*
     * Datos que el RT enviará cuando el BC pida datos con TR=1.
     */
    const uint16_t rt_tx_data[3] = {
        0x1111,
        0x2222,
        0x3333
    };

    while (true) {
        /*
         * ============================================================
         * CASO 1:
         * TR=0 → BC transmite datos al RT.
         *
         * Esperamos:
         * F0 + CMD + 0F + DATA + CHK
         * ============================================================
         */
        uint16_t cmd = 0;
        uint16_t rx_data[MAX_DATA_WORDS] = {0};
        uint8_t wc = 0;

        if (bus_read_packet_checked_auto_pio(&cmd,
                                             rx_data,
                                             MAX_DATA_WORDS,
                                             &wc)) {
            uint8_t rt  = BUS_1553_CMD_RT(cmd);
            uint8_t tr  = BUS_1553_CMD_TR(cmd);
            uint8_t sub = BUS_1553_CMD_SUB(cmd);

            if (rt == MY_RT_ADDR && tr == BUS_1553_TR_BC_TO_RT) {
                printf("BC_TO_RT_OK|CMD=0x%04X|RT=%u|TR=%u|SUB=%u|WC=%u",
                       cmd,
                       rt,
                       tr,
                       sub,
                       wc);

                for (uint8_t i = 0; i < wc; i++) {
                    printf("|D%u=0x%04X", i, rx_data[i]);
                }

                printf("\n");

                /*
                 * Responder STATUS.
                 */
                sleep_us(3000);

                bus_set_tx_mode();

                bus_send_status_word(MY_RT_ADDR, false);

                sleep_us(30 * BIT_PERIOD_US);

                bus_idle();
                bus_set_rx_mode();

                printf("STATUS_SENT|RT=%u|MSG_ERROR=0\n", MY_RT_ADDR);
            }

            sleep_ms(20);
            continue;
        }

        /*
         * ============================================================
         * CASO 2:
         * TR=1 → BC solicita datos al RT.
         *
         * Esperamos:
         * F0 + CMD
         *
         * Respondemos:
         * F0 + STATUS + 0F + DATA + CHK
         * ============================================================
         */
        cmd = 0;

        if (bus_read_command_word_pio(&cmd)) {
            uint8_t rt  = BUS_1553_CMD_RT(cmd);
            uint8_t tr  = BUS_1553_CMD_TR(cmd);
            uint8_t sub = BUS_1553_CMD_SUB(cmd);
            wc          = BUS_1553_CMD_WC(cmd);

            if (rt == MY_RT_ADDR && tr == BUS_1553_TR_RT_TO_BC) {
                printf("RT_TO_BC_REQ|CMD=0x%04X|RT=%u|TR=%u|SUB=%u|WC=%u\n",
                       cmd,
                       rt,
                       tr,
                       sub,
                       wc);

                /*
                 * Para esta prueba, solo tenemos 3 palabras disponibles.
                 */
                uint8_t tx_wc = wc;

                if (tx_wc > 3u) {
                    tx_wc = 3u;
                }

                /*
                 * Guarda para que el BC pase a RX.
                 */
                sleep_us(3000);

                bus_set_tx_mode();

                bus_send_status_data_checked(MY_RT_ADDR,
                                             false,
                                             rt_tx_data,
                                             tx_wc);

                sleep_us(30 * BIT_PERIOD_US);

                bus_idle();
                bus_set_rx_mode();

                printf("STATUS_DATA_SENT|RT=%u|WC=%u",
                       MY_RT_ADDR,
                       tx_wc);

                for (uint8_t i = 0; i < tx_wc; i++) {
                    printf("|D%u=0x%04X", i, rt_tx_data[i]);
                }

                printf("\n");
            }

            sleep_ms(20);
            continue;
        }

        sleep_ms(20);
    }
}