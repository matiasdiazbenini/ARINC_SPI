#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>

#include "bus/bus.h"
#include "pico/stdlib.h"

// Esta prueba es intencionalmente minima:
// - sin sync
// - sin paridad
// - solo intercambio de palabra de 16 bits.

#define RT_INTERFRAME_GUARD_US 4u
#define RT_RESPONSE_XOR_MASK   0xFFFFu

static bool rt_receive_word16(uint16_t *out_word) {
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

static void rt_send_word16(uint16_t word) {
    for (int bit_index = 15; bit_index >= 0; bit_index--) {
        int bit = (word >> bit_index) & 0x01;
        bus_send_bit(bit);
    }
}

static uint16_t rt_build_response(uint16_t request_word) {
    // Respuesta simple para validar ida/vuelta en banco.
    return (uint16_t)(request_word ^ RT_RESPONSE_XOR_MASK);
}

int main(void) {
    stdio_init_all();
    sleep_ms(1200);

    printf("MIL-STD-1553 RT test (logico) iniciado.\n");
    printf("Esperando palabra de 16 bits...\n");

    bus_init();
    bus_set_rx_mode();

    while (true) {
        uint16_t request_word = 0;
        if (!rt_receive_word16(&request_word)) {
            // Si hubo error de muestreo/transicion, reintenta.
            tight_loop_contents();
            continue;
        }

        uint16_t response_word = rt_build_response(request_word);
        printf("RX: 0x%04X | TX: 0x%04X\n", request_word, response_word);

        bus_set_tx_mode();
        sleep_us(RT_INTERFRAME_GUARD_US);
        rt_send_word16(response_word);
        bus_set_rx_mode();
    }
}
