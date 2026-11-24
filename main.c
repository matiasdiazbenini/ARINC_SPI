#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>

#include "pico/stdlib.h"
#include "hardware/clocks.h"

#include "bus/bus.h"
#include "bus/mil1553_rx.h"
#include "bus/mil1553_tx.h"


/* ============================================================
 * CONFIGURACION
 * ============================================================
 */

#define MY_RT_ADDR             20u
#define EXPECTED_SUBADDR       18u
#define EXPECTED_WORD_COUNT    32u

#define TEST_TRANSACTIONS      1000u

/*
 * Timeout entre palabras DATA.
 *
 * No es el tiempo de toda la transaccion.
 *
 * Se reinicia cada vez que llega una DATA.
 */
#define RX_TIMEOUT_US          1000ull

#define TEST_END_SILENCE_US    1500000ull


/* ============================================================
 * DATA ESPERADOS
 * ============================================================
 */

static const uint16_t expected_data[
    EXPECTED_WORD_COUNT
] =
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
 * ESTADOS
 * ============================================================
 */

typedef enum
{
    RT_WAIT_COMMAND = 0,

    RT_RECEIVE_DATA

} rt_state_t;


/* ============================================================
 * DECODIFICAR EVENTO RX
 * ============================================================
 */

static void decode_rx_event(
    uint32_t event,
    uint16_t *word,
    bool *parity_ok)
{
    uint32_t payload =
        event &
        MIL1553_RX_PAYLOAD_MASK;


    *word =
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
            *word
        );


    *parity_ok =
        (
            received_parity ==
            expected_parity
        );
}


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
    /*
     * 120 MHz.
     */
    set_sys_clock_khz(
        120000,
        true
    );


    stdio_init_all();


    sleep_ms(
        1000
    );


    /*
     * Primero RX.
     */
    mil1553_rx_init();


    /*
     * Luego TX.
     */
    mil1553_tx_init();


    mil1553_rx_clear();


    /* ========================================================
     * PALABRAS
     * ========================================================
     */

    const uint16_t expected_cmd =
        BUS_1553_CMD_MAKE(
            MY_RT_ADDR,
            BUS_1553_TR_BC_TO_RT,
            EXPECTED_SUBADDR,
            EXPECTED_WORD_COUNT
        );


    const uint16_t status_ok =
        BUS_1553_STATUS_MAKE(
            MY_RT_ADDR,
            false
        );


    const uint16_t status_error =
        BUS_1553_STATUS_MAKE(
            MY_RT_ADDR,
            true
        );


    /* ========================================================
     * CONTADORES COMMAND
     * ========================================================
     */

    uint32_t cmd_received =
        0u;


    uint32_t cmd_correct =
        0u;


    uint32_t cmd_parity_errors =
        0u;


    uint32_t cmd_unexpected =
        0u;


    /* ========================================================
     * CONTADORES DATA
     * ========================================================
     */

    uint32_t data_expected =
        TEST_TRANSACTIONS *
        EXPECTED_WORD_COUNT;


    uint32_t data_received =
        0u;


    uint32_t data_correct =
        0u;


    uint32_t data_parity_errors =
        0u;


    uint32_t data_mismatch =
        0u;


    uint32_t sequence_errors =
        0u;


    /* ========================================================
     * CONTADORES TRANSACCION
     * ========================================================
     */

    uint32_t transactions_started =
        0u;


    uint32_t transactions_ok =
        0u;


    uint32_t transactions_bad =
        0u;


    uint32_t timeout_count =
        0u;


    uint32_t status_sent =
        0u;


    uint32_t status_error_sent =
        0u;


    /* ========================================================
     * ESTADO
     * ========================================================
     */

    rt_state_t state =
        RT_WAIT_COMMAND;


    uint8_t data_position =
        0u;


    bool transaction_valid =
        true;


    bool test_started =
        false;


    bool report_done =
        false;


    uint64_t last_word_us =
        time_us_64();


    uint64_t last_activity_us =
        time_us_64();


    /* ========================================================
     * INICIO
     * ========================================================
     */

    printf(
        "\n"
        "========================================\n"
        "RT - TEST BC -> RT / WC = 32\n"
        "========================================\n"
    );


    printf(
        "Transacciones    : %lu\n",
        (unsigned long)
            TEST_TRANSACTIONS
    );


    printf(
        "RT               : %u\n",
        MY_RT_ADDR
    );


    printf(
        "Subaddress       : %u\n",
        EXPECTED_SUBADDR
    );


    printf(
        "Word Count       : %u\n",
        EXPECTED_WORD_COUNT
    );


    printf(
        "COMMAND esperado : 0x%04X\n",
        expected_cmd
    );


    printf(
        "STATUS OK        : 0x%04X\n",
        status_ok
    );


    printf(
        "========================================\n"
    );


    /* ========================================================
     * LOOP
     * ========================================================
     */

    while (true)
    {
        uint64_t now_us =
            time_us_64();


        /* ====================================================
         * EVENTO RX
         * ====================================================
         */

        if (mil1553_rx_available())
        {
            uint32_t event =
                mil1553_rx_get();


            uint32_t type =
                event &
                MIL1553_RX_TYPE_MASK;


            uint16_t word =
                0u;


            bool parity_ok =
                false;


            decode_rx_event(
                event,
                &word,
                &parity_ok
            );


            test_started =
                true;


            last_activity_us =
                now_us;


            /* =================================================
             * ESPERANDO COMMAND
             * =================================================
             */

            if (state ==
                RT_WAIT_COMMAND)
            {
                /*
                 * Debe ser CMD/STATUS sync.
                 */
                if (type !=
                    MIL1553_RX_TYPE_CMD_STATUS)
                {
                    continue;
                }


                cmd_received++;


                /*
                 * Paridad COMMAND.
                 */
                if (!parity_ok)
                {
                    cmd_parity_errors++;


                    continue;
                }


                /*
                 * COMMAND esperado.
                 */
                if (word !=
                    expected_cmd)
                {
                    cmd_unexpected++;


                    continue;
                }


                /* =============================================
                 * COMMAND CORRECTO
                 * =============================================
                 */

                cmd_correct++;


                transactions_started++;


                data_position =
                    0u;


                transaction_valid =
                    true;


                state =
                    RT_RECEIVE_DATA;


                last_word_us =
                    now_us;


                continue;
            }


            /* =================================================
             * RECIBIENDO DATA
             * =================================================
             */

            if (state ==
                RT_RECEIVE_DATA)
            {
                /*
                 * Debe ser DATA sync.
                 */
                if (type !=
                    MIL1553_RX_TYPE_DATA)
                {
                    sequence_errors++;


                    transaction_valid =
                        false;


                    continue;
                }


                data_received++;


                /* =============================================
                 * PARIDAD
                 * =============================================
                 */

                if (!parity_ok)
                {
                    data_parity_errors++;


                    transaction_valid =
                        false;
                }


                /* =============================================
                 * VALOR / POSICION
                 * =============================================
                 */

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


                        transaction_valid =
                            false;
                    }
                }
                else
                {
                    sequence_errors++;


                    transaction_valid =
                        false;
                }


                data_position++;


                /*
                 * El timeout se reinicia
                 * en cada DATA recibida.
                 */
                last_word_us =
                    now_us;


                /* =============================================
                 * DATA32
                 * =============================================
                 */

                if (data_position >=
                    EXPECTED_WORD_COUNT)
                {
                    /*
                     * Tiempo de respuesta experimental
                     * utilizado en las pruebas anteriores.
                     */
                    busy_wait_us_32(
                        8u
                    );


                    uint16_t status_to_send =
                        transaction_valid
                            ?
                            status_ok
                            :
                            status_error;


                    /*
                     * Enviar STATUS.
                     */
                    mil1553_tx_send_word(
                        BUS_1553_SYNC_CMD_STATUS,
                        status_to_send
                    );


                    /*
                     * Esperar final fisico.
                     */
                    mil1553_tx_wait_idle();


                    status_sent++;


                    if (!transaction_valid)
                    {
                        status_error_sent++;
                    }


                    /*
                     * Descartar auto-recepcion
                     * del STATUS.
                     */
                    mil1553_rx_clear();


                    if (transaction_valid)
                    {
                        transactions_ok++;
                    }
                    else
                    {
                        transactions_bad++;


                        printf(
                            "\n"
                            "----------------------------------------\n"
                            "RT - TRANSACCION BAD\n"
                            "----------------------------------------\n"
                        );


                        printf(
                            "Transaccion       : %lu\n",
                            (unsigned long)
                                transactions_started
                        );


                        printf(
                            "DATA recibidos    : %u/%u\n",
                            data_position,
                            EXPECTED_WORD_COUNT
                        );


                        printf(
                            "----------------------------------------\n"
                        );
                    }


                    data_position =
                        0u;


                    transaction_valid =
                        true;


                    state =
                        RT_WAIT_COMMAND;


                    last_word_us =
                        time_us_64();


                    last_activity_us =
                        last_word_us;
                }
            }
        }


        /* ====================================================
         * TIMEOUT DATA
         * ====================================================
         */

        if ((state ==
             RT_RECEIVE_DATA)
            &&
            ((now_us -
              last_word_us)
             >=
             RX_TIMEOUT_US))
        {
            timeout_count++;


            transactions_bad++;


            printf(
                "\n"
                "----------------------------------------\n"
                "RT - TIMEOUT BC -> RT\n"
                "----------------------------------------\n"
            );


            printf(
                "Transaccion       : %lu\n",
                (unsigned long)
                    transactions_started
            );


            printf(
                "DATA recibidos    : %u/%u\n",
                data_position,
                EXPECTED_WORD_COUNT
            );


            printf(
                "Siguiente esperado: DATA%u = 0x%04X\n",
                (unsigned int)(
                    data_position + 1u
                ),
                (
                    data_position <
                    EXPECTED_WORD_COUNT
                )
                    ?
                    expected_data[
                        data_position
                    ]
                    :
                    0u
            );


            printf(
                "----------------------------------------\n"
            );


            /*
             * Recuperar receptor.
             */
            mil1553_rx_clear();


            data_position =
                0u;


            transaction_valid =
                true;


            state =
                RT_WAIT_COMMAND;


            last_word_us =
                time_us_64();


            last_activity_us =
                last_word_us;
        }


        /* ====================================================
         * INFORME FINAL
         * ====================================================
         */

        if (test_started &&
            !report_done &&
            (state ==
             RT_WAIT_COMMAND))
        {
            now_us =
                time_us_64();


            if ((now_us -
                 last_activity_us)
                >=
                TEST_END_SILENCE_US)
            {
                report_done =
                    true;


                printf(
                    "\n\n"
                    "========================================\n"
                    "RT - RESULTADO BC -> RT / WC = 32\n"
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


                printf(
                    "--- COMMAND ---\n"
                );


                printf(
                    "CMD recibidos          : %lu\n",
                    (unsigned long)
                        cmd_received
                );


                printf(
                    "CMD correctos          : %lu\n",
                    (unsigned long)
                        cmd_correct
                );


                printf(
                    "CMD error paridad      : %lu\n",
                    (unsigned long)
                        cmd_parity_errors
                );


                printf(
                    "CMD inesperados        : %lu\n\n",
                    (unsigned long)
                        cmd_unexpected
                );


                printf(
                    "--- DATA ---\n"
                );


                printf(
                    "DATA esperados         : %lu\n",
                    (unsigned long)
                        data_expected
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
                        data_parity_errors
                );


                printf(
                    "DATA incorrectos       : %lu\n\n",
                    (unsigned long)
                        data_mismatch
                );


                printf(
                    "--- STATUS ---\n"
                );


                printf(
                    "STATUS enviados        : %lu\n",
                    (unsigned long)
                        status_sent
                );


                printf(
                    "STATUS MSG_ERROR       : %lu\n\n",
                    (unsigned long)
                        status_error_sent
                );


                printf(
                    "--- ERRORES ---\n"
                );


                printf(
                    "Timeout DATA           : %lu\n",
                    (unsigned long)
                        timeout_count
                );


                printf(
                    "Errores secuencia      : %lu\n\n",
                    (unsigned long)
                        sequence_errors
                );


                printf(
                    "--- TASAS ---\n"
                );


                print_rate(
                    "CMD correctos",
                    cmd_correct,
                    TEST_TRANSACTIONS
                );


                print_rate(
                    "DATA recibidos",
                    data_received,
                    data_expected
                );


                print_rate(
                    "DATA correctos",
                    data_correct,
                    data_expected
                );


                print_rate(
                    "Transacciones completas",
                    transactions_ok,
                    TEST_TRANSACTIONS
                );


                printf(
                    "\n"
                    "COMMAND esperado       : 0x%04X\n",
                    expected_cmd
                );


                printf(
                    "STATUS OK              : 0x%04X\n",
                    status_ok
                );


                printf(
                    "\n"
                    "========================================\n"
                    "FIN DEL INFORME RT\n"
                    "========================================\n"
                );
            }
        }


        tight_loop_contents();
    }
}