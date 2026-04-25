#include <stdio.h>

#include "hardware/gpio.h"
#include "pico/stdlib.h"

#include "bus/bus.h"
#include "mil1553_words.h"

static bool has_valid_start(void) {
    const int p = gpio_get(BUS_PIN_P);
    const int n = gpio_get(BUS_PIN_N);
    return (p == 1 && n == 0) || (p == 0 && n == 1);
}

static bool looks_like_command(const mil1553_command_t *cmd) {
    if (cmd == NULL) {
        return false;
    }

    return (cmd->rt_address == 3u) &&
           (cmd->subaddress == 1u) &&
           (cmd->word_count > 0u);
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

        sleep_ms(BIT_PERIOD_MS / 4u);

        uint8_t sync = 0;
        if (!bus_read_byte(&sync) || sync != 0xF0u) {
            printf("SYNC_ERROR|SYNC=0x%02X\n", sync);
            continue;
        }

        printf("SYNC DETECTADO\n");

        uint16_t first_word = 0;
        if (!bus_read_word16_parity(&first_word)) {
            printf("RX_WORD_ERROR[0]\n");
            continue;
        }

        mil1553_command_t cmd = {0};
        if (mil1553_decode_command(first_word, &cmd) && looks_like_command(&cmd)) {
            printf("CMD=0x%04X\n", first_word);
            printf("CMD_DECODED|RT=%u|TR=%u|SA=%u|WC=%u\n",
                   cmd.rt_address,
                   cmd.transmit ? 1u : 0u,
                   cmd.subaddress,
                   cmd.word_count);

            if (!cmd.transmit) {
                for (uint8_t i = 0; i < cmd.word_count; ++i) {
                    uint16_t data_word = 0;
                    if (!bus_read_word16_parity(&data_word)) {
                        printf("DATA_ERROR[%u]\n", (unsigned)i);
                        break;
                    }

                    printf("DATA[%u]=0x%04X\n", (unsigned)i, data_word);
                }
            }

            continue;
        }

        mil1553_status_t status = {0};
        if (mil1553_decode_status(first_word, &status)) {
            printf("STATUS=0x%04X\n", first_word);
            printf("STATUS_DECODED|RT=%u|ME=%u|SR=%u|BUSY=%u|TF=%u\n",
                   status.rt_address,
                   status.message_error ? 1u : 0u,
                   status.service_request ? 1u : 0u,
                   status.busy ? 1u : 0u,
                   status.terminal_flag ? 1u : 0u);
        }
    }
}
