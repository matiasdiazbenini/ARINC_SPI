#include <stdio.h>
#include <stdint.h>

#include "pico/stdlib.h"

#include "bus/bus.h"
#include "bus/mil1553_rx.h"


int main(void)
{
    stdio_init_all();

    sleep_ms(2000);

    mil1553_rx_init();

    printf("RX PIO - PALABRA COMPLETA + CHECK PARIDAD\n");


    uint32_t cmd_ok      = 0;
    uint32_t cmd_par_err = 0;

    uint32_t data_ok      = 0;
    uint32_t data_par_err = 0;


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


            uint32_t payload =
                event & MIL1553_RX_PAYLOAD_MASK;


            /*
             * bits 16..1 -> palabra
             */
            uint16_t word =
                (uint16_t)(
                    (payload >> MIL1553_RX_WORD_SHIFT)
                    & MIL1553_RX_WORD_MASK
                );


            /*
             * bit 0 -> paridad recibida
             */
            uint8_t received_parity =
                (uint8_t)(
                    payload
                    & MIL1553_RX_PARITY_MASK
                );


            /*
             * Paridad que debería tener la palabra
             * para cumplir paridad impar.
             */
            uint8_t expected_parity =
                bus_compute_odd_parity(word);


            /*
             * Comparación.
             */
            bool parity_ok =
                (received_parity ==
                 expected_parity);


            // ------------------------------------------------
            // CMD / STATUS
            // ------------------------------------------------

            if (type ==
                MIL1553_RX_TYPE_CMD_STATUS)
            {
                if (parity_ok)
                {
                    cmd_ok++;

                    printf(
                        "CMD  word=0x%04X "
                        "parity=%u OK\n",
                        word,
                        received_parity
                    );
                }
                else
                {
                    cmd_par_err++;

                    printf(
                        "CMD  word=0x%04X "
                        "parity=%u esperada=%u ERROR\n",
                        word,
                        received_parity,
                        expected_parity
                    );
                }
            }


            // ------------------------------------------------
            // DATA
            // ------------------------------------------------

            else if (type ==
                     MIL1553_RX_TYPE_DATA)
            {
                if (parity_ok)
                {
                    data_ok++;

                    printf(
                        "DATA word=0x%04X "
                        "parity=%u OK\n",
                        word,
                        received_parity
                    );
                }
                else
                {
                    data_par_err++;

                    printf(
                        "DATA word=0x%04X "
                        "parity=%u esperada=%u ERROR\n",
                        word,
                        received_parity,
                        expected_parity
                    );
                }
            }
        }


        /*
         * Resumen cada 5 segundos.
         */
        if (absolute_time_diff_us(
                get_absolute_time(),
                next_report) <= 0)
        {
            printf(
                "En 5 s: "
                "CMD[OK=%lu PAR_ERR=%lu] "
                "DATA[OK=%lu PAR_ERR=%lu]\n",

                (unsigned long)cmd_ok,
                (unsigned long)cmd_par_err,

                (unsigned long)data_ok,
                (unsigned long)data_par_err
            );


            cmd_ok      = 0;
            cmd_par_err = 0;

            data_ok      = 0;
            data_par_err = 0;


            next_report =
                make_timeout_time_ms(5000);
        }


        tight_loop_contents();
    }
}