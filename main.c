#include <stdio.h>
#include <stdint.h>

#include "pico/stdlib.h"
#include "bus/bus.h"

#define MY_RT_ADDR 3u

int main(void) {
    stdio_init_all();
    sleep_ms(1200);

    bus_init();
    bus_set_rx_mode();

    printf("RT SLAVE HIGH LEVEL API TEST\n");

    while (true) {
        rt_process_once(MY_RT_ADDR);
        sleep_ms(20);
    }
}