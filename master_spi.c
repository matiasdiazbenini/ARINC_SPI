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

        uint16_t chk_test = cmd;

        for (uint8_t i = 0; i < wc; i++) {
            chk_test ^= data[i];
        }

        printf("BC_TX|CMD=0x%04X|WC=%u|CHK=0x%04X\n", cmd, wc, chk_test);

        bus_send_packet_checked(cmd, data, wc);

        sleep_us(30 * BIT_PERIOD_US);

        bus_idle();
        bus_set_rx_mode();

        printf("BC_SENT|CMD=0x%04X\n", cmd);

        uint16_t status = 0;

        bool status_ok = false;

        for (int attempt = 0; attempt < 10; attempt++) {
            if (bus_read_status_word_pio(&status)) {
                status_ok = true;
                break;
            }

            sleep_ms(20);
        }

        if (status_ok) {
            uint8_t st_rt = BUS_1553_STATUS_RT(status);
            uint8_t msg_error = BUS_1553_STATUS_MSG_ERROR(status);

            printf("STATUS_OK|RAW=0x%04X|RT=%u|MSG_ERROR=%u\n",
                status,
                st_rt,
                msg_error);
        } else {
            printf("STATUS_NOT_FOUND\n");
        }

        sleep_ms(100);
    }
}