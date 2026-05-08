#include <stdio.h>
#include <stdint.h>

#include "pico/stdlib.h"
#include "bus/bus.h"

#define MY_RT_ADDR      3u
#define MAX_DATA_WORDS  8u

#define SUB_DATA        2u
#define SUB_STATUS      3u
#define SUB_DIAG        4u

static bool rt_is_valid_subaddress(uint8_t sub) {
    switch (sub) {
        case SUB_DATA:
        case SUB_STATUS:
        case SUB_DIAG:
            return true;

        default:
            return false;
    }
}

static bool rt_is_valid_wc(uint8_t wc) {
    if (wc == 0u) {
        return false;
    }

    if (wc > MAX_DATA_WORDS) {
        return false;
    }

    return true;
}

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

        if (bus_read_packet_parity_pio(&cmd,
                                             rx_data,
                                             MAX_DATA_WORDS,
                                             &wc)) {
            uint8_t rt  = BUS_1553_CMD_RT(cmd);
            uint8_t tr  = BUS_1553_CMD_TR(cmd);
            uint8_t sub = BUS_1553_CMD_SUB(cmd);

            if (rt == MY_RT_ADDR && tr == BUS_1553_TR_BC_TO_RT) {
                bool msg_error = false;

                if (!rt_is_valid_subaddress(sub)) {
                    msg_error = true;
                }

                if (!rt_is_valid_wc(wc)) {
                    msg_error = true;
                }

                printf("BC_TO_RT_PARITY_OK|CMD=0x%04X|RT=%u|TR=%u|SUB=%u|WC=%u",
                    cmd,
                    rt,
                    tr,
                    sub,
                    wc);

                for (uint8_t i = 0; i < wc; i++) {
                    printf("|D%u=0x%04X", i, rx_data[i]);
                }

                printf("|MSG_ERROR=%u\n", msg_error ? 1u : 0u);

                sleep_us(3000);

                bus_set_tx_mode();

                bus_send_status_word_parity(MY_RT_ADDR, msg_error);

                sleep_us(30 * BIT_PERIOD_US);

                bus_idle();
                bus_set_rx_mode();

                printf("STATUS_PARITY_SENT|RT=%u|MSG_ERROR=%u\n",
                    MY_RT_ADDR,
                    msg_error ? 1u : 0u);
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

        if (bus_read_command_word_parity_pio(&cmd)) {
            uint8_t rt  = BUS_1553_CMD_RT(cmd);
            uint8_t tr  = BUS_1553_CMD_TR(cmd);
            uint8_t sub = BUS_1553_CMD_SUB(cmd);
            wc          = BUS_1553_CMD_WC(cmd);

            if (rt == MY_RT_ADDR && tr == BUS_1553_TR_RT_TO_BC) {
                bool msg_error = false;

                if (!rt_is_valid_subaddress(sub)) {
                    msg_error = true;
                }

                if (!rt_is_valid_wc(wc)) {
                    msg_error = true;
                }

                printf("RT_TO_BC_REQ|CMD=0x%04X|RT=%u|TR=%u|SUB=%u|WC=%u|MSG_ERROR=%u\n",
                    cmd,
                    rt,
                    tr,
                    sub,
                    wc,
                    msg_error ? 1u : 0u);

                /*
                * Guarda para que el BC pase a RX.
                */
                sleep_us(3000);

                bus_set_tx_mode();

                if (msg_error) {
                    /*
                    * Si hay error, respondemos solo STATUS con MSG_ERROR=1.
                    */
                    bus_send_status_word_parity(MY_RT_ADDR, true);

                    sleep_us(30 * BIT_PERIOD_US);

                    bus_idle();
                    bus_set_rx_mode();

                    printf("STATUS_SENT|RT=%u|MSG_ERROR=1\n", MY_RT_ADDR);
                } else {
                    /*
                    * Si está todo bien, respondemos STATUS + DATA.
                    */
                    uint8_t tx_wc = wc;

                    if (tx_wc > 3u) {
                        tx_wc = 3u;
                    }

                    bus_send_status_data_parity(MY_RT_ADDR,
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
            }

            sleep_ms(20);
            continue;
        }

        sleep_ms(20);
    }
}