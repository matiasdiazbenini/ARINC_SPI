#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>

#include "bus/bus.h"
#include "pico/stdlib.h"

#define MASTER_RX_TIMEOUT_US        200000u
#define MASTER_RX_FAIL_REPORT_EVERY 20u

int main(void) {
    stdio_init_all();
    sleep_ms(1200);

    printf("MASTER RX test pasivo iniciado\n");

    bus_init();
    bus_set_rx_mode();

    uint32_t rx_fail_count = 0;

    while (true) {
        uint16_t word = 0;
        bool parity_ok = false;

        if (!bus_receive_full_word(&word, &parity_ok, MASTER_RX_TIMEOUT_US)) {
            // Timeout de sync o recepcion incompleta.
            rx_fail_count++;
            if (rx_fail_count == 1 || (rx_fail_count % MASTER_RX_FAIL_REPORT_EVERY) == 0) {
                printf("RX_WAIT|TIMEOUT/INVALID|COUNT=%lu\n", (unsigned long)rx_fail_count);
            }
            tight_loop_contents();
            continue;
        }

        if (rx_fail_count > 0) {
            printf("RX_WAIT|RECOVERED|MISSED=%lu\n", (unsigned long)rx_fail_count);
            rx_fail_count = 0;
        }

        if (parity_ok) {
            printf("RX_OK|WORD=0x%04X|PARITY=OK\n", word);
        } else {
            printf("RX_ERR|WORD=0x%04X|PARITY=ERR\n", word);
        }
    }
}
