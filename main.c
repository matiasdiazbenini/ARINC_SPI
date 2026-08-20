#include <stdio.h>
#include <stdint.h>

#include "pico/stdlib.h"

#include "bus/mil1553_rx.h"


int main(void)
{
    stdio_init_all();

    sleep_ms(1500);

    printf("MIL-STD-1553 PIO RX TEST\n");

    mil1553_rx_init();

    printf("RX PIO iniciado\n");

    while (true)
    {
        if (mil1553_rx_available())
        {
            uint32_t event = mil1553_rx_get();

            if (event == 1u)
            {
                printf("SYNC CMD/STATUS detectado\n");
            }
            else
            {
                printf(
                    "Evento RX desconocido: 0x%08lX\n",
                    (unsigned long)event
                );
            }
        }

        tight_loop_contents();
    }
}