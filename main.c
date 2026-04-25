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

static void wait_and_align_to_new_transmitter(void) {
    while (!has_valid_start()) {
        sleep_ms(10);
    }

    sleep_ms(BIT_PERIOD_MS / 4u);
}

static bool read_expected_sync(uint8_t expected_type, const char *label) {
    uint8_t sync_type = 0;

    if (!bus_read_sync(&sync_type)) {
        printf("%s_SYNC_ERROR\n", label);
        return false;
    }

    if (sync_type != expected_type) {
        printf("%s_SYNC_TYPE_ERROR|TYPE=%u\n", label, sync_type);
        return false;
    }

    if (expected_type == BUS_SYNC_TYPE_CMD_STATUS) {
        printf("SYNC_CMD_STATUS\n");
    } else if (expected_type == BUS_SYNC_TYPE_DATA) {
        printf("SYNC_DATA\n");
    }

    return true;
}

static bool read_and_print_status(void) {
    uint16_t status_word = 0;

    if (!bus_read_word16_parity(&status_word)) {
        printf("STATUS_WORD_ERROR\n");
        return false;
    }

    mil1553_status_t status = {0};

    if (!mil1553_decode_status(status_word, &status)) {
        printf("STATUS_DECODE_ERROR|WORD=0x%04X\n", status_word);
        return false;
    }

    printf("STATUS=0x%04X\n", status_word);
    printf("STATUS_DECODED|RT=%u|ME=%u|SR=%u|BUSY=%u|TF=%u\n",
           status.rt_address,
           status.message_error ? 1u : 0u,
           status.service_request ? 1u : 0u,
           status.busy ? 1u : 0u,
           status.terminal_flag ? 1u : 0u);

    return true;
}

int main(void) {
    stdio_init_all();
    sleep_ms(1200);

    bus_init();
    bus_set_rx_mode();

    while (true) {
        /*
         * Primer transmisor: BC.
         * Esperamos inicio real desde IDLE y alineamos.
         */
        wait_and_align_to_new_transmitter();

        if (!read_expected_sync(BUS_SYNC_TYPE_CMD_STATUS, "CMD")) {
            sleep_ms(BIT_PERIOD_MS * 2u);
            continue;
        }

        uint16_t cmd_word = 0;
        if (!bus_read_word16_parity(&cmd_word)) {
            printf("CMD_WORD_ERROR\n");
            sleep_ms(BIT_PERIOD_MS * 2u);
            continue;
        }

        mil1553_command_t cmd = {0};
        if (!mil1553_decode_command(cmd_word, &cmd)) {
            printf("CMD_DECODE_ERROR|WORD=0x%04X\n", cmd_word);
            sleep_ms(BIT_PERIOD_MS * 2u);
            continue;
        }

        printf("CMD=0x%04X\n", cmd_word);
        printf("CMD_DECODED|RT=%u|TR=%u|SA=%u|WC=%u\n",
               cmd.rt_address,
               cmd.transmit ? 1u : 0u,
               cmd.subaddress,
               cmd.word_count);

        if (!cmd.transmit) {
            /*
             * TR=0:
             * BC sigue transmitiendo DATA inmediatamente después del CMD.
             * Por eso NO esperamos idle ni realineamos entre DATA.
             */
            for (uint8_t i = 0; i < cmd.word_count; ++i) {
                if (!read_expected_sync(BUS_SYNC_TYPE_DATA, "DATA")) {
                    sleep_ms(BIT_PERIOD_MS * 2u);
                    break;
                }

                uint16_t data_word = 0;
                if (!bus_read_word16_parity(&data_word)) {
                    printf("DATA_WORD_ERROR[%u]\n", (unsigned)i);
                    sleep_ms(BIT_PERIOD_MS * 2u);
                    break;
                }

                printf("DATA[%u]=0x%04X\n", (unsigned)i, data_word);
            }

            /*
             * Cambio de transmisor: ahora responde el RT.
             * Volvemos a esperar inicio y alinear.
             */
            wait_and_align_to_new_transmitter();

            if (!read_expected_sync(BUS_SYNC_TYPE_CMD_STATUS, "STATUS")) {
                sleep_ms(BIT_PERIOD_MS * 2u);
                continue;
            }

            read_and_print_status();
        } else {
            /*
             * TR=1:
             * Después del CMD cambia el transmisor: responde el RT.
             * Esperamos inicio y alineamos.
             */
            wait_and_align_to_new_transmitter();

            for (uint8_t i = 0; i < cmd.word_count; ++i) {
                if (!read_expected_sync(BUS_SYNC_TYPE_DATA, "DATA")) {
                    sleep_ms(BIT_PERIOD_MS * 2u);
                    break;
                }

                uint16_t data_word = 0;
                if (!bus_read_word16_parity(&data_word)) {
                    printf("DATA_WORD_ERROR[%u]\n", (unsigned)i);
                    sleep_ms(BIT_PERIOD_MS * 2u);
                    break;
                }

                printf("DATA[%u]=0x%04X\n", (unsigned)i, data_word);
            }

            if (!read_expected_sync(BUS_SYNC_TYPE_CMD_STATUS, "STATUS")) {
                sleep_ms(BIT_PERIOD_MS * 2u);
                continue;
            }

            read_and_print_status();
        }
    }
}