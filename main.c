#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>

#include "bus/bus.h"
#include "mil1553_words.h"
#include "pico/stdlib.h"

// Perfil minimo de RT para esta etapa.
#define RT_ADDRESS               3u
#define RT_SUBADDRESS            1u
#define RT_WORD_COUNT            1u
#define RT_TEST_DATA_WORD        0xA5A5u

#define RT_SYNC_TIMEOUT_US       30000u
#define RT_RESPONSE_DELAY_US     500u
#define RT_INTERWORD_GAP_US      200u
#define RT_RX_FAIL_REPORT_EVERY  20u

static bool rt_is_supported_tx_request(const mil1553_command_word_t *cmd) {
    return cmd &&
           cmd->transmit &&
           cmd->subaddress == RT_SUBADDRESS &&
           cmd->word_count == RT_WORD_COUNT;
}

static uint16_t rt_build_status_from_command(const mil1553_command_word_t *cmd) {
    mil1553_status_word_t status = {
        .rt_address = RT_ADDRESS,
        .message_error = !(cmd &&
                           cmd->subaddress == RT_SUBADDRESS &&
                           cmd->word_count == RT_WORD_COUNT),
        .service_request = false,
        .broadcast_command_received = false,
        .busy = false,
        .subsystem_flag = false,
        .dynamic_bus_control_acceptance = false,
        .terminal_flag = false,
    };

    return mil1553_logic_build_status_word(&status);
}

int main(void) {
    stdio_init_all();
    sleep_ms(1200);

    printf("RT logico MIL-STD-1553 iniciado (Command/Status/Data)\n");
    printf("RT=%u SA=%u WC=%u DATA=0x%04X\n",
           RT_ADDRESS,
           RT_SUBADDRESS,
           RT_WORD_COUNT,
           RT_TEST_DATA_WORD);

    bus_init();
    bus_set_rx_mode();

    uint32_t rx_fail_count = 0;

    while (true) {
        uint16_t rx_word = 0;
        bool parity_ok = false;

        if (!bus_receive_full_word(&rx_word, &parity_ok, RT_SYNC_TIMEOUT_US)) {
            // Timeout de sync o recepcion incompleta.
            rx_fail_count++;
            if (rx_fail_count == 1 || (rx_fail_count % RT_RX_FAIL_REPORT_EVERY) == 0) {
                printf("RX_WAIT|TIMEOUT_INVALID|COUNT=%lu\n", (unsigned long)rx_fail_count);
            }
            tight_loop_contents();
            continue;
        }

        if (rx_fail_count > 0) {
            printf("RX_WAIT|RECOVERED|MISSED=%lu\n", (unsigned long)rx_fail_count);
            rx_fail_count = 0;
        }

        if (!parity_ok) {
            printf("RX_ERR_PARITY|WORD=0x%04X\n", rx_word);
            continue;
        }

        printf("RX_OK|WORD=0x%04X|PARITY=OK\n", rx_word);

        mil1553_command_word_t command;
        mil1553_logic_decode_command_word(rx_word, &command);
        printf("CMD_DEC|RT=%u|TR=%u|SA=%u|WC=%u\n",
               command.rt_address,
               command.transmit ? 1 : 0,
               command.subaddress,
               command.word_count);

        // Solo procesa comandos dirigidos a este RT.
        if (command.rt_address != RT_ADDRESS) {
            continue;
        }

        uint16_t status_word = rt_build_status_from_command(&command);
        const bool send_data = rt_is_supported_tx_request(&command);

        bus_set_tx_mode();
        sleep_us(RT_RESPONSE_DELAY_US);
        bus_send_full_word(status_word);
        printf("TX_STATUS|WORD=0x%04X\n", status_word);

        if (send_data) {
            uint16_t data_word = mil1553_logic_build_data_word(RT_TEST_DATA_WORD);
            sleep_us(RT_INTERWORD_GAP_US);
            bus_send_full_word(data_word);
            printf("TX_DATA|WORD=0x%04X\n", data_word);
        }

        bus_set_rx_mode();
    }
}
