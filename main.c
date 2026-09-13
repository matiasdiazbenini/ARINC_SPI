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

#define TEST_MESSAGES          1000u

/*
 * Después de este tiempo sin recibir nada
 * consideramos terminado el ensayo.
 */
#define TEST_END_SILENCE_US    1500000ull


/* ============================================================
 * DATA ESPERADOS
 * ============================================================
 */

static const uint16_t expected_data[EXPECTED_WORD_COUNT] =
{
    0x4444u,
    0x2222u,
    0x3333u,
    0x1111u
};


/* ============================================================
 * IMPRIMIR PORCENTAJE
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
                ((uint64_t)correct * 100000ull)
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
     * 120 MHz / 8 MHz = divisor 15 exacto.
     */
    set_sys_clock_khz(120000, true);

    stdio_init_all();

    sleep_ms(1000);


    /*
     * IMPORTANTE:
     *
     * RX primero.
     * TX después.
     */
    mil1553_rx_init();
    mil1553_tx_init();


    printf("\n");
    printf("========================================\n");
    printf("RT INICIALIZADO\n");
    printf("Esperando 1000 transacciones\n");
    printf("========================================\n");


    /* ========================================================
     * PALABRAS ESPERADAS
     * ========================================================
     */

    const uint16_t expected_cmd =
        BUS_1553_CMD_MAKE(
            MY_RT_ADDR,
            BUS_1553_TR_BC_TO_RT,
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

    uint32_t data_total      = 0u;
    uint32_t data_correct    = 0u;
    uint32_t data_parity_err = 0u;
    uint32_t data_mismatch   = 0u;
    uint32_t data_orphan     = 0u;

    uint32_t messages_started = 0u;
    uint32_t messages_valid   = 0u;
    uint32_t messages_invalid = 0u;

    uint32_t status_sent      = 0u;


    /* ========================================================
     * ESTADO MENSAJE ACTUAL
     * ========================================================
     */

    bool message_active = false;
    bool message_valid  = false;

    uint8_t data_position = 0u;


    /* ========================================================
     * CONTROL DEL ENSAYO
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
        /* ====================================================
         * RECEPCIÓN
         * ====================================================
         */

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
             * COMMAND
             * =================================================
             */

            if (type ==
                MIL1553_RX_TYPE_CMD_STATUS)
            {
                cmd_total++;


                /*
                 * Si aparece otro CMD y todavía
                 * había un mensaje abierto,
                 * ese mensaje quedó incompleto.
                 */
                if (message_active)
                {
                    messages_invalid++;

                    message_active = false;
                }


                if (!parity_ok)
                {
                    cmd_parity_err++;

                    continue;
                }


                if (word ==
                    expected_cmd)
                {
                    cmd_correct++;

                    messages_started++;

                    message_active = true;
                    message_valid  = true;

                    data_position = 0u;
                }
                else
                {
                    cmd_unexpected++;
                }
            }


            /* =================================================
             * DATA
             * =================================================
             */

            else if (type ==
                     MIL1553_RX_TYPE_DATA)
            {
                data_total++;


                if (!message_active)
                {
                    data_orphan++;

                    continue;
                }


                if (!parity_ok)
                {
                    data_parity_err++;

                    message_valid = false;
                }
                else
                {
                    if (data_position <
                        EXPECTED_WORD_COUNT)
                    {
                        if (word ==
                            expected_data[
                                data_position
                            ])
                        {
                            data_correct++;
                        }
                        else
                        {
                            data_mismatch++;

                            message_valid =
                                false;
                        }
                    }
                    else
                    {
                        data_mismatch++;

                        message_valid =
                            false;
                    }
                }


                data_position++;


                /* =============================================
                 * MENSAJE COMPLETO
                 * =============================================
                 */

                if (data_position >=
                    EXPECTED_WORD_COUNT)
                {
                    message_active =
                        false;


                    if (message_valid)
                    {
                        messages_valid++;


                        /*
                         * Tiempo de respuesta experimental.
                         *
                         * Queremos mantenernos dentro
                         * del intervalo 4...12 us.
                         */
                        busy_wait_us_32(8u);


                        /*
                         * STATUS:
                         *
                         * RT = 20
                         * Message Error = 0
                         *
                         * STATUS = 0xA000
                         */
                        mil1553_tx_send_word(
                            BUS_1553_SYNC_CMD_STATUS,
                            status_word
                        );


                        /*
                         * Esperar hasta finalizar
                         * físicamente el STATUS.
                         */
                        mil1553_tx_wait_idle();


                        /*
                         * El RX también puede haber
                         * observado nuestro propio STATUS.
                         *
                         * Lo descartamos.
                         */
                        mil1553_rx_clear();


                        status_sent++;
                    }
                    else
                    {
                        messages_invalid++;
                    }
                }
            }
        }


        /* ====================================================
         * FIN DEL ENSAYO
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
                /*
                 * Si quedó un mensaje incompleto
                 * al finalizar.
                 */
                if (message_active)
                {
                    messages_invalid++;

                    message_active = false;
                }


                report_done = true;


                /* =============================================
                 * REPORTE
                 * =============================================
                 */

                printf("\n\n");

                printf(
                    "========================================\n"
                );

                printf(
                    "RT - RESULTADO TEST BC -> RT -> STATUS\n"
                );

                printf(
                    "========================================\n\n"
                );


                printf(
                    "MENSAJES ESPERADOS : %lu\n\n",
                    (unsigned long)
                        TEST_MESSAGES
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
                    "--- DATA ---\n"
                );

                printf(
                    "DATA esperados       : %lu\n",
                    (unsigned long)(
                        TEST_MESSAGES *
                        EXPECTED_WORD_COUNT
                    )
                );

                printf(
                    "DATA recibidos total : %lu\n",
                    (unsigned long)
                        data_total
                );

                printf(
                    "DATA correctos       : %lu\n",
                    (unsigned long)
                        data_correct
                );

                printf(
                    "DATA error paridad   : %lu\n",
                    (unsigned long)
                        data_parity_err
                );

                printf(
                    "DATA valor incorrecto: %lu\n",
                    (unsigned long)
                        data_mismatch
                );

                printf(
                    "DATA sin CMD activo  : %lu\n\n",
                    (unsigned long)
                        data_orphan
                );


                printf(
                    "--- MENSAJES ---\n"
                );

                printf(
                    "Mensajes iniciados   : %lu\n",
                    (unsigned long)
                        messages_started
                );

                printf(
                    "Mensajes validos     : %lu\n",
                    (unsigned long)
                        messages_valid
                );

                printf(
                    "Mensajes invalidos   : %lu\n\n",
                    (unsigned long)
                        messages_invalid
                );


                printf(
                    "--- STATUS ---\n"
                );

                printf(
                    "STATUS enviados      : %lu\n",
                    (unsigned long)
                        status_sent
                );

                printf(
                    "STATUS esperado      : 0x%04X\n\n",
                    status_word
                );


                printf(
                    "--- TASAS DE EXITO ---\n"
                );


                print_rate(
                    "Exito CMD",
                    cmd_correct,
                    TEST_MESSAGES
                );


                print_rate(
                    "Exito DATA",
                    data_correct,
                    TEST_MESSAGES *
                    EXPECTED_WORD_COUNT
                );


                print_rate(
                    "Mensajes validos",
                    messages_valid,
                    TEST_MESSAGES
                );


                print_rate(
                    "STATUS enviados",
                    status_sent,
                    TEST_MESSAGES
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