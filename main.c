#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>

#include "pico/stdlib.h"

#include "bus/bus.h"
#include "bus/mil1553_rx.h"


#define MY_RT_ADDR 3u


int main(void)
{
    stdio_init_all();

    sleep_ms(2000);

    mil1553_rx_init();

    printf("MIL-STD-1553 RT - COMMAND + DATA RX\n");
    printf("Direccion RT local: %u\n\n", MY_RT_ADDR);


    /*
     * Contadores del periodo de 5 segundos.
     */
    uint32_t cmd_for_me     = 0;
    uint32_t cmd_other_rt   = 0;
    uint32_t cmd_parity_err = 0;

    uint32_t data_ok        = 0;
    uint32_t data_par_err   = 0;


    /*
     * Contadores acumulativos.
     *
     * Estos NO se reinician cada 5 segundos.
     */
    uint32_t total_1111 = 0;
    uint32_t total_2222 = 0;
    uint32_t total_3333 = 0;

    uint32_t total_data_other = 0;


    absolute_time_t next_report =
        make_timeout_time_ms(5000);


    while (true)
    {
        if (mil1553_rx_available())
        {
            uint32_t event =
                mil1553_rx_get();


            /*
             * Tipo de palabra.
             */
            uint32_t type =
                event & MIL1553_RX_TYPE_MASK;


            /*
             * Payload:
             *
             * bits 16..1 -> palabra
             * bit 0      -> paridad
             */
            uint32_t payload =
                event & MIL1553_RX_PAYLOAD_MASK;


            uint16_t word =
                (uint16_t)(
                    (payload >> MIL1553_RX_WORD_SHIFT)
                    & MIL1553_RX_WORD_MASK
                );


            uint8_t received_parity =
                (uint8_t)(
                    payload
                    & MIL1553_RX_PARITY_MASK
                );


            uint8_t expected_parity =
                bus_compute_odd_parity(word);


            bool parity_ok =
                (received_parity ==
                 expected_parity);


            // ====================================================
            // COMMAND / STATUS
            // ====================================================

            if (type ==
                MIL1553_RX_TYPE_CMD_STATUS)
            {
                if (!parity_ok)
                {
                    cmd_parity_err++;

                    printf(
                        "CMD ERROR PARIDAD: "
                        "word=0x%04X "
                        "parity=%u esperada=%u\n\n",
                        word,
                        received_parity,
                        expected_parity
                    );
                }
                else
                {
                    /*
                     * Decodificar Command Word.
                     */
                    uint8_t rt =
                        BUS_1553_CMD_RT(word);

                    uint8_t tr =
                        BUS_1553_CMD_TR(word);

                    uint8_t sub =
                        BUS_1553_CMD_SUB(word);

                    uint8_t wc =
                        BUS_1553_CMD_WC(word);


                    printf(
                        "CMD recibido: 0x%04X PARITY=OK\n",
                        word
                    );

                    printf(
                        "RT=%u TR=%u SUB=%u WC=%u\n",
                        rt,
                        tr,
                        sub,
                        wc
                    );


                    /*
                     * Verificar direccionamiento.
                     */
                    if (rt == MY_RT_ADDR)
                    {
                        cmd_for_me++;

                        printf(
                            "CMD dirigido a este RT\n"
                        );


                        if (tr ==
                            BUS_1553_TR_RT_TO_BC)
                        {
                            printf(
                                "Operacion: RT -> BC "
                                "(Transmit Command)\n"
                            );
                        }
                        else
                        {
                            printf(
                                "Operacion: BC -> RT "
                                "(Receive Command)\n"
                            );
                        }
                    }
                    else
                    {
                        cmd_other_rt++;

                        printf(
                            "CMD para otro RT - ignorado\n"
                        );
                    }


                    printf("\n");
                }
            }


            // ====================================================
            // DATA WORD
            // ====================================================

            else if (type ==
                     MIL1553_RX_TYPE_DATA)
            {
                if (parity_ok)
                {
                    data_ok++;


                    /*
                     * Contadores acumulativos por valor.
                     */
                    if (word == 0x1111u)
                    {
                        total_1111++;
                    }
                    else if (word == 0x2222u)
                    {
                        total_2222++;
                    }
                    else if (word == 0x3333u)
                    {
                        total_3333++;
                    }
                    else
                    {
                        total_data_other++;

                        printf(
                            ">>> DATA INESPERADO: 0x%04X "
                            "parity=%u OK <<<\n\n",
                            word,
                            received_parity
                        );
                    }


                    printf(
                        "DATA recibido: "
                        "0x%04X PARITY=OK\n\n",
                        word
                    );
                }
                else
                {
                    data_par_err++;

                    printf(
                        "DATA ERROR PARIDAD: "
                        "word=0x%04X "
                        "parity=%u esperada=%u\n\n",
                        word,
                        received_parity,
                        expected_parity
                    );
                }
            }
        }


        // ========================================================
        // REPORTE CADA 5 SEGUNDOS
        // ========================================================

        if (absolute_time_diff_us(
                get_absolute_time(),
                next_report) <= 0)
        {
            printf(
                "En 5 s: "
                "CMD[MI_RT=%lu OTRO_RT=%lu PAR_ERR=%lu] "
                "DATA[OK=%lu PAR_ERR=%lu]\n",

                (unsigned long)cmd_for_me,
                (unsigned long)cmd_other_rt,
                (unsigned long)cmd_parity_err,

                (unsigned long)data_ok,
                (unsigned long)data_par_err
            );


            /*
             * Acumulados desde que arrancó el RT.
             */
            printf(
                "TOTAL DATA: "
                "1111=%lu "
                "2222=%lu "
                "3333=%lu "
                "OTROS=%lu\n\n",

                (unsigned long)total_1111,
                (unsigned long)total_2222,
                (unsigned long)total_3333,
                (unsigned long)total_data_other
            );


            /*
             * Solo se reinician los contadores
             * correspondientes a la ventana de 5 segundos.
             */
            cmd_for_me     = 0;
            cmd_other_rt   = 0;
            cmd_parity_err = 0;

            data_ok      = 0;
            data_par_err = 0;


            next_report =
                make_timeout_time_ms(5000);
        }


        tight_loop_contents();
    }
}