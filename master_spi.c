#include <stdio.h>

#include "pico/stdlib.h"

#include "bus/bus.h"
#include "mil1553_words.h"

#define TEST_RT_ADDRESS     3u
#define TEST_SUBADDRESS     1u
#define TEST_WORD_COUNT     3u 
#define TEST_TR             false
#define TEST_MODE_CODE      false
#define TEST_MODE_LAST_COMMAND false
// Modo temporal para aislar PIO TX bit a bit.
// true  -> envia 1,0,1,0 con bus_send_bit()
// false -> flujo normal del protocolo
#define TEST_PIO_TX_BIT_MODE false

#define MC_TRANSMIT_STATUS       0u
#define MC_TRANSMIT_LAST_COMMAND 2u

int main(void) {
    stdio_init_all();
    sleep_ms(1200);

    bus_init();
    bus_set_tx_mode();

    while (true) {
        bus_idle();
        sleep_ms(1000);

        if (TEST_PIO_TX_BIT_MODE) {
            const bool test_bits[4] = {true, false, true, false};
            for (uint i = 0; i < 4u; ++i) {
                bus_send_bit(test_bits[i]);
                printf("TX_BIT=%u\n", test_bits[i] ? 1u : 0u);
                sleep_ms(500);
            }
            bus_idle();
            continue;
        }

        const bool mode_code_last_command = TEST_MODE_LAST_COMMAND;
        const bool mode_code_status = TEST_MODE_CODE && !mode_code_last_command;
        const bool use_mode_code = mode_code_status || mode_code_last_command;

        const bool cmd_tr = mode_code_last_command ? true : (mode_code_status ? false : TEST_TR);
        const uint8_t cmd_subaddress = use_mode_code ? 0u : TEST_SUBADDRESS;
        const uint8_t cmd_word_count = mode_code_last_command ? MC_TRANSMIT_LAST_COMMAND :
                                       (mode_code_status ? MC_TRANSMIT_STATUS : TEST_WORD_COUNT);

        printf("---- FRAME ----\n");
        printf("MODE=%s|RT=%u|SA=%u|WC=%u\n",
               cmd_tr ? "RT_TO_BC" : "BC_TO_RT",
               TEST_RT_ADDRESS,
               cmd_subaddress,
               cmd_word_count);

        if (mode_code_last_command) {
            printf("MODE_CODE_REQUEST|Transmit_Last_Command\n");
        } else if (mode_code_status) {
            printf("MODE_CODE_REQUEST|Transmit_Status\n");
        }

        uint16_t cmd = mil1553_build_command(
            TEST_RT_ADDRESS,
            cmd_tr,
            cmd_subaddress,
            cmd_word_count
        );

        bus_send_sync_cmd_status();
        bus_send_word16_parity(cmd);

        if (!use_mode_code && !TEST_TR) {
            for (uint8_t i = 0; i < cmd_word_count; ++i) {
                const uint16_t tx_data = mil1553_build_data((uint16_t)(0xA000u + i));

                bus_send_sync_data();
                bus_send_word16_parity(tx_data);

                printf("TX_DATA[%u]=0x%04X\n", (unsigned)i, tx_data);
            }
        }

        bus_idle();
        bus_set_rx_mode();
        sleep_us(BIT_PERIOD_US);

        bool comm_error = false;
        uint16_t status_word = 0;

        if (mode_code_last_command) {
            uint8_t sync_type = 0;

            if (!bus_read_sync(&sync_type) || sync_type != BUS_SYNC_TYPE_DATA) {
                printf("LAST_CMD_SYNC_ERROR\n");
                comm_error = true;
            } else {
                uint16_t last_cmd_word = 0;
                if (!bus_read_word16_parity(&last_cmd_word)) {
                    printf("LAST_CMD_PARITY_OR_READ_ERROR\n");
                    comm_error = true;
                } else {
                    printf("RX_LAST_CMD=0x%04X\n", last_cmd_word);
                }
            }
        } else if (!use_mode_code && TEST_TR) {
            for (uint8_t i = 0; i < cmd_word_count; ++i) {
                uint8_t sync_type = 0;

                if (!bus_read_sync(&sync_type) || sync_type != BUS_SYNC_TYPE_DATA) {
                    printf("DATA_SYNC_ERROR[%u]\n", (unsigned)i);
                    comm_error = true;
                    break;
                }

                uint16_t rx_data = 0;
                if (!bus_read_word16_parity(&rx_data)) {
                    printf("DATA_PARITY_OR_READ_ERROR[%u]\n", (unsigned)i);
                    comm_error = true;
                    break;
                }

                printf("RX_DATA[%u]=0x%04X\n", (unsigned)i, rx_data);
            }
        }

        if (!comm_error) {
            uint8_t sync_type = 0;

            if (!bus_read_sync(&sync_type) || sync_type != BUS_SYNC_TYPE_CMD_STATUS) {
                printf("STATUS_SYNC_ERROR\n");
                comm_error = true;
            }
        }

        if (!comm_error) {
            if (!bus_read_word16_parity(&status_word)) {
                printf("STATUS_PARITY_OR_READ_ERROR\n");
                comm_error = true;
            }
        }

        if (comm_error) {
            printf("BC_EVENT|TIMEOUT_OR_SYNC_ERROR\n");
        } else {
            printf("RX_STATUS=0x%04X\n", status_word);
            mil1553_status_t status = {0};

            if (mil1553_decode_status(status_word, &status)) {
                printf("STATUS_DECODED|RT=%u|ME=%u|SR=%u|BUSY=%u|TF=%u\n",
                       status.rt_address,
                       status.message_error ? 1 : 0,
                       status.service_request ? 1 : 0,
                       status.busy ? 1 : 0,
                       status.terminal_flag ? 1 : 0);

                bool has_action = false;

                if (status.message_error) {
                    printf("STATUS_ACTION|MESSAGE_ERROR\n");
                    printf("BC_EVENT|ERROR_DETECTED\n");
                    has_action = true;
                }

                if (status.busy) {
                    printf("STATUS_ACTION|RT_BUSY\n");
                    printf("BC_EVENT|RETRYING\n");
                    sleep_ms(200);
                    has_action = true;
                }

                if (status.service_request) {
                    printf("STATUS_ACTION|SERVICE_REQUEST\n");
                    printf("BC_EVENT|SR_HANDLED\n");
                    has_action = true;
                }

                if (status.terminal_flag) {
                    printf("STATUS_ACTION|TERMINAL_FLAG\n");
                    has_action = true;
                }

                if (!has_action) {
                    printf("STATUS_ACTION|OK\n");
                }
            } else {
                printf("STATUS_DECODE_ERROR\n");
            }
        }

        bus_set_tx_mode();
        bus_idle();
        sleep_ms(2000);
    }
}
