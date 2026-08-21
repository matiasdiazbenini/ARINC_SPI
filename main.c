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

    uint32_t cmd_count   = 0;
    uint32_t data_count  = 0;
    uint32_t other_count = 0;

    absolute_time_t next_report =
        make_timeout_time_ms(5000);

    while (true)
    {
        if (mil1553_rx_available())
        {
            uint32_t event = mil1553_rx_get();

            if (event == 1u)
            {
                cmd_count++;
            }
            else if (event == 2u)
            {
                data_count++;
            }
            else
            {
                other_count++;
            }
        }

        if (absolute_time_diff_us(
                get_absolute_time(),
                next_report) <= 0)
        {
            printf(
                "En 5 s: CMD=%lu DATA=%lu OTROS=%lu\n",
                (unsigned long)cmd_count,
                (unsigned long)data_count,
                (unsigned long)other_count
            );

            cmd_count   = 0;
            data_count  = 0;
            other_count = 0;

            next_report =
                make_timeout_time_ms(5000);
        }

        tight_loop_contents();
    }
}