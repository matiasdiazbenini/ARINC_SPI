#include <stdio.h>
#include "pico/stdlib.h"
#include "hardware/gpio.h"
#include "bus/bus.h"

static bool has_valid_start(void) {
    int p = gpio_get(BUS_PIN_P);
    int n = gpio_get(BUS_PIN_N);

    return (p == 1 && n == 0) || (p == 0 && n == 1);
}
int main(void){
    stdio_init_all();
    sleep_ms(1200);

    bus_init();
    bus_set_tx_mode();

    while(true){
        while(!has_valid_start()) {
            sleep_ms(10);
        }

        sleep_us(BIT_PERIOD_US / 4u);

        uint8_t byte = 0;
        if(bus_read_byte(&byte)) {
            printf("Received byte: 0x%02X\n", byte);
        } else {
            printf("Failed to read byte\n");
        }
        sleep_ms(200);
    }
}