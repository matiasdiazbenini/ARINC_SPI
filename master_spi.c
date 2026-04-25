#include <stdio.h>

#include "pico/stdlib.h"

#include "bus/bus.h"
#include "mil1553_words.h"


int main(void) {
    stdio_init_all();
    sleep_ms(1200);

    bus_init();
    bus_set_tx_mode();

    while (true) {
        bus_idle();
        sleep_ms(1000);

        bus_send_byte(0xF0);
        uint16_t cmd = mil1553_build_command(3, false, 1, 1);
        uint16_t data = mil1553_build_data(0xA5A5);

        bus_send_word16_parity(cmd);
        bus_send_word16_parity(data);

        bus_idle();
        bus_set_rx_mode();
        sleep_ms(BIT_PERIOD_MS);

        uint16_t status_word = 0;

        if (bus_read_word16_parity(&status_word)) {
            printf("RX_STATUS=0x%04X\n", status_word);

            mil1553_status_t status = {0};

            if (mil1553_decode_status(status_word, &status)) {
                printf("STATUS_DECODED|RT=%u|ME=%u|SR=%u|BUSY=%u|TF=%u\n",
                    status.rt_address,
                    status.message_error ? 1 : 0,
                    status.service_request ? 1 : 0,
                    status.busy ? 1 : 0,
                    status.terminal_flag ? 1 : 0);
            } else {
                printf("STATUS_DECODE_ERROR\n");
            }
        } else {
            printf("STATUS_PARITY_OR_READ_ERROR\n");
        }

        bus_set_tx_mode();
        bus_idle();
        sleep_ms(2000);
    }
}
