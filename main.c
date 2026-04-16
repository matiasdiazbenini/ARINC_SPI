#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>

#include "bus/bus.h"
#include "pico/stdlib.h"

// Esta prueba es intencionalmente minima:
// - con sync simple de laboratorio
// - con paridad impar
// - intercambio de palabra completa de prueba.

#define RT_INTERFRAME_GUARD_US 4u
#define RT_RESPONSE_XOR_MASK   0xFFFFu
#define RT_SYNC_TIMEOUT_US     30000u

static uint16_t rt_build_response(uint16_t request_word) {
    // Respuesta simple para validar ida/vuelta en banco.
    return (uint16_t)(request_word ^ RT_RESPONSE_XOR_MASK);
}

int main(void) {
    stdio_init_all();
    sleep_ms(1200);

    printf("MIL-STD-1553 RT test (logico) iniciado.\n");
    printf("Esperando palabra completa (sync + 16 bits + paridad)...\n");

    bus_init();
    bus_set_rx_mode();

    while (true) {
        uint16_t request_word = 0;
        bool parity_ok = false;
        if (!bus_receive_full_word(&request_word, &parity_ok, RT_SYNC_TIMEOUT_US)) {
            // Timeout de sync o error de recepcion de bits.
            tight_loop_contents();
            continue;
        }
        if (!parity_ok) {
            printf("RX_ERR_PARITY|WORD=0x%04X\n", request_word);
            continue;
        }

        uint16_t response_word = rt_build_response(request_word);
        printf("RX: 0x%04X | TX: 0x%04X\n", request_word, response_word);

        bus_set_tx_mode();
        sleep_us(RT_INTERFRAME_GUARD_US);
        bus_send_full_word(response_word);
        bus_set_rx_mode();
    }
}
