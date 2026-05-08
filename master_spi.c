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

    /*
     * TR=0: BC transmite datos al RT.
     */
    const uint16_t cmd = BUS_1553_CMD_MAKE(
        rt_addr,
        BUS_1553_TR_BC_TO_RT,
        subaddr,
        wc
    );

    const uint16_t tx_data[3] = {
        0x1234,
        0xABCD,
        0x55AA
    };

    while (true) {
        bus_set_tx_mode();

        bus_send_packet_checked(cmd, tx_data, wc);

        sleep_us(50 * BIT_PERIOD_US);

        bus_idle();
        bus_set_rx_mode();

        uint16_t status = 0;
        bool status_ok = false;

        for (int attempt = 0; attempt < 20; attempt++) {
            if (bus_read_status_word_pio(&status)) {
                status_ok = true;
                break;
            }

            sleep_ms(10);
        }

        if (status_ok) {
            printf("STATUS_OK|RAW=0x%04X|RT=%u|MSG_ERROR=%u\n",
                   status,
                   BUS_1553_STATUS_RT(status),
                   BUS_1553_STATUS_MSG_ERROR(status));
        }

        sleep_ms(200);
    }
}