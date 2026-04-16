#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>

#include "bus/bus.h"
#include "pico/stdlib.h"

// Sniffer pasivo de banco:
// escucha palabra completa (sync + 16 bits + paridad)
// y emite una linea simple por USB serial.

#define SNIFFER_SYNC_TIMEOUT_US 30000u

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
            printf("RX_OK|WORD=0x%04X|PARITY=OK\r\n", word);
        } else {
            printf("RX_ERR|WORD=0x%04X|PARITY=ERR\r\n", word);
        }

        fflush(stdout);
    }
}
