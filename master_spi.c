#include <stdio.h>

#include "pico/stdlib.h"
#include "bus/bus.h"

int main(void){
    stdio_init_all();
    sleep_ms(1200);

    bus_init();
    bus_set_tx_mode();

    while (true){
        bus_idle();
        sleep_ms(1000);

        printf("PROBANDO\n");
        
        bus_set_tx_mode();
        bus_send_sync_cmd_status();

        bus_idle();
        sleep_ms(2000);
    }
}