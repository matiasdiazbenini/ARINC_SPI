#include <stdio.h>
#include <stdint.h>

#include "pico/stdlib.h"
#include "bus/bus.h"

#define MY_RT_ADDR 3u

int main(void) {
    stdio_init_all();
    sleep_ms(1200);

    bus_init();
    bus_set_rx_mode();

    printf("SLAVE RT COMMAND RX + DATA RESPONSE TEST\n");

    const uint16_t rt_data[3] = {
        0x1111,
        0x2222,
        0x3333
    };

    while (true) {
        uint16_t cmd = 0;

        if (bus_read_command_word_pio(&cmd)) {
            uint8_t rt  = BUS_1553_CMD_RT(cmd);
            uint8_t tr  = BUS_1553_CMD_TR(cmd);
            uint8_t sub = BUS_1553_CMD_SUB(cmd);
            uint8_t wc  = BUS_1553_CMD_WC(cmd);

            printf("CMD_RX|RAW=0x%04X|RT=%u|TR=%u|SUB=%u|WC=%u\n",
                   cmd,
                   rt,
                   tr,
                   sub,
                   wc);

            if (rt == MY_RT_ADDR && tr == BUS_1553_TR_RT_TO_BC) {
                printf("RT_REQUEST_DETECTED|WC=%u\n", wc);

                /*
                 * Pequeña guarda para que el BC pueda pasar a RX.
                 */
                sleep_us(3000);

                bus_set_tx_mode();

                /*
                 * Por ahora respondemos hasta 3 palabras.
                 */
                uint8_t tx_wc = wc;

                if (tx_wc > 3u) {
                    tx_wc = 3u;
                }

                bus_send_status_data_checked(MY_RT_ADDR,
                                             false,
                                             rt_data,
                                             tx_wc);

                sleep_us(30 * BIT_PERIOD_US);

                bus_idle();
                bus_set_rx_mode();

                printf("STATUS_DATA_SENT|RT=%u|WC=%u|D0=0x%04X|D1=0x%04X|D2=0x%04X\n",
                       MY_RT_ADDR,
                       tx_wc,
                       rt_data[0],
                       rt_data[1],
                       rt_data[2]);
            }
        }

        sleep_ms(20);
    }
}