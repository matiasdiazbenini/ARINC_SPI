#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>

#include "pico/stdlib.h"
#include "hardware/clocks.h"

#include "bus/bus.h"
#include "bus/mil1553_tx.h"
#include "bus/mil1553_rx.h"


/* ============================================================
 * CONFIGURACIÓN
 * ============================================================
 */

#define RT_ADDR                 20u
#define SUBADDR                 18u
#define WORD_COUNT              4u

#define TEST_TRANSACTIONS       1000u

#define INTER_TRANSACTION_MS    100u

/*
 * Timeout amplio para recibir:
 *
 * STATUS + 4 DATA
 */
#define RESPONSE_TIMEOUT_US     300ull


/* ============================================================
 * DATA ESPERADOS
 * ============================================================
 */

static const uint16_t expected_data[WORD_COUNT] =
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


    /*
     * Primero RX.
     * Después TX.
     */
    mil1553_rx_init();
    mil1553_tx_init();


    /*
     * Dar tiempo al RT para iniciar.
     */
    sleep_ms(10000);


    /* ========================================================
     * PALABRAS
     * ========================================================
     */

    const uint16_t cmd =
        BUS_1553_CMD_MAKE(
            RT_ADDR,
            BUS_1553_TR_RT_TO_BC,
            SUBADDR,
            WORD_COUNT
        );


    const uint16_t expected_status =
        BUS_1553_STATUS_MAKE(
            RT_ADDR,
            false
        );


    /* ========================================================
     * CONTADORES
     * ========================================================
     */

    uint32_t transactions_sent = 0u;
    uint32_t transactions_ok   = 0u;


    uint32_t status_received   = 0u;
    uint32_t status_correct    = 0u;

    uint32_t status_parity_err = 0u;
    uint32_t status_unexpected = 0u;


    uint32_t data_received     = 0u;
    uint32_t data_correct      = 0u;

    uint32_t data_parity_err   = 0u;
    uint32_t data_mismatch     = 0u;

    uint32_t data_before_status = 0u;
    uint32_t data_extra         = 0u;


    uint32_t response_timeout  = 0u;


    printf("\n");

    printf(
        "========================================\n"
    );

    printf(
        "BC - INICIANDO 1000 TRANSACCIONES RT -> BC\n"
    );

    printf(
        "========================================\n"
    );


    /* ========================================================
     * ENSAYO
     * ========================================================
     */

    for (uint32_t transaction = 0u;
         transaction <
         TEST_TRANSACTIONS;
         transaction++)
    {
        /*
         * Limpiar cualquier evento anterior.
         */
        mil1553_rx_clear();


        /* ====================================================
         * TRANSMIT COMMAND
         * ====================================================
         */

        mil1553_tx_send_word(
            BUS_1553_SYNC_CMD_STATUS,
            cmd
        );


        transactions_sent++;


        /*
         * Esperar hasta que el Command
         * termine físicamente.
         */
        mil1553_tx_wait_idle();


        /*
         * El BC también observó su propio CMD.
         *
         * Limpiar y rearmar receptor.
         */
        mil1553_rx_clear();


        /* ====================================================
         * ESTADO DE ESTA TRANSACCIÓN
         * ====================================================
         */

        bool status_seen  = false;
        bool status_valid = false;

        bool message_valid = true;

        bool response_complete =
            false;


        uint8_t data_position =
            0u;


        const uint64_t timeout_start =
            time_us_64();


        /* ====================================================
         * ESPERAR STATUS + DATA
         * ====================================================
         */

        while (!response_complete &&
               ((time_us_64() -
                 timeout_start)
                <
                RESPONSE_TIMEOUT_US))
        {
            if (!mil1553_rx_available())
            {
                tight_loop_contents();

                continue;
            }


            uint32_t event =
                mil1553_rx_get();


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
             * STATUS
             * =================================================
             */

            if (type ==
                MIL1553_RX_TYPE_CMD_STATUS)
            {
                /*
                 * Solo esperamos un STATUS.
                 */
                if (status_seen)
                {
                    status_unexpected++;

                    message_valid =
                        false;

                    continue;
                }


                status_seen = true;

                status_received++;


                if (!parity_ok)
                {
                    status_parity_err++;

                    message_valid =
                        false;

                    continue;
                }


                if (word ==
                    expected_status)
                {
                    status_correct++;

                    status_valid =
                        true;
                }
                else
                {
                    status_unexpected++;

                    message_valid =
                        false;
                }
            }


            /* =================================================
             * DATA
             * =================================================
             */

            else if (type ==
                     MIL1553_RX_TYPE_DATA)
            {
                data_received++;


                /*
                 * DATA antes del STATUS
                 * no corresponde a la secuencia.
                 */
                if (!status_valid)
                {
                    data_before_status++;

                    message_valid =
                        false;

                    continue;
                }


                /*
                 * Más de cuatro DATA.
                 */
                if (data_position >=
                    WORD_COUNT)
                {
                    data_extra++;

                    message_valid =
                        false;

                    continue;
                }


                /*
                 * Paridad.
                 */
                if (!parity_ok)
                {
                    data_parity_err++;

                    message_valid =
                        false;
                }
                else
                {
                    /*
                     * Valor y posición.
                     */
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


                data_position++;


                /*
                 * Ya llegaron los cuatro DATA.
                 */
                if (data_position >=
                    WORD_COUNT)
                {
                    response_complete =
                        true;
                }
            }
        }


        /* ====================================================
         * EVALUAR TRANSACCIÓN
         * ====================================================
         */

        if (!response_complete)
        {
            response_timeout++;
        }
        else
        {
            if (status_valid &&
                data_position ==
                    WORD_COUNT &&
                message_valid)
            {
                transactions_ok++;
            }
        }


        /*
         * Separación entre transacciones.
         */
        sleep_ms(
            INTER_TRANSACTION_MS
        );
    }


    /* ========================================================
     * REPORTE FINAL
     * ========================================================
     */

    printf("\n\n");

    printf(
        "========================================\n"
    );

    printf(
        "BC - RESULTADO TEST RT -> BC\n"
    );

    printf(
        "========================================\n\n"
    );


    printf(
        "TRANSACCIONES ESPERADAS : %lu\n\n",
        (unsigned long)
            TEST_TRANSACTIONS
    );


    /* ========================================================
     * TRANSMISIÓN
     * ========================================================
     */

    printf(
        "--- TRANSMISION BC ---\n"
    );

    printf(
        "CMD enviados           : %lu\n\n",
        (unsigned long)
            transactions_sent
    );


    /* ========================================================
     * STATUS
     * ========================================================
     */

    printf(
        "--- STATUS ---\n"
    );

    printf(
        "STATUS recibidos       : %lu\n",
        (unsigned long)
            status_received
    );

    printf(
        "STATUS correctos       : %lu\n",
        (unsigned long)
            status_correct
    );

    printf(
        "STATUS error paridad   : %lu\n",
        (unsigned long)
            status_parity_err
    );

    printf(
        "STATUS inesperados     : %lu\n\n",
        (unsigned long)
            status_unexpected
    );


    /* ========================================================
     * DATA
     * ========================================================
     */

    printf(
        "--- DATA ---\n"
    );

    printf(
        "DATA esperados         : %lu\n",
        (unsigned long)(
            TEST_TRANSACTIONS *
            WORD_COUNT
        )
    );

    printf(
        "DATA recibidos         : %lu\n",
        (unsigned long)
            data_received
    );

    printf(
        "DATA correctos         : %lu\n",
        (unsigned long)
            data_correct
    );

    printf(
        "DATA error paridad     : %lu\n",
        (unsigned long)
            data_parity_err
    );

    printf(
        "DATA valor incorrecto  : %lu\n",
        (unsigned long)
            data_mismatch
    );

    printf(
        "DATA antes de STATUS   : %lu\n",
        (unsigned long)
            data_before_status
    );

    printf(
        "DATA extra             : %lu\n\n",
        (unsigned long)
            data_extra
    );


    /* ========================================================
     * TRANSACCIONES
     * ========================================================
     */

    printf(
        "--- TRANSACCIONES ---\n"
    );

    printf(
        "Transacciones OK       : %lu\n",
        (unsigned long)
            transactions_ok
    );

    printf(
        "Transacciones BAD      : %lu\n",
        (unsigned long)(
            TEST_TRANSACTIONS -
            transactions_ok
        )
    );

    printf(
        "Timeout respuesta      : %lu\n\n",
        (unsigned long)
            response_timeout
    );


    /* ========================================================
     * TASAS
     * ========================================================
     */

    printf(
        "--- TASAS DE EXITO ---\n"
    );


    print_rate(
        "STATUS correctos",
        status_correct,
        TEST_TRANSACTIONS
    );


    print_rate(
        "DATA correctos",
        data_correct,
        TEST_TRANSACTIONS *
        WORD_COUNT
    );


    print_rate(
        "Transacciones completas",
        transactions_ok,
        TEST_TRANSACTIONS
    );


    printf("\n");

    printf(
        "CMD enviado esperado   : 0x%04X\n",
        cmd
    );

    printf(
        "STATUS esperado        : 0x%04X\n",
        expected_status
    );

    printf(
        "RT esperado            : %u\n",
        RT_ADDR
    );

    printf(
        "Message Error esperado : 0\n"
    );


    printf("\n");

    printf(
        "========================================\n"
    );

    printf(
        "FIN DEL INFORME BC\n"
    );

    printf(
        "========================================\n"
    );


    while (true)
    {
        tight_loop_contents();
    }
}