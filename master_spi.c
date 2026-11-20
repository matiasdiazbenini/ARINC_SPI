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

#define TEST_TRANSACTIONS       20u

#define INTER_TRANSACTION_MS    100u

/*
 * Timeout amplio solamente para detectar
 * ausencia de STATUS.
 */
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

    /*
     * Caso problematico que estamos validando.
     */
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
    uint32_t scaled =
        0u;


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
    /* ========================================================
     * CLOCK
     * ========================================================
     */

    /*
     * 120 MHz / 8 MHz = divisor PIO 15 exacto.
     */
    set_sys_clock_khz(
        120000,
        true
    );


    stdio_init_all();


    /* ========================================================
     * INICIALIZAR RX Y TX
     * ========================================================
     */

    mil1553_rx_init();

    mil1553_tx_init();


    /*
     * Tiempo para que RT y BM ya esten iniciados.
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


    uint32_t own_command_removed =
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
        "BC - TEST BC -> RT / WC = 32\n"
        "========================================\n"
        "Transacciones       : %u\n"
        "COMMAND             : 0x%04X\n"
        "STATUS esperado     : 0x%04X\n"
        "WC                  : %u\n"
        "DATA32              : 0x%04X\n"
        "RT response         : 8 us\n"
        "Timeout STATUS      : %llu us\n"
        "Rearme rapido SM0   : HABILITADO\n"
        "========================================\n\n",
        TEST_TRANSACTIONS,
        command,
        expected_status,
        WORD_COUNT,
        test_data[WORD_COUNT - 1u],
        (unsigned long long)
            RESPONSE_TIMEOUT_US
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
        /* ====================================================
         * ESTADO INICIAL LIMPIO
         * ====================================================
         */

        mil1553_rx_clear();


        transactions_started++;


        bool own_command_seen =
            false;


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
         * 1. ENVIAR COMMAND
         * ====================================================
         */

        mil1553_tx_send_word(
            BUS_1553_SYNC_CMD_STATUS,
            command
        );


        commands_sent++;


        /*
         * Esperar exactamente hasta que el COMMAND
         * haya terminado fisicamente.
         */
        mil1553_tx_wait_idle_exact();


        /* ====================================================
         * 2. RETIRAR COMMAND PROPIO
         * ====================================================
         *
         * SM0 observa tambien la transmision del propio BC.
         *
         * Lo retiramos antes de enviar DATA.
         * ====================================================
         */

        const uint64_t own_cmd_start =
            time_us_64();


        while ((time_us_64() -
                own_cmd_start)
               <
               200ull)
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


            /*
             * Debe ser exactamente nuestro COMMAND.
             */
            if (word ==
                command)
            {
                own_command_seen =
                    true;


                own_command_removed++;


                break;
            }
        }


        /* ====================================================
         * 3. ENVIAR LOS 32 DATA
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
         * 4. ESPERAR FINAL FISICO EXACTO DE DATA32
         * ====================================================
         *
         * MUY IMPORTANTE:
         *
         * NO usamos el antiguo mil1553_tx_wait_idle()
         * porque despues de FIFO empty agregaba 21 us.
         *
         * El RT responde aproximadamente 8 us despues
         * del final del mensaje BC -> RT.
         *
         * Necesitamos saber el instante real en que
         * DATA32 termino.
         * ====================================================
         */

        mil1553_tx_wait_idle_exact();


        /* ====================================================
         * 5. REARME RAPIDO DE SM0
         * ====================================================
         *
         * DATA32 = 0x8000 puede dejar SM0 en PC6.
         *
         * Lo llevamos inmediatamente a cmd_entry
         * antes de que empiece el STATUS del RT.
         *
         * Este es el cambio principal de esta prueba.
         * ====================================================
         */

        mil1553_rx_cmd_status_rearm();


        /* ====================================================
         * 6. ESPERAR STATUS
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
             * VALOR
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
         * 7. EVALUAR TRANSACCION
         * ====================================================
         */

        if (!status_seen)
        {
            status_timeout++;

            transaction_timeout =
                true;
        }


        bool transaction_ok =
            own_command_seen &&
            status_seen &&
            status_valid;


        if (transaction_ok)
        {
            transactions_ok++;
        }
        else
        {
            transactions_bad++;


            /* ================================================
             * MOSTRAR BAD
             * ================================================
             */

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
                "CMD propio visto   : %s\n",
                own_command_seen
                    ?
                    "SI"
                    :
                    "NO"
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
         * SEPARACION ENTRE TRANSACCIONES
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
        "BC - RESULTADO BC -> RT / WC = 32\n"
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
     * TRANSMISION
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
        "DATA enviados          : %lu\n",
        (unsigned long)
            data_sent
    );


    printf(
        "CMD propios retirados  : %lu\n\n",
        (unsigned long)
            own_command_removed
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
        "STATUS correctos",
        status_correct,
        TEST_TRANSACTIONS
    );


    print_rate(
        "Transacciones completas",
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
        "DATA32 utilizado       : 0x%04X\n",
        test_data[
            WORD_COUNT - 1u
        ]
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