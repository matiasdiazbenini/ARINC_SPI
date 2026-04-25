#include <stdio.h>

#include "pico/stdlib.h"

#include "bus/bus.h"
#include "mil1553_words.h"

#define TEST_RT_ADDRESS     3u
#define TEST_SUBADDRESS     1u
#define TEST_WORD_COUNT     3u
#define TEST_TR             true

int main(void) {
    stdio_init_all();
    sleep_ms(1200);

    bus_init();
    bus_set_tx_mode();

    while (true) {
        bus_idle();
        sleep_ms(1000);

        printf("---- FRAME ----\n");
        printf("MODE=%s|RT=%u|SA=%u|WC=%u\n",
               TEST_TR ? "RT_TO_BC" : "BC_TO_RT",
               TEST_RT_ADDRESS,
               TEST_SUBADDRESS,
               TEST_WORD_COUNT);

        uint16_t cmd = mil1553_build_command(
            TEST_RT_ADDRESS,
            TEST_TR,
            TEST_SUBADDRESS,
            TEST_WORD_COUNT
        );

        bus_send_sync_cmd_status();
        bus_send_word16_parity(cmd);

        if (!TEST_TR) {
            for (uint8_t i = 0; i < TEST_WORD_COUNT; ++i) {
                const uint16_t tx_data = mil1553_build_data((uint16_t)(0xA000u + i));

                bus_send_sync_data();
                bus_send_word16_parity(tx_data);

                printf("TX_DATA[%u]=0x%04X\n", (unsigned)i, tx_data);
            }
        }

        bus_idle();
        bus_set_rx_mode();
        sleep_ms(BIT_PERIOD_MS);

        bool data_ok = true;
        uint16_t status_word = 0;

        if (TEST_TR) {
            for (uint8_t i = 0; i < TEST_WORD_COUNT; ++i) {
                uint8_t sync_type = 0;

                if (!bus_read_sync(&sync_type) || sync_type != BUS_SYNC_TYPE_DATA) {
                    printf("DATA_SYNC_ERROR[%u]\n", (unsigned)i);
                    data_ok = false;
                    break;
                }

                uint16_t rx_data = 0;
                if (!bus_read_word16_parity(&rx_data)) {
                    printf("DATA_PARITY_OR_READ_ERROR[%u]\n", (unsigned)i);
                    data_ok = false;
                    break;
                }

                printf("RX_DATA[%u]=0x%04X\n", (unsigned)i, rx_data);
            }
        }

        if (data_ok) {
            uint8_t sync_type = 0;

            if (!bus_read_sync(&sync_type) || sync_type != BUS_SYNC_TYPE_CMD_STATUS) {
                printf("STATUS_SYNC_ERROR\n");
                data_ok = false;
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