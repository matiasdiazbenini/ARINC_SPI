#include <stdio.h>
#include <stdint.h>

#include "pico/stdlib.h"
#include "bus/bus.h"

#define MY_RT_ADDR 3u

int main(void)
{
    stdio_init_all();
    sleep_ms(1200);

    bus_init();
    bus_set_rx_mode();

    printf("MIL1553 REAL RX TEST\n");

    while (true) {

        uint16_t cmd = 0;

        if (bus_read_command_word_parity_pio(&cmd)) {

            printf(
                "CMD=0x%04X|RT=%u|TR=%u|SUB=%u|WC=%u\n",
                cmd,
                BUS_1553_CMD_RT(cmd),
                BUS_1553_CMD_TR(cmd),
                BUS_1553_CMD_SUB(cmd),
                BUS_1553_CMD_WC(cmd)
            );
        }
    }
}