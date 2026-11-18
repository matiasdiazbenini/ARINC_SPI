#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>

#include "pico/stdlib.h"

#include "hardware/clocks.h"
#include "hardware/pio.h"
#include "hardware/gpio.h"

#include "bus/bus.h"
#include "bus/mil1553_rx.h"


/* ============================================================
 * CONFIGURACION
 * ============================================================
 */

#define BM_MAX_DATA_WORDS       32u

#define TEST_TRANSACTIONS       10000u

#define BM_WORD_TIMEOUT_US      1000ull

#define TEST_END_SILENCE_US     1500000ull


/* ============================================================
 * PIO DEL BM
 *
 * PIO1
 *
 * SM0 -> P -> CMD / STATUS
 * SM1 -> N -> DATA
 * ============================================================
 */

#define BM_RX_PIO               pio1
#define BM_RX_SM_DATA           1u


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
 * TRANSACCION
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
 * DIAGNOSTICO SM1
 * ============================================================
 */

typedef struct
{
    uint8_t pc_after_status;

    uint8_t fifo_after_status;

    uint8_t gpio_n_after_status;


    uint8_t last_pc;

    uint32_t pc_changes;

    uint32_t pc_seen_mask;


    bool active;

} bm_sm1_diag_t;


/* ============================================================
 * RESET DIAGNOSTICO
 * ============================================================
 */

static void sm1_diag_reset(
    bm_sm1_diag_t *d)
{
    d->pc_after_status = 0u;

    d->fifo_after_status = 0u;

    d->gpio_n_after_status = 0u;


    d->last_pc = 0u;

    d->pc_changes = 0u;

    d->pc_seen_mask = 0u;


    d->active = false;
}


/* ============================================================
 * FOTO DE SM1 DESPUES DEL STATUS
 * ============================================================
 */

static void sm1_diag_start(
    bm_sm1_diag_t *d)
{
    uint8_t pc =
        (uint8_t)
        pio_sm_get_pc(
            BM_RX_PIO,
            BM_RX_SM_DATA
        );


    d->pc_after_status =
        pc;


    d->fifo_after_status =
        (uint8_t)
        pio_sm_get_rx_fifo_level(
            BM_RX_PIO,
            BM_RX_SM_DATA
        );


    d->gpio_n_after_status =
        gpio_get(
            BUS_PIN_N
        )
            ? 1u
            : 0u;


    d->last_pc =
        pc;


    d->pc_changes =
        0u;


    d->pc_seen_mask =
        (1u << pc);


    d->active =
        true;
}


/* ============================================================
 * SEGUIMIENTO PC SM1
 * ============================================================
 */

static void sm1_diag_update(
    bm_sm1_diag_t *d)
{
    if (!d->active)
    {
        return;
    }


    uint8_t pc =
        (uint8_t)
        pio_sm_get_pc(
            BM_RX_PIO,
            BM_RX_SM_DATA
        );


    d->pc_seen_mask |=
        (1u << pc);


    if (pc != d->last_pc)
    {
        d->pc_changes++;

        d->last_pc =
            pc;
    }
}


/* ============================================================
 * MOSTRAR PCs VISTOS
 * ============================================================
 */

static void print_seen_pcs(
    uint32_t mask)
{
    printf(
        "SM1 PCs vistos      : "
    );


    bool first =
        true;


    for (uint8_t pc = 0u;
         pc < 32u;
         pc++)
    {
        if (mask &
            (1u << pc))
        {
            if (!first)
            {
                printf(",");
            }


            printf(
                "%u",
                pc
            );


            first =
                false;
        }
    }


    if (first)
    {
        printf(
            "ninguno"
        );
    }


    printf(
        "\n"
    );
}


/* ============================================================
 * PARIDAD IMPAR
 * ============================================================
 */

static uint8_t compute_odd_parity(
    uint16_t word)
{
    uint8_t parity =
        0u;


    for (uint8_t i = 0u;
         i < 16u;
         i++)
    {
        parity ^=
            (uint8_t)(
                (word >> i) &
                0x01u
            );
    }


    return (uint8_t)(
        parity ^
        0x01u
    );
}


/* ============================================================
 * WORD COUNT
 * ============================================================
 */

static uint8_t decode_word_count(
    uint8_t wc)
{
    return (wc == 0u)
        ? 32u
        : wc;
}


/* ============================================================
 * RESET TRANSACCION
 * ============================================================
 */

