#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>

#include "bus/bus.h"
#include "mil1553_words.h"
#include "pico/stdlib.h"

#define RT_ADDRESS                  3u
#define RT_SUBADDRESS_EXPECTED      1u
#define RT_WORD_COUNT_EXPECTED      1u
#define RT_DATA_WORD_TEST           0xA5A5u

#define RT_RX_TIMEOUT_US            200000u
#define RT_TIMEOUT_REPORT_EVERY     20u
#define RT_RX_REARM_DELAY_US        2u
#define RT_RESPONSE_DELAY_US        500u
#define RT_INTERWORD_GAP_US         200u
#define RT_POST_TR0_GUARD_US        5000u

static bool rt_is_supported_command(const mil1553_command_word_t *cmd) {
    if (!cmd) {
        return false;
    }

    const bool rt_match = (cmd->rt_address == RT_ADDRESS);
    const bool sa_match = (cmd->subaddress == RT_SUBADDRESS_EXPECTED);
    const bool wc_match = (cmd->word_count == RT_WORD_COUNT_EXPECTED);

    // Este firmware soporta ambos casos de T/R:
    // TR=1 (RT->BC) y TR=0 (BC->RT).
    const bool tr_supported = true;

    return rt_match && sa_match && wc_match && tr_supported;
}

static uint8_t rt_effective_word_count(uint8_t word_count) {
    // MIL-STD-1553: en palabras de comando normales, WC=0 representa 32 palabras.
    return (word_count == 0u) ? 32u : word_count;
}

static void rt_post_tr0_rearm_to_wait_cmd(void) {
    // Vuelve a alta impedancia y deja pasar residuos inmediatos del bus
    // antes de volver a interpretar nuevas Command Words.
    bus_set_rx_mode();
    sleep_us(RT_POST_TR0_GUARD_US);
    bus_set_rx_mode();
}

static void rt_rearm_rx_before_data_window(void) {
    // Rearmado corto antes de esperar DATA en TR=0.
    bus_set_rx_mode();
    sleep_us(RT_RX_REARM_DELAY_US);
    bus_set_rx_mode();
}
//
int main(void) {
    stdio_init_all();
    sleep_ms(1200);

    printf("RT MIL test iniciado\n");

    bus_init();
    bus_set_rx_mode();

    uint32_t rx_timeout_count = 0;

    while (true) {
        uint16_t rx_word = 0;
        bool parity_ok = false;

        if (!bus_receive_full_word(&rx_word, &parity_ok, RT_RX_TIMEOUT_US)) {
            rx_timeout_count++;
            if (rx_timeout_count == 1u || (rx_timeout_count % RT_TIMEOUT_REPORT_EVERY) == 0u) {
                printf("RX_WAIT|TIMEOUT_INVALID|COUNT=%lu\n", (unsigned long)rx_timeout_count);
            }
            tight_loop_contents();
            continue;
        }

        rx_timeout_count = 0;

        if (!parity_ok) {
            printf("RX_ERR|WORD=0x%04X|PARITY=ERR\n", rx_word);
            continue;
        }

        printf("RX_OK|WORD=0x%04X|PARITY=OK\n", rx_word);

        mil1553_command_word_t cmd = {0};
        mil1553_logic_decode_command_word(rx_word, &cmd);

        if (!rt_is_supported_command(&cmd)) {
                    printf("CMD_INVALID|WORD=0x%04X|RT=%u|TR=%u|SA=%u|WC=%u\n",
                        rx_word,
                        cmd.rt_address,
                        cmd.transmit ? 1u : 0u,
                        cmd.subaddress,
                        cmd.word_count);
                    continue;
                }

        printf("CMD_DEC|RT=%u|TR=%u|SA=%u|WC=%u\n",
               cmd.rt_address,
               cmd.transmit ? 1u : 0u,
               cmd.subaddress,
               cmd.word_count);

        mil1553_status_word_t status = {
            .rt_address = RT_ADDRESS,
            .message_error = false,
            .service_request = false,
            .broadcast_command_received = false,
            .busy = false,
            .subsystem_flag = false,
            .dynamic_bus_control_acceptance = false,
            .terminal_flag = false,
        };

        if (cmd.transmit) {
            // Flujo RT -> BC: CMD -> STS -> DATA.
            const uint16_t status_word = mil1553_logic_build_status_word(&status);

            sleep_us(RT_RESPONSE_DELAY_US);
            bus_set_tx_mode();
            bus_send_full_word(status_word);
            printf("TX_STATUS|WORD=0x%04X\n", status_word);

            if (cmd.subaddress == RT_SUBADDRESS_EXPECTED &&
                cmd.word_count == RT_WORD_COUNT_EXPECTED) {
                const uint16_t data_word = mil1553_logic_build_data_word(RT_DATA_WORD_TEST);

                sleep_us(RT_INTERWORD_GAP_US);
                bus_send_full_word(data_word);
                printf("TX_DATA|WORD=0x%04X\n", data_word);
            }

            bus_set_rx_mode();
            continue;
        }

        // Flujo BC -> RT: CMD -> DATA(s) -> STS.
        rt_rearm_rx_before_data_window();

        const uint8_t data_words_expected = rt_effective_word_count(cmd.word_count);
        for (uint8_t i = 0; i < data_words_expected; i++) {
            uint16_t data_word = 0;
            bool data_parity_ok = false;

            if (!bus_receive_full_word(&data_word, &data_parity_ok, RT_RX_TIMEOUT_US)) {
                status.message_error = true;
                printf("RX_DATA|INDEX=%u|TIMEOUT_INVALID\n", (unsigned)(i + 1u));
                break;
            }

            if (!data_parity_ok) {
                status.message_error = true;
                printf("RX_DATA|INDEX=%u|WORD=0x%04X|PARITY=ERR\n",
                       (unsigned)(i + 1u),
                       data_word);
                break;
            }

            printf("RX_DATA|INDEX=%u|WORD=0x%04X|PARITY=OK\n",
                   (unsigned)(i + 1u),
                   data_word);
        }

        const uint16_t status_word = mil1553_logic_build_status_word(&status);
        sleep_us(RT_RESPONSE_DELAY_US);
        bus_set_tx_mode();
        bus_send_full_word(status_word);
        printf("TX_STATUS|WORD=0x%04X\n", status_word);
        rt_post_tr0_rearm_to_wait_cmd();
    }
}
