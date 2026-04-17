#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>

#include "bus/bus.h"
#include "mil1553_words.h"
#include "pico/stdlib.h"

// Flujo minimo MIL-STD-1553:
// Command Word -> Status Word -> Data Word
// sobre bus logico (sync laboratorio + paridad impar).

#define MASTER_RT_DEST              3u
#define MASTER_TR                   true
#define MASTER_SUBADDRESS           1u
#define MASTER_WORD_COUNT           1u

#define MASTER_PERIOD_MS            1000u
#define MASTER_TURNAROUND_GUARD_US  50u
#define MASTER_RX_TIMEOUT_US        40000u

typedef enum {
    RX_WORD_OK,
    RX_WORD_TIMEOUT_INVALID,
    RX_WORD_PARITY_ERROR
} rx_word_result_t;

static rx_word_result_t master_receive_checked_word(uint16_t *out_word) {
    bool parity_ok = false;
    bool received = bus_receive_full_word(out_word, &parity_ok, MASTER_RX_TIMEOUT_US);

    if (!received) {
        return RX_WORD_TIMEOUT_INVALID;
    }

    if (!parity_ok) {
        return RX_WORD_PARITY_ERROR;
    }

    return RX_WORD_OK;
}

int main(void) {
    stdio_init_all();
    sleep_ms(1200);

    printf("MASTER MIL-STD-1553 (logico) Command->Status->Data\n");

    bus_init();
    bus_set_rx_mode();

    while (true) {
        uint16_t status_word_raw = 0;
        uint16_t data_word_raw = 0;
        mil1553_status_word_t status;

        const uint16_t command_word = mil1553_logic_build_command_word(
            MASTER_RT_DEST,
            MASTER_TR,
            MASTER_SUBADDRESS,
            MASTER_WORD_COUNT
        );

        // 1) TX: envia Command Word al RT.
        bus_set_tx_mode();
        bus_send_full_word(command_word);
        printf("TX_CMD|WORD=0x%04X|RT=%u|TR=%u|SA=%u|WC=%u\n",
               command_word,
               MASTER_RT_DEST,
               MASTER_TR ? 1 : 0,
               MASTER_SUBADDRESS,
               MASTER_WORD_COUNT);

        // 2) RX: espera Status Word.
        bus_set_rx_mode();
        sleep_us(MASTER_TURNAROUND_GUARD_US);

        rx_word_result_t status_rx = master_receive_checked_word(&status_word_raw);
        if (status_rx == RX_WORD_TIMEOUT_INVALID) {
            printf("RX_STATUS|TIMEOUT/INVALID\n");
            sleep_ms(MASTER_PERIOD_MS);
            continue;
        }
        if (status_rx == RX_WORD_PARITY_ERROR) {
            printf("RX_STATUS|PARITY ERROR|WORD=0x%04X\n", status_word_raw);
            sleep_ms(MASTER_PERIOD_MS);
            continue;
        }

        mil1553_logic_decode_status_word(status_word_raw, &status);
        printf("RX_STATUS|WORD=0x%04X|RT=%u|ME=%u|SR=%u|BCR=%u|BUSY=%u|SF=%u|DBCA=%u|TF=%u\n",
               status_word_raw,
               status.rt_address,
               status.message_error ? 1 : 0,
               status.service_request ? 1 : 0,
               status.broadcast_command_received ? 1 : 0,
               status.busy ? 1 : 0,
               status.subsystem_flag ? 1 : 0,
               status.dynamic_bus_control_acceptance ? 1 : 0,
               status.terminal_flag ? 1 : 0);

        // 3) RX: espera Data Word.
        rx_word_result_t data_rx = master_receive_checked_word(&data_word_raw);
        if (data_rx == RX_WORD_TIMEOUT_INVALID) {
            printf("RX_DATA|TIMEOUT/INVALID\n");
            sleep_ms(MASTER_PERIOD_MS);
            continue;
        }
        if (data_rx == RX_WORD_PARITY_ERROR) {
            printf("RX_DATA|PARITY ERROR|WORD=0x%04X\n", data_word_raw);
            sleep_ms(MASTER_PERIOD_MS);
            continue;
        }

        // Data Word es payload de 16 bits.
        printf("RX_DATA|WORD=0x%04X|DATA=0x%04X\n", data_word_raw, data_word_raw);

        sleep_ms(MASTER_PERIOD_MS);
    }
}