static void transaction_reset(
    bm_transaction_t *t)
{
    t->command =
        0u;

    t->status =
        0u;


    t->rt_address =
        0u;

    t->tr =
        0u;

    t->subaddress =
        0u;

    t->word_count =
        0u;


    t->data_received =
        0u;


    t->command_parity_ok =
        false;

    t->status_parity_ok =
        false;

    t->data_parity_ok =
        true;


    t->valid =
        true;


    for (uint8_t i = 0u;
         i < BM_MAX_DATA_WORDS;
         i++)
    {
        t->data[i] =
            0u;
    }
}


/* ============================================================
 * DECODIFICAR RX
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

    set_sys_clock_khz(
        120000,
        true
    );


    stdio_init_all();

    sleep_ms(3000);


    /* ========================================================
     * RECEPTOR
     * ========================================================
     */

    mil1553_rx_init();

    mil1553_rx_clear();


    printf(
        "\n"
        "========================================\n"
        "BUS MONITOR - RX DATA GATE POR IRQ\n"
        "RX PIO = 8 MHz\n"
        "TEST = %lu transacciones\n"
        "========================================\n",
        (unsigned long)
            TEST_TRANSACTIONS
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


    bm_sm1_diag_t sm1_diag;

    sm1_diag_reset(
        &sm1_diag
    );


    /* ========================================================
     * HISTOGRAMAS PC
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
     * CONTROL DEL TEST
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
                 * No procesamos Mode Codes.
                 *
                 * STATUS = 0xA000 tambien queda
                 * descartado gracias a SUB = 0.
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


                sm1_diag_reset(
                    &sm1_diag
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


                    /*
                     * Dejar nuevamente SM1 esperando
                     * la proxima IRQ valida.
                     */
                    mil1553_rx_data_rearm();


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


                /* ============================================
                 * BC -> RT
                 * ============================================
                 *
                 * Al terminar COMMAND, SM0 genera IRQ 0.
                 *
                 * Esa IRQ es justamente la que queremos:
                 *
                 *      COMMAND
                 *      DATA
                 *      STATUS
                 *
                 * Por lo tanto NO rearmamos SM1.
                 * ============================================
                 */

                if (transaction.tr ==
                    BUS_1553_TR_BC_TO_RT)
                {
                    state =
                        BM_BC_TO_RT_DATA;
                }


                /* ============================================
                 * RT -> BC
                 * ============================================
                 *
                 * Secuencia:
                 *
                 *      COMMAND
                 *      espera RT
                 *      STATUS
                 *      DATA
                 *
                 * La IRQ generada por COMMAND NO nos sirve.
                 *
                 * Rearmamos SM1 ahora:
                 *
                 *      - descarta IRQ de COMMAND,
                 *      - limpia cualquier falsa adquisicion,
                 *      - queda esperando IRQ de STATUS.
                 *
                 * Cuando SM0 complete STATUS:
                 *
                 *      IRQ 0
                 *
                 * despierta SM1 directamente desde PIO.
                 * ============================================
                 */

                else
                {
                    mil1553_rx_data_rearm();


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
                 * NO rearmar SM1 aqui.
                 *
                 * SM0 ya genero IRQ 0 al completar STATUS.
                 *
                 * Esa IRQ despierta directamente a SM1.
                 */


                /*
                 * Foto diagnostica.
                 *
                 * NOTA:
                 *
                 * Los numeros de PC ya NO son comparables
                 * directamente con el PIO anterior porque
                 * ahora el programa tiene instrucciones extra.
                 */
                sm1_diag_start(
                    &sm1_diag
                );


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
            sm1_diag_update(
                &sm1_diag
            );


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
                 * FIN RT -> BC
                 */
                if (transaction.data_received >=
                    transaction.word_count)
                {
                    /*
                     * Histograma.
                     */
                    if (sm1_diag.active)
                    {
                        if (transaction.valid)
                        {
                            pc_valid[
                                sm1_diag.pc_after_status
                            ]++;
                        }
                        else
                        {
                            pc_failed[
                                sm1_diag.pc_after_status
                            ]++;
                        }
                    }


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
                     * Dejar SM1 esperando
                     * la siguiente IRQ.
                     */
                    mil1553_rx_data_rearm();


                    transaction_reset(
                        &transaction
                    );


                    sm1_diag_reset(
                        &sm1_diag
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


                if (transaction.valid)
                {
                    transactions_valid++;

                    bc_to_rt_valid++;
                }
                else
                {
                    transactions_invalid++;
                }


                /*
                 * La transaccion termino.
                 *
                 * El STATUS acaba de generar IRQ 0,
                 * pero en BC -> RT esa IRQ NO debe
                 * comenzar un nuevo bloque DATA.
                 *
                 * Rearm limpia esa IRQ residual.
                 */
                mil1553_rx_data_rearm();


                transaction_reset(
                    &transaction
                );


                sm1_diag_reset(
                    &sm1_diag
                );


                state =
                    BM_WAIT_COMMAND;


                last_word_us =
                    time_us_64();
            }
        }


        /* ====================================================
         * TIMEOUT
         * ====================================================
         */

        if ((state != BM_WAIT_COMMAND) &&
            ((now_us - last_word_us) >=
             BM_WORD_TIMEOUT_US))
        {
            transactions_invalid++;

            sequence_errors++;


            /*
             * Clasificar PC como falla
             * solamente si ya pasamos por STATUS.
             */
            if (sm1_diag.active)
            {
                pc_failed[
                    sm1_diag.pc_after_status
                ]++;
            }


            uint8_t pc_timeout =
                (uint8_t)
                pio_sm_get_pc(
                    BM_RX_PIO,
                    BM_RX_SM_DATA
                );


            uint8_t fifo_timeout =
                (uint8_t)
                pio_sm_get_rx_fifo_level(
                    BM_RX_PIO,
                    BM_RX_SM_DATA
                );


            uint8_t gpio_n_timeout =
                gpio_get(
                    BUS_PIN_N
                )
                    ? 1u
                    : 0u;


            printf(
                "\n"
                "----------------------------------------\n"
                "BM - TIMEOUT DE TRANSACCION\n"
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
                    "DATA%-2u             : 0x%04X\n",
                    (unsigned int)(
                        i + 1u
                    ),
                    transaction.data[i]
                );
            }


            printf(
                "Estado BM          : %u\n",
                (unsigned int)
                    state
            );


            /* ================================================
             * DIAGNOSTICO SM1
             * ================================================
             */

            if (sm1_diag.active)
            {
                printf(
                    "\n"
                    "--- SM1 DESPUES DEL STATUS ---\n"
                );


                printf(
                    "SM1 PC post STATUS : %u\n",
                    sm1_diag.pc_after_status
                );


                printf(
                    "SM1 FIFO post STATUS: %u\n",
                    sm1_diag.fifo_after_status
                );


                printf(
                    "GPIO N post STATUS : %u\n",
                    sm1_diag.gpio_n_after_status
                );
            }
            else
            {
                printf(
                    "\n"
                    "--- SM1 DESPUES DEL STATUS ---\n"
                    "No capturado\n"
                );
            }


            printf(
                "\n"
                "--- SM1 AL TIMEOUT ---\n"
            );


            printf(
                "SM1 PC timeout     : %u\n",
                pc_timeout
            );


            printf(
                "SM1 FIFO timeout   : %u\n",
                fifo_timeout
            );


            printf(
                "GPIO N timeout     : %u\n",
                gpio_n_timeout
            );


            printf(
                "SM1 cambios PC     : %lu\n",
                (unsigned long)
                    sm1_diag.pc_changes
            );


            printf(
                "SM1 PC mask        : 0x%08lX\n",
                (unsigned long)
                    sm1_diag.pc_seen_mask
            );


            print_seen_pcs(
                sm1_diag.pc_seen_mask
            );


            printf(
                "----------------------------------------\n"
            );


            /*
             * Ante error:
             *
             * reset completo de ambas SM e IRQ.
             */
            mil1553_rx_clear();


            transaction_reset(
                &transaction
            );


            sm1_diag_reset(
                &sm1_diag
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


                printf(
                    "\n\n"
                    "========================================\n"
                    "BM - RESULTADO TEST PASIVO\n"
                    "RX PIO = 8 MHz\n"
                    "DATA GATE = IRQ PIO\n"
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
                 * TASAS
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


                /* ============================================
                 * HISTOGRAMA PC
                 * ============================================
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


                printf(
                    "\n"
                    "========================================\n"
                    "FIN DEL INFORME BM\n"
                    "========================================\n"
                );
            }
        }


        tight_loop_contents();
    }
}