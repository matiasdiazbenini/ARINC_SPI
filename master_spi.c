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

#define RT_ADDR              20u
#define SUBADDR              18u
#define WORD_COUNT           4u

#define TEST_TRANSACTIONS    1000u

#define INTER_TRANSACTION_MS 100u

/*
 * Timeout experimental amplio.
 *
 * Más adelante lo ajustaremos.
 */
#define STATUS_TIMEOUT_US    200ull


/* ============================================================
 * DATA
 * ============================================================
 */

static const uint16_t data_test[WORD_COUNT] =
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
                ((uint64_t)correct *
                 100000ull)
                /
                expected
            );
    }

    printf(
        "%s: %lu.%03lu %%\n",
        name,
        (unsigned long)(
            scaled / 1000u
        ),
        (unsigned long)(
            scaled % 1000u
        )
    );
}


/* ============================================================
 * MAIN
 * ============================================================
 */

int main(void)
{
    /*
     * 120 MHz / 8 MHz = divisor entero 15.
     */
    set_sys_clock_khz(
        120000,
        true
    );


    stdio_init_all();


    /*
     * Inicializar primero RX
     * y después TX.
     */
    mil1553_rx_init();
    mil1553_tx_init();


    /*
     * Dar tiempo al RT para arrancar.
     */
    sleep_ms(10000);


    /* ========================================================
     * PALABRAS
     * ========================================================
     */

    const uint16_t cmd =
        BUS_1553_CMD_MAKE(
            RT_ADDR,
            BUS_1553_TR_BC_TO_RT,
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

    uint32_t status_timeout    = 0u;

    uint32_t unexpected_data   = 0u;


    printf("\n");

    printf(
        "========================================\n"
    );

    printf(
        "BC - INICIANDO TEST DE 1000 TRANSACCIONES\n"
    );

    printf(
        "========================================\n"
    );


    /* ========================================================
     * TEST
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
         * COMMAND
         * ====================================================
         */

        mil1553_tx_send_word(
            BUS_1553_SYNC_CMD_STATUS,
            cmd
        );


        /* ====================================================
         * DATA
         * ====================================================
         */

        for (uint8_t i = 0u;
             i < WORD_COUNT;
             i++)
        {
            mil1553_tx_send_word(
                BUS_1553_SYNC_DATA,
                data_test[i]
            );
        }


        transactions_sent++;


        /*
         * Esperar hasta que DATA4 termine
         * físicamente.
         */
        mil1553_tx_wait_idle();


        /*
         * Descartar CMD + DATA propios
         * capturados por nuestro RX.
         */
        mil1553_rx_clear();


        /* ====================================================
         * ESPERAR STATUS
         * ====================================================
         */

        uint64_t timeout_start =
            time_us_64();


        bool transaction_finished =
            false;


        while (!transaction_finished &&
               ((time_us_64() -
                 timeout_start)
                <
                STATUS_TIMEOUT_US))
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


            /*
             * Si aparece DATA acá,
             * no es el STATUS esperado.
             */
            if (type ==
                MIL1553_RX_TYPE_DATA)
            {
                unexpected_data++;

                continue;
            }


            /*
             * COMMAND / STATUS
             */
            if (type ==
                MIL1553_RX_TYPE_CMD_STATUS)
            {
                status_received++;


                uint32_t payload =
                    event &
                    MIL1553_RX_PAYLOAD_MASK;


                uint16_t status_word =
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
                        status_word
                    );


                bool parity_ok =
                    (
                        received_parity ==
                        expected_parity
                    );


                /*
                 * Error de paridad.
                 */
                if (!parity_ok)
                {
                    status_parity_err++;

                    transaction_finished =
                        true;

                    break;
                }


                /*
                 * STATUS correcto.
                 */
                if (status_word ==
                    expected_status)
                {
                    status_correct++;

                    transactions_ok++;

                    transaction_finished =
                        true;

                    break;
                }


                /*
                 * Palabra Command/Status
                 * pero valor incorrecto.
                 */
                status_unexpected++;

                transaction_finished =
                    true;

                break;
            }
        }


        /*
         * Si terminó el timeout sin recibir
         * un STATUS válido o inválido.
         */
        if (!transaction_finished)
        {
            status_timeout++;
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
        "BC - RESULTADO TEST BC -> RT -> STATUS\n"
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
        "--- TRANSMISION ---\n"
    );

    printf(
        "Transacciones enviadas : %lu\n",
        (unsigned long)
            transactions_sent
    );

    printf(
        "CMD enviados           : %lu\n",
        (unsigned long)
            transactions_sent
    );

    printf(
        "DATA enviados          : %lu\n\n",
        (unsigned long)(
            transactions_sent *
            WORD_COUNT
        )
    );


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
        "STATUS inesperados     : %lu\n",
        (unsigned long)
            status_unexpected
    );

    printf(
        "STATUS timeout         : %lu\n",
        (unsigned long)
            status_timeout
    );

    printf(
        "DATA inesperados RX    : %lu\n\n",
        (unsigned long)
            unexpected_data
    );


    printf(
        "--- TRANSACCIONES ---\n"
    );

    printf(
        "Transacciones OK       : %lu\n",
        (unsigned long)
            transactions_ok
    );

    printf(
        "Transacciones BAD      : %lu\n\n",
        (unsigned long)(
            TEST_TRANSACTIONS -
            transactions_ok
        )
    );


    printf(
        "--- TASAS DE EXITO ---\n"
    );


    print_rate(
        "STATUS correctos",
        status_correct,
        TEST_TRANSACTIONS
    );


    print_rate(
        "Transacciones completas",
        transactions_ok,
        TEST_TRANSACTIONS
    );


    printf("\n");

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