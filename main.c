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

typedef enum {
    RT_STATE_WAIT_CMD = 0,
    RT_STATE_WAIT_DATA,
    RT_STATE_SEND_STATUS,
    RT_STATE_SEND_DATA,
    RT_STATE_GUARD
} rt_state_t;

typedef struct {
    rt_state_t state;
    mil1553_command_word_t cmd;
    mil1553_status_word_t status;
    uint8_t data_words_expected;
    uint8_t data_words_received;
} rt_fsm_t;

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

static mil1553_status_word_t rt_build_default_status(void) {
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
    return status;
}

int main(void) {
    stdio_init_all();
    sleep_ms(1200);

    printf("RT MIL test iniciado\n");

    bus_init();
    bus_set_rx_mode();

    uint32_t rx_timeout_count = 0;
    rt_fsm_t fsm = {
        .state = RT_STATE_WAIT_CMD,
        .cmd = {0},
        .status = {0},
        .data_words_expected = 0u,
        .data_words_received = 0u,
    };

    while (true) {
        switch (fsm.state) {
            case RT_STATE_WAIT_CMD: {
                uint16_t rx_word = 0;
                bool parity_ok = false;

                if (!bus_receive_full_word(&rx_word, &parity_ok, RT_RX_TIMEOUT_US)) {
                    rx_timeout_count++;
                    if (rx_timeout_count == 1u ||
                        (rx_timeout_count % RT_TIMEOUT_REPORT_EVERY) == 0u) {
                        printf("RX_WAIT|TIMEOUT_INVALID|COUNT=%lu\n",
                               (unsigned long)rx_timeout_count);
                    }
                    tight_loop_contents();
                    break;
                }

                rx_timeout_count = 0;

                if (!parity_ok) {
                    printf("RX_ERR|WORD=0x%04X|PARITY=ERR\n", rx_word);
                    break;
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
                    break;
                }

                printf("CMD_DEC|RT=%u|TR=%u|SA=%u|WC=%u\n",
                       cmd.rt_address,
                       cmd.transmit ? 1u : 0u,
                       cmd.subaddress,
                       cmd.word_count);

                fsm.cmd = cmd;
                fsm.status = rt_build_default_status();
                fsm.data_words_expected = 0u;
                fsm.data_words_received = 0u;

                if (cmd.transmit) {
                    // TR=1: CMD -> STS -> DATA.
                    fsm.state = RT_STATE_SEND_STATUS;
                } else {
                    // TR=0: CMD -> DATA -> STS.
                    rt_rearm_rx_before_data_window();
                    fsm.data_words_expected = rt_effective_word_count(cmd.word_count);
                    fsm.state = RT_STATE_WAIT_DATA;
                }
                break;
            }

            case RT_STATE_WAIT_DATA: {
                if (fsm.data_words_received >= fsm.data_words_expected) {
                    fsm.state = RT_STATE_SEND_STATUS;
                    break;
                }

                uint16_t data_word = 0;
                bool data_parity_ok = false;
                const unsigned index = (unsigned)fsm.data_words_received + 1u;

                if (!bus_receive_full_word(&data_word, &data_parity_ok, RT_RX_TIMEOUT_US)) {
                    fsm.status.message_error = true;
                    printf("RX_DATA|INDEX=%u|TIMEOUT_INVALID\n", index);
                    fsm.state = RT_STATE_SEND_STATUS;
                    break;
                }

                if (!data_parity_ok) {
                    fsm.status.message_error = true;
                    printf("RX_DATA|INDEX=%u|WORD=0x%04X|PARITY=ERR\n", index, data_word);
                    fsm.state = RT_STATE_SEND_STATUS;
                    break;
                }

                printf("RX_DATA|INDEX=%u|WORD=0x%04X|PARITY=OK\n", index, data_word);
                fsm.data_words_received++;

                if (fsm.data_words_received >= fsm.data_words_expected) {
                    fsm.state = RT_STATE_SEND_STATUS;
                }
                break;
            }

            case RT_STATE_SEND_STATUS: {
                const uint16_t status_word = mil1553_logic_build_status_word(&fsm.status);

                sleep_us(RT_RESPONSE_DELAY_US);
                bus_set_tx_mode();
                bus_send_full_word(status_word);
                printf("TX_STATUS|WORD=0x%04X\n", status_word);

                fsm.state = fsm.cmd.transmit ? RT_STATE_SEND_DATA : RT_STATE_GUARD;
                break;
            }

            case RT_STATE_SEND_DATA: {
                // Solo valido para TR=1 en este banco.
                const uint16_t data_word = mil1553_logic_build_data_word(RT_DATA_WORD_TEST);

                sleep_us(RT_INTERWORD_GAP_US);
                bus_send_full_word(data_word);
                printf("TX_DATA|WORD=0x%04X\n", data_word);

                bus_set_rx_mode();
                fsm.state = RT_STATE_WAIT_CMD;
                break;
            }

            case RT_STATE_GUARD: {
                // Cierre de TR=0 antes de volver a aceptar comandos.
                rt_post_tr0_rearm_to_wait_cmd();
                fsm.state = RT_STATE_WAIT_CMD;
                break;
            }

            default:
                bus_set_rx_mode();
                fsm.state = RT_STATE_WAIT_CMD;
                break;
        }
    }
}
