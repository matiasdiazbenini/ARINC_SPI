#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>

#include "bus/bus.h"
#include "pico/stdlib.h"

// Sniffer pasivo de banco:
// escucha palabra completa (sync + 16 bits + paridad)
// y emite una linea simple por USB serial.

#define SNIFFER_SYNC_TIMEOUT_US 30000u

typedef enum {
    SEQ_EXPECT_CMD = 0,
    SEQ_EXPECT_STS = 1,
    SEQ_EXPECT_DATA = 2
} sniffer_seq_state_t;

static const char *type_from_state(sniffer_seq_state_t state) {
    switch (state) {
        case SEQ_EXPECT_CMD:
            return "CMD";
        case SEQ_EXPECT_STS:
            return "STS";
        case SEQ_EXPECT_DATA:
            return "DATA";
        default:
            return "UNKNOWN";
    }
}

int main(void) {
    stdio_init_all();
    sleep_ms(1200);

    bus_init();
    bus_set_rx_mode();
    sniffer_seq_state_t seq_state = SEQ_EXPECT_CMD;

    while (true) {
        uint16_t word = 0;
        bool parity_ok = false;

        // Si hay timeout o recepcion incompleta, reintenta sin loguear ruido.
        if (!bus_receive_full_word(&word, &parity_ok, SNIFFER_SYNC_TIMEOUT_US)) {
            tight_loop_contents();
            continue;
        }

        if (parity_ok) {
            const char *type = type_from_state(seq_state);
            printf("RX_OK|TYPE=%s|WORD=0x%04X|PARITY=OK\r\n", type, word);

            if (seq_state == SEQ_EXPECT_DATA) {
                seq_state = SEQ_EXPECT_CMD;
            } else {
                seq_state = (sniffer_seq_state_t)(seq_state + 1);
            }
        } else {
            printf("RX_ERR|TYPE=UNKNOWN|WORD=0x%04X|PARITY=ERR\r\n", word);
            seq_state = SEQ_EXPECT_CMD;
        }

        fflush(stdout);
    }
}
