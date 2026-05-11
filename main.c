#include <stdio.h>

#include "pico/stdlib.h"
#include "bus/bus.h"

#define SNIFFER_DEBUG_RESYNC 0

int main(void) {
    stdio_init_all();
    sleep_ms(1200);

    bus_init();

    printf("MIL SNIFFER READY\n");

    uint32_t last_event_ms = to_ms_since_boot(get_absolute_time());

    while (true) {
        sniffer_event_t event;

        if (sniffer_capture_event_data(&event)) {
            sniffer_print_event(&event);
            last_event_ms = to_ms_since_boot(get_absolute_time());
        } else {
            uint32_t now_ms = to_ms_since_boot(get_absolute_time());

            /*
             * Resync automático si el sniffer queda fuera de fase.
             * Lo dejamos en 5 s para no resetear demasiado seguido.
             */
            if ((now_ms - last_event_ms) > 5000u) {
                sniffer_resync();
                last_event_ms = now_ms;

#if SNIFFER_DEBUG_RESYNC
                printf("SNIFF_RESYNC\n");
#endif
            }
        }

        sleep_ms(5);
    }
}