#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>

#include "bus/bus.h"
#include "mil1553_words.h"
#include "pico/stdlib.h"

#define MASTER_DEST_RT_ADDRESS      3u
#define MASTER_DEST_SUBADDRESS      1u
#define MASTER_DEST_WORD_COUNT      1u
#define MASTER_DEST_TR              false
#define MASTER_TX_DATA_WORD         0xA5A5u

#define MASTER_RX_TIMEOUT_US        200000u
#define MASTER_CYCLE_PERIOD_MS      500u
#define MASTER_RX_REARM_DELAY_US    2u
#define MASTER_CMD_TO_DATA_GAP_US   5000u

static void master_rearm_rx_after_tx(void) {
    // Libera el bus (alta impedancia) al terminar TX.
    bus_set_rx_mode();

    // Pequenio tiempo de asentamiento de GPIO antes de esperar sync.
    sleep_us(MASTER_RX_REARM_DELAY_US);

    // Reafirma RX para asegurar estado conocido antes de receive_full_word().
    bus_set_rx_mode();
}

int main(void) {
    stdio_init_all();
    sleep_ms(1200);

    printf("MASTER MIL test iniciado\n");

    bus_init();

    while (true) {
        const uint16_t command_word = mil1553_logic_build_command_word(
            MASTER_DEST_RT_ADDRESS,
            MASTER_DEST_TR,
            MASTER_DEST_SUBADDRESS,
            MASTER_DEST_WORD_COUNT);

        bus_set_tx_mode();
        bus_send_full_word(command_word);
        printf("TX_CMD|WORD=0x%04X|RT=%u|TR=%u|SA=%u|WC=%u\n",
               command_word,
               MASTER_DEST_RT_ADDRESS,
               MASTER_DEST_TR ? 1u : 0u,
               MASTER_DEST_SUBADDRESS,
               MASTER_DEST_WORD_COUNT);

        sleep_us(MASTER_CMD_TO_DATA_GAP_US);
        bus_send_full_word(MASTER_TX_DATA_WORD);
        printf("TX_DATA|WORD=0x%04X\n", MASTER_TX_DATA_WORD);

        // Transicion TX -> RX para esperar la Status Word del RT.
        master_rearm_rx_after_tx();

        uint16_t status_word = 0;
        bool status_parity_ok = false;

        if (!bus_receive_full_word(&status_word, &status_parity_ok, MASTER_RX_TIMEOUT_US)) {
            printf("RX_STATUS|TIMEOUT/INVALID\n");
            sleep_ms(MASTER_CYCLE_PERIOD_MS);
            continue;
        }

        if (!status_parity_ok) {
            printf("RX_STATUS|PARITY_ERROR|WORD=0x%04X\n", status_word);
            sleep_ms(MASTER_CYCLE_PERIOD_MS);
            continue;
        }

        mil1553_status_word_t status = {0};
        mil1553_logic_decode_status_word(status_word, &status);
        printf("RX_STATUS|WORD=0x%04X|PARITY=OK|RT=%u|ME=%u|SR=%u|BUSY=%u|TF=%u\n",
               status_word,
               status.rt_address,
               status.message_error ? 1u : 0u,
               status.service_request ? 1u : 0u,
               status.busy ? 1u : 0u,
               status.terminal_flag ? 1u : 0u);

        sleep_ms(MASTER_CYCLE_PERIOD_MS);
    }
}
