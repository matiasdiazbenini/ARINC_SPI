#include <stdio.h>
#include <stdint.h>

#include "pico/stdlib.h"
#include "bus/mil1553_rx.h"

int main(void)
{
    stdio_init_all();

    sleep_ms(2000);

    mil1553_rx_init();

    printf("RX PIO iniciado\n");

    absolute_time_t next_alive =
        make_timeout_time_ms(1000);

    while (true)
    {
        if (mil1553_rx_available())
        {
            uint32_t event = mil1553_rx_get();

            if (event == 1u)
            {
                printf("SYNC CMD/STATUS detectado\n");
            }
            else if (event == 2u)
            {
                printf("SYNC DATA WORD detectado\n");
            }
            else
            {
                printf(
                    "Evento desconocido: %lu\n",
                    (unsigned long)event
                );
            }
        }

        if (absolute_time_diff_us(
                get_absolute_time(),
                next_alive) <= 0)
        {
            printf("RX vivo\n");

            next_alive =
                make_timeout_time_ms(1000);
        }

        tight_loop_contents();
    }
}