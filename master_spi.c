#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>

#include "bus/bus.h"
#include "pico/stdlib.h"

// Prueba minima de maestro:
// - sin sync
// - sin paridad
// - solo palabra de 16 bits ida/vuelta.

#define MASTER_TEST_WORD            0xA5A5u
#define MASTER_PERIOD_MS            1000u
#define MASTER_TURNAROUND_GUARD_US  4u
#define MASTER_RESPONSE_TIMEOUT_MS  40u

static void master_send_word16(uint16_t word) {
    for (int bit_index = 15; bit_index >= 0; bit_index--) {
        int bit = (word >> bit_index) & 0x01;
        bus_send_bit(bit);
    }
}

static bool master_receive_word16_once(uint16_t *out_word) {
    uint16_t word = 0;

    for (int i = 0; i < 16; i++) {
        int bit = bus_receive_bit();
        if (bit < 0) {
            return false;
        }
        word = (uint16_t)((word << 1) | (uint16_t)(bit & 0x01));
    }

    *out_word = word;
    return true;
}

static bool master_wait_response_word16(uint16_t *out_word, uint32_t timeout_ms) {
    uint32_t t0 = to_ms_since_boot(get_absolute_time());

    while ((to_ms_since_boot(get_absolute_time()) - t0) < timeout_ms) {
        if (master_receive_word16_once(out_word)) {
            return true;
        }

        // Reintento simple ante desalineacion temporal.
        tight_loop_contents();
    }

    return false;
}

int main(void) {
    stdio_init_all();
    sleep_ms(1200);

    printf("MASTER MIL-STD-1553 (logico) iniciado\n");
    printf("Word de prueba fija: 0x%04X\n", MASTER_TEST_WORD);

    bus_init();
    bus_set_rx_mode();

    while (true) {
        uint16_t response = 0;
        const uint16_t expected = (uint16_t)(MASTER_TEST_WORD ^ 0xFFFFu);

        // 1) TX: envia palabra de prueba.
        bus_set_tx_mode();
        master_send_word16(MASTER_TEST_WORD);

        // 2) RX: libera el bus y espera respuesta del esclavo.
        bus_set_rx_mode();
        sleep_us(MASTER_TURNAROUND_GUARD_US);

        bool ok = master_wait_response_word16(&response, MASTER_RESPONSE_TIMEOUT_MS);

        if (ok) {
            printf("TX: 0x%04X | RX: 0x%04X | ESP: 0x%04X | %s\n",
                   MASTER_TEST_WORD,
                   response,
                   expected,
                   (response == expected) ? "OK" : "MISMATCH");
        } else {
            printf("TX: 0x%04X | RX: TIMEOUT/INVALID\n", MASTER_TEST_WORD);
        }

        sleep_ms(MASTER_PERIOD_MS);
    }
}
