#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>

#include "pico/stdlib.h"
#include "hardware/clocks.h"

#include "bus/bus.h"
#include "bus/mil1553_rx.h"
#include "bus/mil1553_tx.h"


/* ============================================================
 * CONFIGURACIÓN
 * ============================================================
 */

#define MY_RT_ADDR             20u
#define EXPECTED_SUBADDR       18u
#define EXPECTED_WORD_COUNT    4u

#define TEST_TRANSACTIONS      1000u

/*
 * Después de este tiempo sin recibir comandos
 * consideramos terminado el ensayo.
 */
#define TEST_END_SILENCE_US    1500000ull


/* ============================================================
 * DATA QUE TRANSMITIRÁ EL RT
 * ============================================================
 */

static const uint16_t data_to_bc[EXPECTED_WORD_COUNT] =
{
    0x4444u,
    0x2222u,
    0x3333u,
    0x1111u
};


/* ============================================================
 * PORCENTAJES
 * ============================================================
 */

static void print_rate(
    const char *name,
    uint32_t correct,
    uint32_t expected)
{
    uint32_t scaled = 0u;

    if (expected != 0u)
    {
        scaled =
            (uint32_t)(
                ((uint64_t)correct *
                 100000ull)
                /
                expected
            );
    }

    printf(
        "%s: %lu.%03lu %%\n",
        name,
        (unsigned long)(scaled / 1000u),
        (unsigned long)(scaled % 1000u)
    );
}


/* ============================================================
 * MAIN
 * ============================================================
 */

