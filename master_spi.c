#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>

#include "pico/stdlib.h"

#include "hardware/clocks.h"
#include "hardware/pio.h"

#include "bus/bus.h"
#include "bus/mil1553_tx.h"
#include "bus/mil1553_rx.h"


/* ============================================================
 * CONFIGURACION
 * ============================================================
 */

#define RT_ADDR                 20u
#define SUBADDR                 18u
#define WORD_COUNT              4u

#define TEST_TRANSACTIONS       10000u

#define INTER_TRANSACTION_MS    100u

/*
 * Timeout para recibir:
 *
 * STATUS + 4 DATA
 */
#define RESPONSE_TIMEOUT_US     300ull


/* ============================================================
 * RECEPTOR PIO
 *
 * Igual que en mil1553_rx.c:
 *
 * PIO1
 *
 * SM0 -> P -> CMD / STATUS
 * SM1 -> N -> DATA
 * ============================================================
 */

#define BC_RX_PIO               pio1
#define BC_RX_SM_DATA           1u


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
     * Despues TX.
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
     * HISTOGRAMAS PC SM1
     *
     * Índice = PC absoluto de SM1:
     *
     *      0 ... 31
     * ========================================================
     */

    uint32_t pc_valid[32] =
        {0u};


    uint32_t pc_failed[32] =
        {0u};


    /* ========================================================
     * CONTADORES
     * ========================================================
     */

    uint32_t transactions_sent =
        0u;

    uint32_t transactions_ok =
        0u;


    uint32_t status_received =
        0u;

    uint32_t status_correct =
        0u;

    uint32_t status_parity_err =
        0u;

    uint32_t status_unexpected =
        0u;


    uint32_t data_received =
        0u;

    uint32_t data_correct =
        0u;

    uint32_t data_parity_err =
        0u;

    uint32_t data_mismatch =
        0u;

    uint32_t data_before_status =
        0u;

    uint32_t data_extra =
        0u;


    uint32_t response_timeout =
        0u;


    /* ========================================================
     * INICIO
     * ========================================================
     */

    printf(
        "\n"
        "========================================\n"
        "BC - HISTOGRAMA PC SM1\n"
        "RX PIO = 8 MHz\n"
        "TEST = 10000 transacciones RT -> BC\n"
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
         * Limpiar receptor antes del CMD.
         */
        mil1553_rx_clear();


        /* ====================================================
         * TRANSMITIR COMMAND
         * ====================================================
         */

        mil1553_tx_send_word(
            BUS_1553_SYNC_CMD_STATUS,
            cmd
        );


        transactions_sent++;


        /*
         * Esperar que termine fisicamente
         * la transmision del CMD.
         */
        mil1553_tx_wait_idle();


        /*
         * El BC observa tambien su propio CMD.
         *
         * Rearmamos completamente RX
         * para esperar respuesta del RT.
         */
        mil1553_rx_clear();


        /* ====================================================
         * ESTADO DE ESTA TRANSACCION
         * ====================================================
         */

        bool status_seen =
            false;


        bool status_valid =
            false;


        bool message_valid =
            true;


        bool response_complete =
            false;


        bool transaction_ok =
            false;


        bool transaction_timeout =
            false;


        /* ====================================================
         * DIAGNOSTICO PC SM1
         * ====================================================
         */

        bool pc_after_status_valid =
            false;


        uint8_t pc_after_status =
            0u;


        /* ====================================================
         * DATOS LOCALES DE DIAGNOSTICO
         * ====================================================
         */

        uint16_t transaction_status =
            0u;


        bool transaction_status_parity =
            false;


        uint16_t transaction_data[WORD_COUNT] =
        {
            0u,
            0u,
            0u,
            0u
        };


        bool transaction_data_parity[WORD_COUNT] =
        {
            false,
            false,
            false,
            false
        };


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


                status_seen =
                    true;


                status_received++;


                transaction_status =
                    word;


                transaction_status_parity =
                    parity_ok;


                /*
                 * =============================================
                 * FOTO DE SM1 JUSTO DESPUES DEL STATUS
                 * =============================================
                 *
                 * No modificamos la State Machine.
                 *
                 * Solamente leemos el PC.
                 */
                pc_after_status =
                    (uint8_t)
                    pio_sm_get_pc(
                        BC_RX_PIO,
                        BC_RX_SM_DATA
                    );


                pc_after_status_valid =
                    true;


                /*
                 * Paridad del STATUS.
                 */
                if (!parity_ok)
                {
                    status_parity_err++;

                    message_valid =
                        false;

                    continue;
                }


                /*
                 * Valor del STATUS.
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
                 * DATA antes de STATUS.
                 */
                if (!status_valid)
                {
                    data_before_status++;

                    message_valid =
                        false;

                    continue;
                }


                /*
                 * Mas DATA de los esperados.
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
                 * Guardar exactamente lo recibido.
                 */
                transaction_data[
                    data_position
                ] = word;


                transaction_data_parity[
                    data_position
                ] = parity_ok;


                /*
                 * Paridad DATA.
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
                     * Valor y posicion.
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
                 * Ya llegaron cuatro DATA.
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
         * EVALUAR TRANSACCION
         * ====================================================
         */

        if (!response_complete)
        {
            response_timeout++;


            transaction_timeout =
                true;
        }
        else
        {
            if (status_valid &&
                data_position ==
                    WORD_COUNT &&
                message_valid)
            {
                transactions_ok++;


                transaction_ok =
                    true;
            }
        }


        /* ====================================================
         * CLASIFICAR PC POST STATUS
         * ====================================================
         */

        if (pc_after_status_valid)
        {
            if (transaction_ok)
            {
                pc_valid[
                    pc_after_status
                ]++;
            }
            else
            {
                pc_failed[
                    pc_after_status
                ]++;
            }
        }


        /* ====================================================
         * MOSTRAR SOLO TRANSACCIONES BAD
         * ====================================================
         */

        if (!transaction_ok)
        {
            printf(
                "\n"
                "----------------------------------------\n"
                "BC - TRANSACCION BAD\n"
                "----------------------------------------\n"
            );


            /*
             * Mostrar 1 ... 10000.
             */
            printf(
                "Transaccion        : %lu\n",
                (unsigned long)(
                    transaction + 1u
                )
            );


            printf(
                "COMMAND enviado    : 0x%04X\n",
                cmd
            );


            printf(
                "STATUS visto       : %s\n",
                status_seen
                    ? "SI"
                    : "NO"
            );


            if (status_seen)
            {
                printf(
                    "STATUS recibido    : 0x%04X\n",
                    transaction_status
                );


                printf(
                    "STATUS esperado    : 0x%04X\n",
                    expected_status
                );


                printf(
                    "STATUS paridad     : %s\n",
                    transaction_status_parity
                        ? "OK"
                        : "ERROR"
                );
            }


            printf(
                "STATUS valido      : %s\n",
                status_valid
                    ? "SI"
                    : "NO"
            );


            /* ================================================
             * PC DE SM1
             * ================================================
             */

            if (pc_after_status_valid)
            {
                printf(
                    "SM1 PC post STATUS : %u\n",
                    pc_after_status
                );
            }
            else
            {
                printf(
                    "SM1 PC post STATUS : NO CAPTURADO\n"
                );
            }


            /* ================================================
             * DATA
             * ================================================
             */

            printf(
                "DATA recibidos     : %u/%u\n",
                data_position,
                WORD_COUNT
            );


            for (uint8_t i = 0u;
                 i < data_position;
                 i++)
            {
                printf(
                    "DATA%-2u             : 0x%04X"
                    "   esperado 0x%04X"
                    "   paridad %s\n",
                    (unsigned int)(
                        i + 1u
                    ),
                    transaction_data[i],
                    expected_data[i],
                    transaction_data_parity[i]
                        ? "OK"
                        : "ERROR"
                );
            }


            /*
             * Mostrar posiciones faltantes.
             */
            for (uint8_t i = data_position;
                 i < WORD_COUNT;
                 i++)
            {
                printf(
                    "DATA%-2u             : FALTANTE"
                    "   esperado 0x%04X\n",
                    (unsigned int)(
                        i + 1u
                    ),
                    expected_data[i]
                );
            }


            printf(
                "Message valid      : %s\n",
                message_valid
                    ? "SI"
                    : "NO"
            );


            printf(
                "Respuesta completa : %s\n",
                response_complete
                    ? "SI"
                    : "NO"
            );


            printf(
                "Timeout            : %s\n",
                transaction_timeout
                    ? "SI"
                    : "NO"
            );


            printf(
                "----------------------------------------\n"
            );
        }


        /*
         * Separacion entre transacciones.
         */
        sleep_ms(
            INTER_TRANSACTION_MS
        );
    }


    /* ========================================================
     * REPORTE FINAL
     * ========================================================
     */

    printf(
        "\n\n"
        "========================================\n"
        "BC - RESULTADO TEST RT -> BC\n"
        "========================================\n\n"
    );


    printf(
        "TRANSACCIONES ESPERADAS : %lu\n\n",
        (unsigned long)
            TEST_TRANSACTIONS
    );


    /* ========================================================
     * TRANSMISION
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


    /* ========================================================
     * HISTOGRAMA PC SM1
     * ========================================================
     */

    printf(
        "\n"
        "========================================\n"
        "PC SM1 INMEDIATAMENTE DESPUES DEL STATUS\n"
        "========================================\n"
    );


    printf(
        "PC     VALIDAS     FALLIDAS\n"
    );


    printf(
        "---------------------------\n"
    );


    for (uint8_t pc = 0u;
         pc < 32u;
         pc++)
    {
        if ((pc_valid[pc] != 0u) ||
            (pc_failed[pc] != 0u))
        {
            printf(
                "%2u     %7lu     %8lu\n",
                (unsigned int)
                    pc,
                (unsigned long)
                    pc_valid[pc],
                (unsigned long)
                    pc_failed[pc]
            );
        }
    }


    printf(
        "===========================\n"
    );


    /* ========================================================
     * PARAMETROS
     * ========================================================
     */

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