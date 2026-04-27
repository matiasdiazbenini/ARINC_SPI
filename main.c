#include <stdio.h>
#include "pico/stdlib.h"
#include "hardware/gpio.h"
#include "bus/bus.h"

static bool has_valid_start(void) {
    int p = gpio_get(BUS_PIN_P);
    int n = gpio_get(BUS_PIN_N);
    return (p == 1 && n == 0) || (p == 0 && n == 1);
}

int main(void) {
    stdio_init_all();
    sleep_ms(1200);

    bus_init();
    bus_set_rx_mode();

    while (true) {
        while (!has_valid_start()) {
            sleep_ms(100);
        }

        printf("START DETECTED\n");

        sleep_us((BIT_PERIOD_US * 3u) / 8u);

        uint8_t sync_type = 0;

        if(bus_read_sync(&sync_type)){
            printf("SYNC_OK | TYPE: %u\n", sync_type);
        }else{
            printf("SYNC_ERROR\n");
        }

        sleep_ms(200);
    }
}