int main(void)
{
    /*
     * 120 MHz / 8 MHz = divisor PIO 15 exacto.
     */
    set_sys_clock_khz(
        120000,
        true
    );

    stdio_init_all();

    sleep_ms(1000);


    /*
     * Primero RX.
     * Después TX.
     */
    mil1553_rx_init();
    mil1553_tx_init();


    printf("\n");
    printf("========================================\n");
    printf("RT INICIALIZADO\n");
    printf("Esperando 1000 TRANSMIT COMMAND\n");
    printf("========================================\n");


    /* ========================================================
     * PALABRAS
     * ========================================================
     */

    const uint16_t expected_cmd =
        BUS_1553_CMD_MAKE(
            MY_RT_ADDR,
            BUS_1553_TR_RT_TO_BC,
            EXPECTED_SUBADDR,
            EXPECTED_WORD_COUNT
        );


    const uint16_t status_word =
        BUS_1553_STATUS_MAKE(
            MY_RT_ADDR,
            false
        );


    /* ========================================================
     * CONTADORES
     * ========================================================
     */

    uint32_t cmd_total       = 0u;
    uint32_t cmd_correct     = 0u;
    uint32_t cmd_parity_err  = 0u;
    uint32_t cmd_unexpected  = 0u;

    uint32_t responses_sent  = 0u;
    uint32_t status_sent     = 0u;
    uint32_t data_sent       = 0u;


    /* ========================================================
     * CONTROL DEL TEST
     * ========================================================
     */

    bool test_started = false;
    bool report_done  = false;

    uint64_t last_rx_us = 0u;


    /* ========================================================
     * LOOP
     * ========================================================
     */

    while (true)
    {
        if (mil1553_rx_available())
        {
            uint32_t event =
                mil1553_rx_get();


            test_started = true;

            last_rx_us =
                time_us_64();


            uint32_t type =
                event &
                MIL1553_RX_TYPE_MASK;


            /*
             * El RT está esperando solamente
             * Command / Status sync.
             */
            if (type !=
                MIL1553_RX_TYPE_CMD_STATUS)
            {
                continue;
            }


            cmd_total++;


            uint32_t payload =
                event &
                MIL1553_RX_PAYLOAD_MASK;


            uint16_t word =
                (uint16_t)(
                    (payload >>
                     MIL1553_RX_WORD_SHIFT)
                    &
                    MIL1553_RX_WORD_MASK
                );


            uint8_t received_parity =
                (uint8_t)(
                    payload &
                    MIL1553_RX_PARITY_MASK
                );


            uint8_t expected_parity =
                bus_compute_odd_parity(
                    word
                );


            bool parity_ok =
                (
                    received_parity ==
                    expected_parity
                );


            /* =================================================
             * PARIDAD INCORRECTA
             * =================================================
             */

            if (!parity_ok)
            {
                cmd_parity_err++;

                continue;
            }


            /* =================================================
             * COMMAND INCORRECTO
             * =================================================
             */

            if (word !=
                expected_cmd)
            {
                cmd_unexpected++;

                continue;
            }


            /* =================================================
             * COMMAND CORRECTO
             * =================================================
             */

            cmd_correct++;


            /*
             * Tiempo de respuesta experimental
             * ya validado anteriormente.
             */
            busy_wait_us_32(8u);


            /*
             * Primero STATUS.
             */
            mil1553_tx_send_word(
                BUS_1553_SYNC_CMD_STATUS,
                status_word
            );


            /*
             * Después los cuatro DATA
             * consecutivos.
             */
            for (uint8_t i = 0u;
                 i < EXPECTED_WORD_COUNT;
                 i++)
            {
                mil1553_tx_send_word(
                    BUS_1553_SYNC_DATA,
                    data_to_bc[i]
                );
            }


            /*
             * Esperar finalización física
             * de DATA4.
             */
            mil1553_tx_wait_idle();


            /*
             * Descartar la auto-recepción
             * de STATUS + DATA.
             */
            mil1553_rx_clear();


            responses_sent++;

            status_sent++;

            data_sent +=
                EXPECTED_WORD_COUNT;
        }


        /* ====================================================
         * FIN DEL TEST
         * ====================================================
         */

        if (test_started &&
            !report_done)
        {
            uint64_t now_us =
                time_us_64();


            if ((now_us -
                 last_rx_us)
                >=
                TEST_END_SILENCE_US)
            {
                report_done = true;


                printf("\n\n");

                printf(
                    "========================================\n"
                );

                printf(
                    "RT - RESULTADO TEST RT -> BC\n"
                );

                printf(
                    "========================================\n\n"
                );


                printf(
                    "TRANSACCIONES ESPERADAS : %lu\n\n",
                    (unsigned long)
                        TEST_TRANSACTIONS
                );


                printf(
                    "--- COMMAND ---\n"
                );

                printf(
                    "CMD recibidos total : %lu\n",
                    (unsigned long)
                        cmd_total
                );

                printf(
                    "CMD correctos       : %lu\n",
                    (unsigned long)
                        cmd_correct
                );

                printf(
                    "CMD error paridad   : %lu\n",
                    (unsigned long)
                        cmd_parity_err
                );

                printf(
                    "CMD inesperados     : %lu\n\n",
                    (unsigned long)
                        cmd_unexpected
                );


                printf(
                    "--- RESPUESTAS ---\n"
                );

                printf(
                    "Respuestas enviadas : %lu\n",
                    (unsigned long)
                        responses_sent
                );

                printf(
                    "STATUS enviados     : %lu\n",
                    (unsigned long)
                        status_sent
                );

                printf(
                    "DATA enviados       : %lu\n\n",
                    (unsigned long)
                        data_sent
                );


                printf(
                    "--- TASAS DE EXITO ---\n"
                );


                print_rate(
                    "Exito CMD",
                    cmd_correct,
                    TEST_TRANSACTIONS
                );


                print_rate(
                    "Respuestas completas",
                    responses_sent,
                    TEST_TRANSACTIONS
                );


                printf("\n");

                printf(
                    "CMD esperado         : 0x%04X\n",
                    expected_cmd
                );

                printf(
                    "STATUS enviado       : 0x%04X\n",
                    status_word
                );


                printf("\n");

                printf(
                    "========================================\n"
                );

                printf(
                    "FIN DEL INFORME RT\n"
                );

                printf(
                    "========================================\n"
                );
            }
        }


        tight_loop_contents();
    }
}