#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>

#include "pico/stdlib.h"
#include "hardware/clocks.h"

#include "bus/bus.h"
#include "bus/mil1553_tx.h"
#include "bus/mil1553_rx.h"


/* ============================================================
 * CONFIGURACION
 * ============================================================
 */

#define RT_ADDR                 20u
#define SUBADDR                 18u
#define WORD_COUNT              32u

#define TEST_TRANSACTIONS       1000u

#define INTER_TRANSACTION_MS    100u

#define RESPONSE_TIMEOUT_US     1500ull


/* ============================================================
 * DATA DE PRUEBA
 * ============================================================
 */

static const uint16_t test_data[WORD_COUNT] =
{
    0x4444u,
    0x2222u,
    0x3333u,
    0x1111u,

    0xAAAAu,
    0x5555u,
    0xFFFFu,
    0x0000u,

    0x1234u,
    0x5678u,
    0xABCDu,
    0xDCBAu,

    0x0F0Fu,
    0xF0F0u,
    0x8001u,
    0x7FFEu,

    0x0001u,
    0x0002u,
    0x0004u,
    0x0008u,

    0x0010u,
    0x0020u,
    0x0040u,
    0x0080u,

    0x0100u,
    0x0200u,
    0x0400u,
    0x0800u,

    0x1000u,
    0x2000u,
    0x4000u,
    0x8000u
};


