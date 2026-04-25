#include <stdio.h>

#include "hardware/gpio.h"
#include "pico/stdlib.h"

#include "mil1553_words.h"
#include "bus/bus.h"

#define RT_LOCAL_ADDRESS        3u
#define RT_SUPPORTED_SUBADDRESS 1u
#define RT_MAX_WORD_COUNT       3u

static bool has_valid_start(void) {
    const int p = gpio_get(BUS_PIN_P);
    const int n = gpio_get(BUS_PIN_N);
    return (p == 1 && n == 0) || (p == 0 && n == 1);
}

int main(void) {
    stdio_init_all();
    sleep_ms(1200);

    bus_init();
    bus_set_rx_mode();

    while (true) {
        while (!has_valid_start()) {
            sleep_ms(10);
        }

        sleep_ms(BIT_PERIOD_US / 4u);

        uint8_t sync_type = 0;
        if (!bus_read_sync(&sync_type) || sync_type != BUS_SYNC_TYPE_CMD_STATUS) {
            printf("SYNC_ERROR\n");
            continue;
        }

        printf("SYNC DETECTADO\n");

        uint16_t cmd_word = 0;
        if (!bus_read_word16_parity(&cmd_word)) {
            printf("CMD_PARITY_OR_READ_ERROR\n");
            continue;
        }

        mil1553_command_t cmd = {0};

        if (!mil1553_decode_command(cmd_word, &cmd)) {
            printf("CMD_DECODE_ERROR\n");
            continue;
        }

        printf("CMD=0x%04X\n", cmd_word);
        printf("CMD_DECODED|RT=%u|TR=%u|SA=%u|WC=%u\n",
               cmd.rt_address,
               cmd.transmit ? 1u : 0u,
               cmd.subaddress,
               cmd.word_count);

        const bool cmd_valid = (cmd.rt_address == RT_LOCAL_ADDRESS) &&
                               (cmd.subaddress == RT_SUPPORTED_SUBADDRESS) &&
                               (cmd.word_count > 0u) &&
                               (cmd.word_count <= RT_MAX_WORD_COUNT);

        printf("%s\n", cmd_valid ? "CMD_VALID" : "CMD_INVALID");

        bool data_error = false;

        if (!cmd.transmit) {
            for (uint8_t i = 0; i < cmd.word_count; ++i) {
                uint8_t data_sync_type = 0;

                if (!bus_read_sync(&data_sync_type) || data_sync_type != BUS_SYNC_TYPE_DATA) {
                    printf("DATA_SYNC_ERROR[%u]\n", (unsigned)i);
                    data_error = true;
                    break;
                }

                uint16_t data = 0;
                if (!bus_read_word16_parity(&data)) {
                    printf("DATA_PARITY_OR_READ_ERROR[%u]\n", (unsigned)i);
                    data_error = true;
                    break;
                }

                printf("RX_DATA[%u]=0x%04X\n", (unsigned)i, data);
            }
        }

        mil1553_status_t status = {
            .rt_address = RT_LOCAL_ADDRESS,
            .message_error = (!cmd_valid) || data_error,
            .service_request = false,
            .busy = false,
            .terminal_flag = false,
        };

        uint16_t status_word = mil1553_build_status(&status);

        bus_set_tx_mode();
        bus_idle();
        sleep_ms(BIT_PERIOD_US / 2u);

        if (cmd.transmit) {
            for (uint8_t i = 0; i < cmd.word_count; ++i) {
                const uint16_t tx_data = (uint16_t)(0x1000u + i);

                bus_send_sync_data();
                bus_send_word16_parity(tx_data);

                printf("TX_DATA[%u]=0x%04X\n", (unsigned)i, tx_data);
            }
        }

        bus_send_sync_cmd_status();
        bus_send_word16_parity(status_word);

        printf("TX_STATUS=0x%04X\n", status_word);

        bus_idle();
        sleep_ms(1000);

        bus_set_rx_mode();
    }
}