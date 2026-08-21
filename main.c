#include <stdio.h>
#include <stdint.h>

#include "pico/stdlib.h"
#include "bus/mil1553_rx.h"


int main(void)
{
    stdio_init_all();

    sleep_ms(2000);

    mil1553_rx_init();

    printf("RX PIO - PRIMER BIT MANCHESTER\n");


    uint32_t cmd_bit0  = 0;
    uint32_t cmd_bit1  = 0;

    uint32_t data_bit0 = 0;
    uint32_t data_bit1 = 0;

    uint32_t other = 0;


    absolute_time_t next_report =
        make_timeout_time_ms(5000);


    while (true)
    {
        if (mil1553_rx_available())
        {
            uint32_t event =
                mil1553_rx_get();


            if (event ==
                MIL1553_RX_EVENT_CMD_BIT0)
            {
                cmd_bit0++;
            }
            else if (event ==
                     MIL1553_RX_EVENT_CMD_BIT1)
            {
                cmd_bit1++;
            }
            else if (event ==
                     MIL1553_RX_EVENT_DATA_BIT0)
            {
                data_bit0++;
            }
            else if (event ==
                     MIL1553_RX_EVENT_DATA_BIT1)
            {
                data_bit1++;
            }
            else
            {
                other++;
            }
        }


        if (absolute_time_diff_us(
                get_absolute_time(),
                next_report) <= 0)
        {
            printf(
                "CMD[b0=%lu b1=%lu] "
                "DATA[b0=%lu b1=%lu] "
                "OTROS=%lu\n",

                (unsigned long)cmd_bit0,
                (unsigned long)cmd_bit1,

                (unsigned long)data_bit0,
                (unsigned long)data_bit1,

                (unsigned long)other
            );


            cmd_bit0  = 0;
            cmd_bit1  = 0;

            data_bit0 = 0;
            data_bit1 = 0;

            other = 0;


            next_report =
                make_timeout_time_ms(5000);
        }


        tight_loop_contents();
    }
}