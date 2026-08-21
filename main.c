#include <stdio.h>
#include <stdint.h>

#include "pico/stdlib.h"
#include "bus/mil1553_rx.h"


int main(void)
{
    stdio_init_all();

    sleep_ms(2000);

    mil1553_rx_init();

    printf(
        "RX PIO - 16 BITS MANCHESTER\n"
    );


    uint32_t cmd_ok   = 0;
    uint32_t cmd_bad  = 0;

    uint32_t data_ok  = 0;
    uint32_t data_bad = 0;


    absolute_time_t next_report =
        make_timeout_time_ms(5000);


    while (true)
    {
        if (mil1553_rx_available())
        {
            uint32_t event =
                mil1553_rx_get();


            uint32_t type =
                event & MIL1553_RX_TYPE_MASK;


            uint16_t word =
                (uint16_t)(
                    event & MIL1553_RX_WORD_MASK
                );


            if (type ==
                MIL1553_RX_TYPE_CMD_STATUS)
            {
                if (word == 0x1CE3u)
                {
                    cmd_ok++;
                }
                else
                {
                    cmd_bad++;

                    printf(
                        "CMD incorrecto: 0x%04X\n",
                        word
                    );
                }
            }
            else if (type ==
                     MIL1553_RX_TYPE_DATA)
            {
                if (word == 0x1CE3u)
                {
                    data_ok++;
                }
                else
                {
                    data_bad++;

                    printf(
                        "DATA incorrecto: 0x%04X\n",
                        word
                    );
                }
            }
        }


        if (absolute_time_diff_us(
                get_absolute_time(),
                next_report) <= 0)
        {
            printf(
                "CMD[OK=%lu ERR=%lu] "
                "DATA[OK=%lu ERR=%lu]\n",

                (unsigned long)cmd_ok,
                (unsigned long)cmd_bad,

                (unsigned long)data_ok,
                (unsigned long)data_bad
            );


            cmd_ok   = 0;
            cmd_bad  = 0;

            data_ok  = 0;
            data_bad = 0;


            next_report =
                make_timeout_time_ms(5000);
        }


        tight_loop_contents();
    }
}