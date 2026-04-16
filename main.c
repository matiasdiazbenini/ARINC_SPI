#include <stdio.h>

#include "bus/bus.h"
#include "hardware/gpio.h"
#include "pico/stdlib.h"

#define RAW_PRINT_PERIOD_MS 100u

int main(void) {
    stdio_init_all();
    sleep_ms(1200);

    printf("RAW BUS monitor iniciado\n");
    printf("Leyendo niveles instantaneos de BUS_P/BUS_N cada %u ms\n", RAW_PRINT_PERIOD_MS);

    bus_init();
    bus_set_rx_mode();

    while (true) {
        int p = gpio_get(BUS_PIN_P) ? 1 : 0;
        int n = gpio_get(BUS_PIN_N) ? 1 : 0;

        printf("BUS_RAW|P=%d|N=%d\n", p, n);
        sleep_ms(RAW_PRINT_PERIOD_MS);
    }
}
