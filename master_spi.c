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
        BUS_1553_TR_RT_TO_BC,
        subaddr,
        wc
    );

    while (true) {
        bus_set_tx_mode();

        printf("BC_TX_CMD|CMD=0x%04X|RT=%u|TR=%u|SUB=%u|WC=%u\n",
            cmd,
            BUS_1553_CMD_RT(cmd),
            BUS_1553_CMD_TR(cmd),
            BUS_1553_CMD_SUB(cmd),
            BUS_1553_CMD_WC(cmd));

        bus_send_command_word(cmd);

        sleep_us(30 * BIT_PERIOD_US);

        bus_idle();
        bus_set_rx_mode();

        uint16_t status = 0;
        uint16_t rx_data[BUS_1553_MAX_DATA_WORDS] = {0};
        bool response_ok = false;

        for (int attempt = 0; attempt < 20; attempt++) {
            if (bus_read_status_data_checked_pio(&status, rx_data, wc)) {
                response_ok = true;
                break;
            }

            sleep_ms(10);
        }

        if (response_ok) {
            uint8_t st_rt = BUS_1553_STATUS_RT(status);
            uint8_t msg_error = BUS_1553_STATUS_MSG_ERROR(status);

            printf("RT_DATA_OK|STATUS=0x%04X|RT=%u|MSG_ERROR=%u",
                status,
                st_rt,
                msg_error);

            for (uint8_t i = 0; i < wc; i++) {
                printf("|D%u=0x%04X", i, rx_data[i]);
            }

            printf("\n");
        } else {
            printf("RT_DATA_NOT_FOUND\n");
        }

        sleep_ms(200);
    }
}