#include <stdio.h>
#include <stdint.h>

#include "pico/stdlib.h"
#include "bus/bus.h"

int main(void) {
    stdio_init_all();
    sleep_ms(1200);

    bus_init();
    bus_set_rx_mode();

    printf("MIL-STD-1553-LIKE SNIFFER | UNIFIED EVENT TEST\n");

    while (true) {
        sniffer_event_t event = {0};

        if (sniffer_capture_event_data(&event)) {
            sniffer_print_event(&event);
            sleep_ms(5);
            continue;
        }

        sleep_ms(5);
    }
}