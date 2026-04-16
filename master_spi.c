#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>

#include "bus/bus.h"
#include "pico/stdlib.h"

// Prueba minima de maestro:
// - con sync simple de laboratorio
// - con paridad impar en palabra completa
// - intercambio ida/vuelta.

#define MASTER_TEST_WORD            0xA5A5u
#define MASTER_PERIOD_MS            1000u
#define MASTER_TURNAROUND_GUARD_US  4u
#define MASTER_RESPONSE_TIMEOUT_MS  40u

int main(void) {
    stdio_init_all();
    sleep_ms(1200);

    printf("MASTER MIL-STD-1553 (logico) iniciado\n");
    printf("Word fija: 0x%04X (sync + 16 bits + paridad)\n", MASTER_TEST_WORD);

    bus_init();
    bus_set_rx_mode();

    while (true) {
        uint16_t response = 0;
        bool parity_ok = false;
        const uint16_t expected = (uint16_t)(MASTER_TEST_WORD ^ 0xFFFFu);

        // 1) TX: envia palabra completa (sync + 16 bits + paridad).
        bus_set_tx_mode();
        bus_send_full_word(MASTER_TEST_WORD);

        // 2) RX: libera el bus y espera respuesta del esclavo.
        bus_set_rx_mode();
        sleep_us(MASTER_TURNAROUND_GUARD_US);

        bool received = bus_receive_full_word(&response,
                                              &parity_ok,
                                              MASTER_RESPONSE_TIMEOUT_MS * 1000u);

        if (!received) {
            printf("TX: 0x%04X | RX: TIMEOUT/INVALID\n", MASTER_TEST_WORD);
        } else if (!parity_ok) {
            printf("TX: 0x%04X | RX: 0x%04X | PARITY ERROR\n",
                   MASTER_TEST_WORD,
                   response);
        } else {
            printf("TX: 0x%04X | RX: 0x%04X | ESP: 0x%04X | %s\n",
                   MASTER_TEST_WORD,
                   response,
                   expected,
                   (response == expected) ? "OK" : "MISMATCH");
        }

        sleep_ms(MASTER_PERIOD_MS);
    }
}
