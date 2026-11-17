#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>

#include "pico/stdlib.h"
#include "hardware/clocks.h"

#include "bus/bus.h"
#include "bus/mil1553_rx.h"


/* ============================================================
 * CONFIGURACION
 * ============================================================
 */

#define BM_MAX_DATA_WORDS       32u
#define TEST_TRANSACTIONS       1000u

/*
 * Tiempo maximo para esperar la siguiente palabra
 * dentro de una transaccion.
 */
#define BM_WORD_TIMEOUT_US      1000ull

/*
 * Tiempo sin actividad para considerar terminado
 * el ensayo completo.
 */
#define TEST_END_SILENCE_US     1500000ull


/* ============================================================
 * ESTADOS
 * ============================================================
 */

typedef enum
{
    BM_WAIT_COMMAND = 0,

    BM_BC_TO_RT_DATA,
    BM_BC_TO_RT_STATUS,

    BM_RT_TO_BC_STATUS,
    BM_RT_TO_BC_DATA

} bm_state_t;


/* ============================================================
 * ESTRUCTURA DE TRANSACCION
 * ============================================================
 */

typedef struct
{
    uint16_t command;
    uint16_t status;

    uint16_t data[BM_MAX_DATA_WORDS];

    uint8_t rt_address;
    uint8_t tr;
    uint8_t subaddress;
    uint8_t word_count;

    uint8_t data_received;

    bool command_parity_ok;
    bool status_parity_ok;
    bool data_parity_ok;

    bool valid;

} bm_transaction_t;


/* ============================================================
 * PARIDAD IMPAR
 * ============================================================
 */

static uint8_t compute_odd_parity(uint16_t word)
{
    uint8_t parity = 0u;

    for (uint8_t i = 0u; i < 16u; i++)
    {
        parity ^=
            (uint8_t)(
                (word >> i) & 0x01u
            );
    }

    return (uint8_t)(parity ^ 0x01u);
}


/* ============================================================
 * WORD COUNT
 * ============================================================
 */

static uint8_t decode_word_count(uint8_t wc)
{
    return (wc == 0u) ? 32u : wc;
}


/* ============================================================
 * RESET DE TRANSACCION
 * ============================================================
 */

static void transaction_reset(
    bm_transaction_t *t)
{
    t->command = 0u;
    t->status  = 0u;

    t->rt_address = 0u;
    t->tr         = 0u;
    t->subaddress = 0u;
    t->word_count = 0u;

    t->data_received = 0u;

    t->command_parity_ok = false;
    t->status_parity_ok  = false;
    t->data_parity_ok    = true;

    t->valid = true;

    for (uint8_t i = 0u;
         i < BM_MAX_DATA_WORDS;
         i++)
    {
        t->data[i] = 0u;
    }
}


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
        compute_odd_parity(
            *word
        );


    *parity_ok =
        (
            received_parity ==
            expected_parity
        );
}