/* ============================================================
 * PORCENTAJE
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
    /* ========================================================
     * CLOCK
     * ========================================================
     */

    set_sys_clock_khz(
        120000,
        true
    );


    stdio_init_all();


    /* ========================================================
     * RX / TX
     * ========================================================
     */

    mil1553_rx_init();

    mil1553_tx_init();


    /*
     * Tiempo para iniciar RT y BM.
     */
    sleep_ms(
        5000
    );


    /* ========================================================
     * COMMAND
     * ========================================================
     */

    const uint16_t command =
        BUS_1553_CMD_MAKE(
            RT_ADDR,
            BUS_1553_TR_BC_TO_RT,
            SUBADDR,
            WORD_COUNT
        );


    /* ========================================================
     * STATUS ESPERADO
     * ========================================================
     */

    const uint16_t expected_status =
        BUS_1553_STATUS_MAKE(
            RT_ADDR,
            false
        );


    /* ========================================================
     * CONTADORES
     * ========================================================
     */

    uint32_t transactions_started =
        0u;


    uint32_t transactions_ok =
        0u;


    uint32_t transactions_bad =
        0u;


    uint32_t commands_sent =
        0u;


    uint32_t data_sent =
        0u;


    uint32_t status_received =
        0u;


    uint32_t status_correct =
        0u;


    uint32_t status_parity_error =
        0u;


    uint32_t status_unexpected =
        0u;


    uint32_t status_timeout =
        0u;


    /* ========================================================
     * CABECERA
     * ========================================================
     */

    printf(
        "\n"
        "========================================\n"
        "BC - TX ESTABLE + RX STATUS / WC = 32\n"
        "========================================\n"
        "Transacciones        : %u\n"
        "COMMAND              : 0x%04X\n"
        "STATUS esperado      : 0x%04X\n"
        "WC                   : %u\n"
        "DATA1                : 0x%04X\n"
        "DATA2                : 0x%04X\n"
        "DATA32               : 0x%04X\n"
        "CMD -> DATA          : CONTINUO\n"
        "Rearme durante DATA  : NO\n"
        "Rearme post DATA32   : SI\n"
        "Timeout STATUS       : %llu us\n"
        "========================================\n\n",
        TEST_TRANSACTIONS,
        command,
        expected_status,
        WORD_COUNT,
        test_data[0],
        test_data[1],
        test_data[WORD_COUNT - 1u],
        (unsigned long long)
            RESPONSE_TIMEOUT_US
    );


    /* ========================================================
     * ENSAYO
     * ========================================================
     */

    for (uint32_t transaction = 0u;
         transaction < TEST_TRANSACTIONS;
         transaction++)
    {
        /* ====================================================
         * ESTADO INICIAL
         * ====================================================
         */

        mil1553_rx_clear();


        transactions_started++;


        bool status_seen =
            false;


        bool status_valid =
            false;


        bool transaction_timeout =
            false;


        uint16_t received_status =
            0u;


        bool received_status_parity_ok =
            false;


        /* ====================================================
         * 1. COMMAND
         * ====================================================
         *
         * IMPORTANTE:
         *
         * ESTE BLOQUE QUEDA CONGELADO.
         *
         * NO:
         *
         * - wait_idle aqui
         * - polling RX aqui
         * - retirar COMMAND propio
         * - delays
         * - rearme
         *
         * Inmediatamente despues del COMMAND se encolan
         * DATA1 ... DATA32.
         * ====================================================
         */

        mil1553_tx_send_word(
            BUS_1553_SYNC_CMD_STATUS,
            command
        );


        commands_sent++;


        /* ====================================================
         * 2. DATA1 ... DATA32
         * ====================================================
         *
         * Misma secuencia que acaba de producir:
         *
         * RT:
         *      640/640
         *
         * BM:
         *      640/640
         *      20/20 validas
         * ====================================================
         */

        for (uint8_t i = 0u;
             i < WORD_COUNT;
             i++)
        {
            mil1553_tx_send_word(
                BUS_1553_SYNC_DATA,
                test_data[i]
            );


            data_sent++;
        }


        /* ====================================================
         * 3. ESPERAR FINAL FISICO DE DATA32
         * ====================================================
         *
         * Esta llamada SI estaba presente en el rollback
         * exitoso.
         *
         * FIFO vacia + margen fisico.
         *
         * DATA32 dura aproximadamente 20 us.
         * ====================================================
         */

        mil1553_tx_wait_idle();


        /* ====================================================
         * 4. REARME RAPIDO SM0
         * ====================================================
         *
         * ESTE ES EL UNICO CAMBIO RESPECTO AL ROLLBACK.
         *
         * Hasta aqui la transmision fue exactamente igual
         * a la prueba que dio:
         *
         *      RT = 20/20
         *      BM = 20/20
         *
         * Ahora:
         *
         * - detenemos solamente SM0
         * - limpiamos FIFO CMD/STATUS
         * - reiniciamos SM0
         * - saltamos a cmd_entry
         * - habilitamos nuevamente
         *
         * Esto tambien elimina de la FIFO del BC
         * su propio COMMAND.
         * ====================================================
         */

        mil1553_rx_cmd_status_rearm();


        /* ====================================================
         * 5. ESPERAR STATUS
         * ====================================================
         *
         * RT responde aproximadamente 8 us despues del
         * final del mensaje BC -> RT.
         * ====================================================
         */

        const uint64_t status_wait_start =
            time_us_64();


        while ((time_us_64() -
                status_wait_start)
               <
               RESPONSE_TIMEOUT_US)
        {
            if (!mil1553_rx_cmd_status_available())
            {
                tight_loop_contents();

                continue;
            }


            uint32_t event =
                mil1553_rx_get_cmd_status();


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


            status_seen =
                true;


            status_received++;


            received_status =
                word;


            received_status_parity_ok =
                parity_ok;


            /* ================================================
             * PARIDAD
             * ================================================
             */

            if (!parity_ok)
            {
                status_parity_error++;

                break;
            }


            /* ================================================
             * VALOR STATUS
             * ================================================
             */

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
            }


            break;
        }


        /* ====================================================
         * 6. TIMEOUT
         * ====================================================
         */

        if (!status_seen)
        {
            status_timeout++;

            transaction_timeout =
                true;
        }


        /* ====================================================
         * 7. RESULTADO TRANSACCION
         * ====================================================
         */

        if (status_seen &&
            status_valid)
        {
            transactions_ok++;


            printf(
                "TX %2lu/%u | "
                "STATUS=0x%04X | "
                "paridad=%s | OK\n",
                (unsigned long)(
                    transaction + 1u
                ),
                TEST_TRANSACTIONS,
                received_status,
                received_status_parity_ok
                    ?
                    "OK"
                    :
                    "ERROR"
            );
        }
        else
        {
            transactions_bad++;


            printf(
                "\n"
                "----------------------------------------\n"
                "BC -> RT - TRANSACCION BAD\n"
                "----------------------------------------\n"
            );


            printf(
                "Transaccion        : %lu\n",
                (unsigned long)(
                    transaction + 1u
                )
            );


            printf(
                "COMMAND            : 0x%04X\n",
                command
            );


            printf(
                "DATA1              : 0x%04X\n",
                test_data[0]
            );


            printf(
                "DATA32             : 0x%04X\n",
                test_data[
                    WORD_COUNT - 1u
                ]
            );


            if (status_seen)
            {
                printf(
                    "STATUS             : 0x%04X\n",
                    received_status
                );


                printf(
                    "STATUS esperado    : 0x%04X\n",
                    expected_status
                );


                printf(
                    "STATUS paridad     : %s\n",
                    received_status_parity_ok
                        ?
                        "OK"
                        :
                        "ERROR"
                );
            }
            else
            {
                printf(
                    "STATUS             : NO RECIBIDO\n"
                );
            }


            printf(
                "Timeout            : %s\n",
                transaction_timeout
                    ?
                    "SI"
                    :
                    "NO"
            );


            printf(
                "----------------------------------------\n"
            );
        }


        /* ====================================================
         * SEPARACION
         * ====================================================
         */

        sleep_ms(
            INTER_TRANSACTION_MS
        );
    }


    /* ========================================================
     * INFORME FINAL
     * ========================================================
     */

    printf(
        "\n\n"
        "========================================\n"
        "BC - RESULTADO TX ESTABLE + RX STATUS\n"
        "========================================\n\n"
    );


    printf(
        "TRANSACCIONES ESPERADAS : %lu\n",
        (unsigned long)
            TEST_TRANSACTIONS
    );


    printf(
        "Iniciadas              : %lu\n",
        (unsigned long)
            transactions_started
    );


    printf(
        "OK                     : %lu\n",
        (unsigned long)
            transactions_ok
    );


    printf(
        "BAD                    : %lu\n\n",
        (unsigned long)
            transactions_bad
    );


    /* ========================================================
     * TX
     * ========================================================
     */

    printf(
        "--- TRANSMISION ---\n"
    );


    printf(
        "CMD enviados           : %lu\n",
        (unsigned long)
            commands_sent
    );


    printf(
        "DATA enviados          : %lu\n\n",
        (unsigned long)
            data_sent
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
            status_parity_error
    );


    printf(
        "STATUS inesperados     : %lu\n",
        (unsigned long)
            status_unexpected
    );


    printf(
        "Timeout STATUS         : %lu\n\n",
        (unsigned long)
            status_timeout
    );


    /* ========================================================
     * TASAS
     * ========================================================
     */

    printf(
        "--- TASAS ---\n"
    );


    print_rate(
        "STATUS recibidos",
        status_received,
        TEST_TRANSACTIONS
    );


    print_rate(
        "STATUS correctos",
        status_correct,
        TEST_TRANSACTIONS
    );


    print_rate(
        "Transacciones OK",
        transactions_ok,
        TEST_TRANSACTIONS
    );


    /* ========================================================
     * PARAMETROS
     * ========================================================
     */

    printf(
        "\n"
        "COMMAND utilizado      : 0x%04X\n",
        command
    );


    printf(
        "STATUS esperado        : 0x%04X\n",
        expected_status
    );


    printf(
        "DATA1                  : 0x%04X\n",
        test_data[0]
    );


    printf(
        "DATA32                 : 0x%04X\n",
        test_data[
            WORD_COUNT - 1u
        ]
    );


    printf(
        "CMD -> DATA continuo   : SI\n"
    );


    printf(
        "Rearme post DATA32     : SI\n"
    );


    printf(
        "RT response configurado: 8 us\n"
    );


    printf(
        "\n"
        "========================================\n"
        "FIN DEL INFORME BC\n"
        "========================================\n"
    );


    while (true)
    {
        tight_loop_contents();
    }
}