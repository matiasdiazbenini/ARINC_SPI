#include <stdio.h>

#include "pico/stdlib.h"

#include "bus/bus.h"
#include "mil1553_words.h"


int main(void) {
    const bool tr = false;
    const uint8_t word_count = 3u;

    stdio_init_all();
    sleep_ms(1200);

    bus_init();
    bus_set_tx_mode();

    while (true) {
        bus_idle();
        sleep_ms(1000);

        bus_send_byte(0xF0);
        uint16_t cmd = mil1553_build_command(3, tr, 1, word_count);

        bus_send_word16_parity(cmd);

        if (!tr) {
            for (uint8_t i = 0; i < word_count; ++i) {
                const uint16_t tx_data = mil1553_build_data((uint16_t)(0xA000u + i));
                bus_send_word16_parity(tx_data);
                printf("TX_DATA[%u]=0x%04X\n", (unsigned)i, tx_data);
            }
        }

        bus_idle();
        bus_set_rx_mode();
        sleep_ms(BIT_PERIOD_MS);

        bool data_ok = true;
        uint16_t status_word = 0;

        if (tr) {
            for (uint8_t i = 0; i < word_count; ++i) {
                uint16_t rx_data = 0;
                if (!bus_read_word16_parity(&rx_data)) {
                    printf("DATA_PARITY_OR_READ_ERROR\n");
                    data_ok = false;
                    break;
                }

                printf("RX_DATA[%u]=0x%04X\n", (unsigned)i, rx_data);
            }
        }

        if (data_ok && bus_read_word16_parity(&status_word)) {
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
        } else if (data_ok) {
            printf("STATUS_PARITY_OR_READ_ERROR\n");
        }

        bus_set_tx_mode();
        bus_idle();
        sleep_ms(2000);
    }
}