/* ============================================================
 * MOSTRAR PORCENTAJE
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
    /* ========================================================
     * CLOCK
     * ========================================================
     */

    set_sys_clock_khz(
        120000,
        true
    );


    stdio_init_all();

    sleep_ms(3000);


    /* ========================================================
     * RECEPTOR MIL-STD-1553
     * ========================================================
     */

    mil1553_rx_init();

    mil1553_rx_clear();


    printf("\n");

    printf(
        "========================================\n"
    );

    printf(
        "BUS MONITOR - TEST PASIVO\n"
    );

    printf(
        "Lectura independiente de FIFO\n"
    );

    printf(
        "========================================\n"
    );


    /* ========================================================
     * ESTADO
     * ========================================================
     */

    bm_state_t state =
        BM_WAIT_COMMAND;


    bm_transaction_t transaction;

    transaction_reset(
        &transaction
    );


    /* ========================================================
     * CONTADORES
     * ========================================================
     */

    uint32_t transactions_started =
        0u;

    uint32_t transactions_valid =
        0u;

    uint32_t transactions_invalid =
        0u;


    uint32_t bc_to_rt_valid =
        0u;

    uint32_t rt_to_bc_valid =
        0u;


    uint32_t cmd_received =
        0u;

    uint32_t status_received =
        0u;

    uint32_t data_received_total =
        0u;


    uint32_t cmd_parity_errors =
        0u;

    uint32_t status_parity_errors =
        0u;

    uint32_t data_parity_errors =
        0u;


    uint32_t sequence_errors =
        0u;


    /* ========================================================
     * CONTROL DEL ENSAYO
     * ========================================================
     */

    bool test_started =
        false;

    bool report_done =
        false;


    uint64_t last_word_us =
        time_us_64();

    uint64_t last_bus_activity_us =
        time_us_64();


    /* ========================================================
     * LOOP PRINCIPAL
     * ========================================================
     */

    while (true)
    {
        uint64_t now_us =
            time_us_64();


        /* ====================================================
         * ACTIVIDAD GENERAL
         * ====================================================
         */

        if (mil1553_rx_available())
        {
            test_started =
                true;

            last_bus_activity_us =
                now_us;
        }


        /* ====================================================
         * ESPERANDO COMMAND
         * ====================================================
         */

        if (state ==
            BM_WAIT_COMMAND)
        {
            if (mil1553_rx_cmd_status_available())
            {
                uint32_t event =
                    mil1553_rx_get_cmd_status();


                uint16_t word;
                bool parity_ok;


                decode_rx_event(
                    event,
                    &word,
                    &parity_ok
                );


                /*
                 * En esta implementacion no
                 * procesamos Mode Codes.
                 *
                 * Subaddress 0 y 31 se descartan.
                 *
                 * Esto tambien evita interpretar
                 * STATUS = 0xA000 como Command.
                 */
                uint8_t subaddress =
                    BUS_1553_CMD_SUB(
                        word
                    );


                if ((subaddress == 0u) ||
                    (subaddress == 31u))
                {
                    continue;
                }


                transaction_reset(
                    &transaction
                );


                transactions_started++;

                cmd_received++;


                transaction.command =
                    word;


                transaction.command_parity_ok =
                    parity_ok;


                if (!parity_ok)
                {
                    cmd_parity_errors++;

                    transactions_invalid++;

                    continue;
                }


                transaction.rt_address =
                    BUS_1553_CMD_RT(
                        word
                    );


                transaction.tr =
                    BUS_1553_CMD_TR(
                        word
                    );


                transaction.subaddress =
                    subaddress;


                transaction.word_count =
                    decode_word_count(
                        BUS_1553_CMD_WC(
                            word
                        )
                    );


                last_word_us =
                    now_us;


                /*
                 * Elegir secuencia segun T/R.
                 */
                if (transaction.tr ==
                    BUS_1553_TR_BC_TO_RT)
                {
                    state =
                        BM_BC_TO_RT_DATA;
                }
                else
                {
                    state =
                        BM_RT_TO_BC_STATUS;
                }
            }
        }


        /* ====================================================
         * RT -> BC : STATUS
         * ====================================================
         */

        else if (state ==
                 BM_RT_TO_BC_STATUS)
        {
            if (mil1553_rx_cmd_status_available())
            {
                uint32_t event =
                    mil1553_rx_get_cmd_status();


                uint16_t word;
                bool parity_ok;


                decode_rx_event(
                    event,
                    &word,
                    &parity_ok
                );


                status_received++;


                transaction.status =
                    word;


                transaction.status_parity_ok =
                    parity_ok;


                if (!parity_ok)
                {
                    status_parity_errors++;

                    transaction.valid =
                        false;
                }


                /*
                 * El Status debe corresponder
                 * al mismo RT del Command.
                 */
                if (BUS_1553_STATUS_RT(
                        word)
                    !=
                    transaction.rt_address)
                {
                    transaction.valid =
                        false;
                }


                last_word_us =
                    now_us;


                /*
                 * IMPORTANTE:
                 *
                 * No limpiar ni rearmar SM DATA aca.
                 *
                 * El primer DATA puede estar llegando
                 * inmediatamente despues del STATUS.
                 */
                state =
                    BM_RT_TO_BC_DATA;
            }
        }


        /* ====================================================
         * RT -> BC : DATA
         * ====================================================
         */

        else if (state ==
                 BM_RT_TO_BC_DATA)
        {
            if (mil1553_rx_data_available())
            {
                uint32_t event =
                    mil1553_rx_get_data();


                uint16_t word;
                bool parity_ok;


                decode_rx_event(
                    event,
                    &word,
                    &parity_ok
                );


                data_received_total++;


                if (transaction.data_received <
                    transaction.word_count)
                {
                    transaction.data[
                        transaction.data_received
                    ] = word;


                    transaction.data_received++;
                }


                if (!parity_ok)
                {
                    data_parity_errors++;

                    transaction.data_parity_ok =
                        false;

                    transaction.valid =
                        false;
                }


                last_word_us =
                    now_us;


                /*
                 * Fin de RT -> BC.
                 */
                if (transaction.data_received >=
                    transaction.word_count)
                {
                    if (transaction.valid)
                    {
                        transactions_valid++;

                        rt_to_bc_valid++;
                    }
                    else
                    {
                        transactions_invalid++;
                    }


                    /*
                     * NO limpiamos el receptor aqui.
                     *
                     * Simplemente volvemos a esperar
                     * el siguiente Command.
                     */
                    transaction_reset(
                        &transaction
                    );


                    state =
                        BM_WAIT_COMMAND;


                    last_word_us =
                        time_us_64();
                }
            }
        }


        /* ====================================================
         * BC -> RT : DATA
         * ====================================================
         */

        else if (state ==
                 BM_BC_TO_RT_DATA)
        {
            if (mil1553_rx_data_available())
            {
                uint32_t event =
                    mil1553_rx_get_data();


                uint16_t word;
                bool parity_ok;


                decode_rx_event(
                    event,
                    &word,
                    &parity_ok
                );


                data_received_total++;


                if (transaction.data_received <
                    transaction.word_count)
                {
                    transaction.data[
                        transaction.data_received
                    ] = word;


                    transaction.data_received++;
                }


                if (!parity_ok)
                {
                    data_parity_errors++;

                    transaction.data_parity_ok =
                        false;

                    transaction.valid =
                        false;
                }


                last_word_us =
                    now_us;


                if (transaction.data_received >=
                    transaction.word_count)
                {
                    state =
                        BM_BC_TO_RT_STATUS;
                }
            }
        }


        /* ====================================================
         * BC -> RT : STATUS
         * ====================================================
         */

        else if (state ==
                 BM_BC_TO_RT_STATUS)
        {
            if (mil1553_rx_cmd_status_available())
            {
                uint32_t event =
                    mil1553_rx_get_cmd_status();


                uint16_t word;
                bool parity_ok;


                decode_rx_event(
                    event,
                    &word,
                    &parity_ok
                );


                status_received++;


                transaction.status =
                    word;


                transaction.status_parity_ok =
                    parity_ok;


                if (!parity_ok)
                {
                    status_parity_errors++;

                    transaction.valid =
                        false;
                }


                if (BUS_1553_STATUS_RT(
                        word)
                    !=
                    transaction.rt_address)
                {
                    transaction.valid =
                        false;
                }


                last_word_us =
                    now_us;


                /*
                 * Fin de BC -> RT.
                 */
                if (transaction.valid)
                {
                    transactions_valid++;

                    bc_to_rt_valid++;
                }
                else
                {
                    transactions_invalid++;
                }


                transaction_reset(
                    &transaction
                );


                state =
                    BM_WAIT_COMMAND;


                last_word_us =
                    time_us_64();
            }
        }


        /* ====================================================
         * TIMEOUT DE TRANSACCION
         * ====================================================
         */

        if ((state != BM_WAIT_COMMAND) &&
            ((now_us - last_word_us) >=
             BM_WORD_TIMEOUT_US))
        {
            transactions_invalid++;

            sequence_errors++;


            /* ================================================
             * DIAGNOSTICO
             * ================================================
             */

            printf("\n");

            printf(
                "----------------------------------------\n"
            );

            printf(
                "BM - TIMEOUT DE TRANSACCION\n"
            );

            printf(
                "----------------------------------------\n"
            );


            printf(
                "Transaccion        : %lu\n",
                (unsigned long)
                    transactions_started
            );


            printf(
                "COMMAND            : 0x%04X\n",
                transaction.command
            );


            printf(
                "Direccion          : %s\n",
                (transaction.tr ==
                 BUS_1553_TR_BC_TO_RT)
                ?
                "BC -> RT"
                :
                "RT -> BC"
            );


            printf(
                "Word Count         : %u\n",
                transaction.word_count
            );


            printf(
                "DATA recibidos     : %u/%u\n",
                transaction.data_received,
                transaction.word_count
            );


            for (uint8_t i = 0u;
                 i <
                 transaction.data_received;
                 i++)
            {
                printf(
                    "DATA%-2u            : 0x%04X\n",
                    (unsigned int)(
                        i + 1u
                    ),
                    transaction.data[i]
                );
            }


            printf(
                "Estado al timeout  : %u\n",
                (unsigned int)
                    state
            );


            printf(
                "----------------------------------------\n"
            );


            /*
             * En caso de error si limpiamos
             * completamente el receptor.
             *
             * Hay ~100 ms antes de la siguiente
             * transaccion en este ensayo.
             */
            mil1553_rx_clear();


            transaction_reset(
                &transaction
            );


            state =
                BM_WAIT_COMMAND;


            last_word_us =
                time_us_64();

            last_bus_activity_us =
                last_word_us;
        }


        /* ====================================================
         * INFORME FINAL
         * ====================================================
         */

        if (test_started &&
            !report_done &&
            (state == BM_WAIT_COMMAND))
        {
            now_us =
                time_us_64();


            if ((now_us -
                 last_bus_activity_us)
                >=
                TEST_END_SILENCE_US)
            {
                report_done =
                    true;


                printf("\n\n");


                printf(
                    "========================================\n"
                );

                printf(
                    "BM - RESULTADO TEST PASIVO\n"
                );

                printf(
                    "========================================\n\n"
                );


                printf(
                    "TRANSACCIONES ESPERADAS : %lu\n\n",
                    (unsigned long)
                        TEST_TRANSACTIONS
                );


                /* ============================================
                 * TRANSACCIONES
                 * ============================================
                 */

                printf(
                    "--- TRANSACCIONES ---\n"
                );


                printf(
                    "Transacciones iniciadas : %lu\n",
                    (unsigned long)
                        transactions_started
                );


                printf(
                    "Transacciones validas   : %lu\n",
                    (unsigned long)
                        transactions_valid
                );


                printf(
                    "Transacciones invalidas : %lu\n\n",
                    (unsigned long)
                        transactions_invalid
                );


                /* ============================================
                 * DIRECCION
                 * ============================================
                 */

                printf(
                    "--- DIRECCION ---\n"
                );


                printf(
                    "BC -> RT validas        : %lu\n",
                    (unsigned long)
                        bc_to_rt_valid
                );


                printf(
                    "RT -> BC validas        : %lu\n\n",
                    (unsigned long)
                        rt_to_bc_valid
                );


                /* ============================================
                 * PALABRAS
                 * ============================================
                 */

                printf(
                    "--- PALABRAS ---\n"
                );


                printf(
                    "CMD recibidos           : %lu\n",
                    (unsigned long)
                        cmd_received
                );


                printf(
                    "STATUS recibidos        : %lu\n",
                    (unsigned long)
                        status_received
                );


                printf(
                    "DATA recibidos          : %lu\n\n",
                    (unsigned long)
                        data_received_total
                );


                /* ============================================
                 * ERRORES
                 * ============================================
                 */

                printf(
                    "--- ERRORES ---\n"
                );


                printf(
                    "CMD error paridad       : %lu\n",
                    (unsigned long)
                        cmd_parity_errors
                );


                printf(
                    "STATUS error paridad    : %lu\n",
                    (unsigned long)
                        status_parity_errors
                );


                printf(
                    "DATA error paridad      : %lu\n",
                    (unsigned long)
                        data_parity_errors
                );


                printf(
                    "Errores de secuencia    : %lu\n\n",
                    (unsigned long)
                        sequence_errors
                );


                /* ============================================
                 * PORCENTAJE
                 * ============================================
                 */

                printf(
                    "--- TASAS DE EXITO ---\n"
                );


                print_rate(
                    "Transacciones capturadas",
                    transactions_valid,
                    TEST_TRANSACTIONS
                );


                printf("\n");


                printf(
                    "========================================\n"
                );

                printf(
                    "FIN DEL INFORME BM\n"
                );

                printf(
                    "========================================\n"
                );
            }
        }


        tight_loop_contents();
    }
}