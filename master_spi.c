#include <stdio.h>

#include "bus/bus.h"
#include "hardware/gpio.h"
#include "pico/stdlib.h"

#define RAW_TX_PERIOD_MS 1000u

int main(void) {
    stdio_init_all();
    sleep_ms(1200);

    printf("MASTER RAW BUS test iniciado\n");
    printf("Alternando estados diferenciales cada %u ms\n", RAW_TX_PERIOD_MS);

    bus_init();
    bus_set_tx_mode();

    while (true) {
        gpio_put(BUS_PIN_P, 1);
        gpio_put(BUS_PIN_N, 0);
        printf("TX_RAW|P=1|N=0\n");
        sleep_ms(RAW_TX_PERIOD_MS);

        gpio_put(BUS_PIN_P, 0);
        gpio_put(BUS_PIN_N, 1);
        printf("TX_RAW|P=0|N=1\n");
        sleep_ms(RAW_TX_PERIOD_MS);
    }
}
