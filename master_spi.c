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

        sleep_ms(200);
    }
}