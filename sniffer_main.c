#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>

#include "bus/bus.h"
#include "mil1553_words.h"
#include "pico/stdlib.h"

// Sniffer pasivo de banco:
// escucha palabra completa (sync + 16 bits + paridad)
// y emite una linea simple por USB serial

#define SNIFFER_SYNC_TIMEOUT_US 30000u
#define SNIFFER_RT_ADDRESS      3u
#define SNIFFER_SUBADDRESS      1u
#define SNIFFER_WORD_COUNT      1u

static bool is_valid_command_word(uint16_t word) {
    mil1553_command_word_t cmd = {0};
    mil1553_logic_decode_command_word(word, &cmd);

    // Perfil de banco actual: comando al RT=3, SA=1, WC=1.
    return (cmd.rt_address == SNIFFER_RT_ADDRESS) &&
           (cmd.subaddress == SNIFFER_SUBADDRESS) &&
           (cmd.word_count == SNIFFER_WORD_COUNT);
}

static bool is_valid_status_word(uint16_t word) {
    mil1553_status_word_t status = {0};
    mil1553_logic_decode_status_word(word, &status);

    // En esta etapa, los bits reservados [9], [7], [6] y [0]
    // deben permanecer en cero para considerar la palabra como status.
    const bool reserved_bits_ok =
        ((word & (1u << 9)) == 0u) &&
        ((word & (1u << 7)) == 0u) &&
        ((word & (1u << 6)) == 0u) &&
        ((word & (1u << 0)) == 0u);

    return (status.rt_address == SNIFFER_RT_ADDRESS) && reserved_bits_ok;
}

static const char *classify_word_type(uint16_t word) {
    if (is_valid_command_word(word)) {
        return "CMD";
    }
    if (is_valid_status_word(word)) {
        return "STS";
    }
    return "DATA";
}

int main(void) {
    stdio_init_all();
    sleep_ms(1200);

    bus_init();
    bus_set_rx_mode();

    while (true) {
        uint16_t word = 0;
        bool parity_ok = false;

        // Si hay timeout o recepcion incompleta, reintenta sin loguear ruido.
        if (!bus_receive_full_word(&word, &parity_ok, SNIFFER_SYNC_TIMEOUT_US)) {
            tight_loop_contents();
            continue;
        }

        if (parity_ok) {
            const char *type = classify_word_type(word);
            printf("RX_OK|TYPE=%s|WORD=0x%04X|PARITY=OK\r\n", type, word);
        } else {
            printf("RX_ERR|TYPE=UNKNOWN|WORD=0x%04X|PARITY=ERR\r\n", word);
        }

        fflush(stdout);
    }
}
