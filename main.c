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

        sleep_us(BIT_PERIOD_US / 2u);

        for(int i = 0; i < 8; i++){
            bool bit = false;

            if(bus_read_bit(&bit)){
                printf("RX_BIT[%d]: %d\n", i, bit ? 1u : 0u);
            }else{
                printf("RX_BIT[%d]: ERROR\n", i);
                break; 
            }
        }

        sleep_ms(200);
    }
}