#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>

#include "pico/stdlib.h"

#include "hardware/clocks.h"
#include "hardware/pio.h"
#include "hardware/regs/pio.h"
#include "hardware/gpio.h"

#include "bus/bus.h"
#include "bus/mil1553_rx.h"


/* ============================================================
 * CONFIGURACION
 * ============================================================
 */

#define TEST_TRANSACTIONS           1000u
#define BM_WORD_TIMEOUT_US          1000ull
#define TEST_END_SILENCE_US         1500000ull
#define PRINT_FIRST_TRANSACTIONS    6u
#define BM_MAX_DATA_WORDS           32u
#define EDGE_MONITOR_WINDOW_US      200ull
#define SM0_WARMUP_COUNT            5u
#define BM_DIAG_MAX_SAVED           64u


/* ============================================================
 * PIO / GPIO
 * ============================================================
 */

#define BM_RX_PIO                   pio1
#define BM_RX_SM_CMD                0u
#define BM_RX_SM_DATA_BC_TO_RT      1u
#define BM_RX_SM_DATA_RT_TO_BC      2u


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
    uint32_t index;

    uint16_t command;
    bool command_parity_ok;

    uint8_t rt_address;
    uint8_t tr;
    uint8_t subaddress;
    uint8_t word_count;

    uint16_t data[BM_MAX_DATA_WORDS];
    bool data_parity_ok[BM_MAX_DATA_WORDS];

    uint8_t data_count;

    uint16_t status;

    bool status_seen;
    bool status_parity_ok;

    uint8_t status_rt;

    bool status_message_error;
    bool sequence_ok;
    bool valid;

} mil1553_transaction_t;


/* ============================================================
 * ESTADISTICAS
 * ============================================================
 */

typedef struct
{
    uint32_t transactions_started;
    uint32_t transactions_completed;
    uint32_t transactions_valid;
    uint32_t transactions_invalid;

    uint32_t bc_to_rt_started;
    uint32_t bc_to_rt_valid;

    uint32_t rt_to_bc_started;
    uint32_t rt_to_bc_valid;

    uint32_t command_parity_errors;
    uint32_t data_parity_errors;
    uint32_t status_parity_errors;

    uint32_t status_rt_mismatch;
    uint32_t status_message_error;

    uint32_t data_words_received;

    uint32_t sequence_errors;
    uint32_t timeout_errors;
    uint32_t unsupported_mode_codes;

} bm_stats_t;


/* ============================================================
 * DIAGNOSTICO FISICO GP2
 * ============================================================
 */

typedef struct
{
    uint32_t edges;

    uint8_t initial_level;
    uint8_t final_level;

    uint8_t pc_before_rearm;
    uint8_t pc_after_rearm;
    uint8_t pc_after_window;

    uint8_t fifo_after_window;

} edge_diag_t;


/* ============================================================
 * DIAGNOSTICO DATA
 * ============================================================
 *
 * SM1 = BC -> RT
 * SM2 = RT -> BC
 *
 * El diagnostico no modifica el programa PIO.
 * ============================================================
 */

typedef struct
{
    uint64_t phase_start_us;
    uint64_t last_poll_us;
    uint64_t last_read_us;

    uint32_t max_poll_gap_us;
    uint32_t max_read_gap_us;
    uint32_t first_read_us;

    uint8_t max_fifo;

    uint8_t sm;
    uint8_t reads;
    uint8_t max_read_gap_at;

    uint8_t pc_end_data;
    uint8_t fifo_end_data;

    uint8_t pc_timeout;
    uint8_t fifo_timeout;

    bm_state_t timeout_state;

    bool phase_started;
    bool data_snapshot;

    bool rxstall;
    bool timeout;
    bool rxstall_at_timeout;

} data_diag_t;


/* ============================================================
 * REGISTROS GUARDADOS
 * ============================================================
 */

typedef struct
{
    mil1553_transaction_t transaction;

    data_diag_t diag;

    bool complete;

} saved_diag_t;


/* ============================================================
 * MEMORIA ESTATICA
 * ============================================================
 */

static data_diag_t active_diag;

static saved_diag_t saved_diag[BM_DIAG_MAX_SAVED];

static uint32_t saved_count = 0u;
static uint32_t saved_dropped = 0u;

static uint32_t diag_stall_transactions = 0u;

static uint32_t diag_max_poll_gap_us = 0u;
static uint32_t diag_max_read_gap_us = 0u;

static uint8_t diag_max_fifo = 0u;


/* ============================================================
 * PATRON DATA ESPERADO
 * ============================================================
 *
 * Utilizado exclusivamente para el diagnostico.
 *
 * No modifica la logica de recepcion.
 * ============================================================
 */

static const uint16_t diag_expected_data[32] =
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
 * RXSTALL
 * ============================================================
 *
 * FDEBUG.RXSTALL:
 *
 * Indica que una State Machine se bloqueo intentando
 * escribir en su FIFO RX.
 *
 * Es un flag sticky.
 *
 * Se limpia escribiendo un 1 en el bit correspondiente.
 *
 * IMPORTANTE:
 * No utilizar |= sobre FDEBUG.
 * ============================================================
 */

static inline uint32_t data_diag_stall_mask(uint sm)
{
    return 1u << (PIO_FDEBUG_RXSTALL_LSB + sm);
}


static inline void data_diag_clear_flags(void)
{
    BM_RX_PIO->fdebug =
        data_diag_stall_mask(BM_RX_SM_DATA_BC_TO_RT) |
        data_diag_stall_mask(BM_RX_SM_DATA_RT_TO_BC);
}


/* ============================================================
 * INICIAR DIAGNOSTICO
 * ============================================================
 */

static inline void data_diag_begin(uint8_t tr)
{
    active_diag = (data_diag_t){0};

    active_diag.sm =
        (tr == BUS_1553_TR_BC_TO_RT)
        ?
        BM_RX_SM_DATA_BC_TO_RT
        :
        BM_RX_SM_DATA_RT_TO_BC;
}


/* ============================================================
 * INICIAR FASE DATA
 * ============================================================
 */

static inline void data_diag_start_phase(uint64_t now_us)
{
    active_diag.phase_started = true;

    active_diag.phase_start_us = now_us;

    active_diag.last_poll_us = now_us;
}


/* ============================================================
 * DIAGNOSTICO POLLING
 * ============================================================
 *
 * Utilizamos el timestamp que ya obtiene el loop principal.
 *
 * No agregamos otra lectura del timer.
 * ============================================================
 */

static inline void data_diag_poll(uint64_t now_us)
{
    if (!active_diag.phase_started)
    {
        data_diag_start_phase(now_us);
    }

    uint32_t gap =
        (uint32_t)(
            now_us -
            active_diag.last_poll_us
        );

    if (gap > active_diag.max_poll_gap_us)
    {
        active_diag.max_poll_gap_us = gap;
    }

    active_diag.last_poll_us = now_us;
}


/* ============================================================
 * DIAGNOSTICO ANTES DE LEER FIFO
 * ============================================================
 */

static inline void data_diag_read(uint64_t now_us)
{
    uint8_t level =
        (uint8_t)
        pio_sm_get_rx_fifo_level(
            BM_RX_PIO,
            active_diag.sm
        );

    if (level > active_diag.max_fifo)
    {
        active_diag.max_fifo = level;
    }

    if (active_diag.reads == 0u)
    {
        active_diag.first_read_us =
            (uint32_t)(
                now_us -
                active_diag.phase_start_us
            );
    }
    else
    {
        uint32_t gap =
            (uint32_t)(
                now_us -
                active_diag.last_read_us
            );

        if (gap > active_diag.max_read_gap_us)
        {
            active_diag.max_read_gap_us = gap;

            active_diag.max_read_gap_at =
                active_diag.reads + 1u;
        }
    }

    active_diag.last_read_us = now_us;

    active_diag.reads++;
}


/* ============================================================
 * CAPTURAR ESTADO DE SM DATA
 * ============================================================
 */

static inline void data_diag_snapshot(void)
{
    active_diag.rxstall =
        (
            BM_RX_PIO->fdebug &
            data_diag_stall_mask(active_diag.sm)
        ) != 0u;

    active_diag.pc_end_data =
        (uint8_t)
        pio_sm_get_pc(
            BM_RX_PIO,
            active_diag.sm
        );

    active_diag.fifo_end_data =
        (uint8_t)
        pio_sm_get_rx_fifo_level(
            BM_RX_PIO,
            active_diag.sm
        );

    if (active_diag.fifo_end_data >
        active_diag.max_fifo)
    {
        active_diag.max_fifo =
            active_diag.fifo_end_data;
    }

    active_diag.data_snapshot = true;
}


/* ============================================================
 * DIAGNOSTICO AL TIMEOUT
 * ============================================================
 */

static inline void data_diag_timeout(bm_state_t state)
{
    active_diag.timeout = true;

    active_diag.timeout_state = state;

    active_diag.pc_timeout =
        (uint8_t)
        pio_sm_get_pc(
            BM_RX_PIO,
            active_diag.sm
        );

    active_diag.fifo_timeout =
        (uint8_t)
        pio_sm_get_rx_fifo_level(
            BM_RX_PIO,
            active_diag.sm
        );

    active_diag.rxstall_at_timeout =
        (
            BM_RX_PIO->fdebug &
            data_diag_stall_mask(active_diag.sm)
        ) != 0u;

    if (!active_diag.data_snapshot &&
        active_diag.phase_started)
    {
        data_diag_snapshot();
    }
}


/* ============================================================
 * NOMBRE DEL ESTADO
 * ============================================================
 */

static const char *diag_state_name(bm_state_t state)
{
    switch (state)
    {
        case BM_WAIT_COMMAND:
            return "WAIT_COMMAND";

        case BM_BC_TO_RT_DATA:
            return "BC_TO_RT_DATA";

        case BM_BC_TO_RT_STATUS:
            return "BC_TO_RT_STATUS";

        case BM_RT_TO_BC_STATUS:
            return "RT_TO_BC_STATUS";

        case BM_RT_TO_BC_DATA:
            return "RT_TO_BC_DATA";

        default:
            return "DESCONOCIDO";
    }
}


/* ============================================================
 * IDENTIFICAR DATA FALTANTES
 * ============================================================
 *
 * Este analisis se ejecuta al finalizar el ensayo.
 *
 * No consume tiempo durante la recepcion.
 * ============================================================
 */

static void print_missing_data(
    const mil1553_transaction_t *t)
{
    if (t->command != 0xA240u ||
        t->word_count != 32u)
    {
        printf(
            "Comparacion de patron : NO APLICA\n"
        );

        return;
    }

    uint32_t seen = 0u;
    uint32_t duplicate = 0u;
    uint32_t unexpected = 0u;
    uint32_t order_errors = 0u;

    int last = -1;

    for (uint8_t i = 0u;
         i < t->data_count;
         i++)
    {
        int found = -1;

        for (int j = 0; j < 32; j++)
        {
            if (t->data[i] ==
                diag_expected_data[j])
            {
                found = j;

                break;
            }
        }

        if (found < 0)
        {
            unexpected++;

            printf(
                "  RX%u inesperado: 0x%04X\n",
                i + 1u,
                t->data[i]
            );

            continue;
        }

        uint32_t bit =
            1u << (unsigned)found;

        if (seen & bit)
        {
            duplicate++;
        }

        if (found <= last)
        {
            order_errors++;
        }

        seen |= bit;

        last = found;
    }

    printf(
        "DATA del patron no observados:\n"
    );

    if (seen == UINT32_MAX)
    {
        printf("  Ninguno\n");
    }

    for (uint8_t i = 0u; i < 32u; i++)
    {
        if (!(seen & (1u << i)))
        {
            printf(
                "  DATA%u = 0x%04X\n",
                i + 1u,
                diag_expected_data[i]
            );
        }
    }

    printf(
        "Duplicados            : %lu\n",
        (unsigned long)duplicate
    );

    printf(
        "Fuera del patron      : %lu\n",
        (unsigned long)unexpected
    );

    printf(
        "Orden no creciente    : %lu\n",
        (unsigned long)order_errors
    );
}


/* ============================================================
 * MOSTRAR DIAGNOSTICO DATA
 * ============================================================
 */

static void print_data_diag(
    const saved_diag_t *record)
{
    const mil1553_transaction_t *t =
        &record->transaction;

    const data_diag_t *d =
        &record->diag;

    printf(
        "\n"
        "--- DIAGNOSTICO DATA TX %lu ---\n",
        (unsigned long)t->index
    );

    printf(
        "DATA recibidos        : %u/%u\n",
        t->data_count,
        t->word_count
    );

    printf(
        "DATA faltantes (cantidad): %u\n",
        t->word_count > t->data_count
        ?
        (unsigned)(
            t->word_count - t->data_count
        )
        :
        0u
    );

    printf(
        "SM DATA               : %u\n",
        d->sm
    );

    printf(
        "FIFO maxima observada : %u / 8\n",
        d->max_fifo
    );

    printf(
        "Inicio fase a DATA1   : %lu us\n",
        (unsigned long)d->first_read_us
    );

    printf(
        "Max intervalo polling: %lu us\n",
        (unsigned long)d->max_poll_gap_us
    );

    printf(
        "Max intervalo lecturas: %lu us (RX%u)\n",
        (unsigned long)d->max_read_gap_us,
        d->max_read_gap_at
    );

    if (d->data_snapshot)
    {
        printf(
            "RXSTALL fase DATA     : %s\n",
            d->rxstall ? "SI" : "NO"
        );

        printf(
            "PC SM DATA al cierre  : %u\n",
            d->pc_end_data
        );

        printf(
            "FIFO al cierre DATA   : %u\n",
            d->fifo_end_data
        );
    }
    else
    {
        printf(
            "Foto fase DATA        : NO DISPONIBLE\n"
        );
    }

    if (d->timeout)
    {
        printf(
            "Estado al timeout     : %s\n",
            diag_state_name(
                d->timeout_state
            )
        );

        printf(
            "PC SM DATA timeout    : %u\n",
            d->pc_timeout
        );

        printf(
            "FIFO SM DATA timeout  : %u\n",
            d->fifo_timeout
        );

        printf(
            "RXSTALL al timeout    : %s\n",
            d->rxstall_at_timeout
            ?
            "SI"
            :
            "NO"
        );
    }
    else
    {
        printf(
            "Timeout               : NO\n"
        );
    }

    printf(
        "STATUS visto por BM   : %s\n",
        t->status_seen ? "SI" : "NO"
    );

    print_missing_data(t);
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
 * DECODIFICAR EVENTO
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
            (payload >> MIL1553_RX_WORD_SHIFT)
            &
            MIL1553_RX_WORD_MASK
        );

    uint8_t received_parity =
        (uint8_t)(
            payload &
            MIL1553_RX_PARITY_MASK
        );

    uint8_t expected_parity =
        compute_odd_parity(*word);

    *parity_ok =
        (
            received_parity ==
            expected_parity
        );
}


/* ============================================================
 * RESET TRANSACCION
 * ============================================================
 */

static void transaction_reset(
    mil1553_transaction_t *t)
{
    t->index = 0u;

    t->command = 0u;
    t->command_parity_ok = false;

    t->rt_address = 0u;
    t->tr = 0u;
    t->subaddress = 0u;
    t->word_count = 0u;

    t->data_count = 0u;

    for (uint8_t i = 0u;
         i < BM_MAX_DATA_WORDS;
         i++)
    {
        t->data[i] = 0u;

        t->data_parity_ok[i] = false;
    }

    t->status = 0u;

    t->status_seen = false;
    t->status_parity_ok = false;

    t->status_rt = 0u;
    t->status_message_error = false;

    t->sequence_ok = true;
    t->valid = true;
}


/* ============================================================
 * DIRECCION
 * ============================================================
 */

static const char *direction_name(uint8_t tr)
{
    if (tr == BUS_1553_TR_BC_TO_RT)
    {
        return "BC -> RT";
    }

    return "RT -> BC";
}


/* ============================================================
 * MOSTRAR TRANSACCION
 * ============================================================
 */

static void print_transaction(
    const mil1553_transaction_t *t,
    bool complete)
{
    printf(
        "\n"
        "----------------------------------------\n"
        "BM - TRANSACCION #%lu\n"
        "----------------------------------------\n",
        (unsigned long)t->index
    );

    printf(
        "Direccion          : %s\n",
        direction_name(t->tr)
    );

    printf(
        "COMMAND            : 0x%04X\n",
        t->command
    );

    printf(
        "COMMAND paridad    : %s\n",
        t->command_parity_ok
        ?
        "OK"
        :
        "ERROR"
    );

    printf(
        "RT                 : %u\n",
        t->rt_address
    );

    printf(
        "T/R                : %u\n",
        t->tr
    );

    printf(
        "Subaddress         : %u\n",
        t->subaddress
    );

    printf(
        "Word Count         : %u\n",
        t->word_count
    );

    printf(
        "DATA recibidos     : %u/%u\n",
        t->data_count,
        t->word_count
    );

    printf("\nDATA:\n");

    for (uint8_t i = 0u;
         i < t->data_count;
         i++)
    {
        printf(
            "  DATA%-2u          : 0x%04X   paridad %s\n",
            (unsigned int)(i + 1u),
            t->data[i],
            t->data_parity_ok[i]
            ?
            "OK"
            :
            "ERROR"
        );
    }

    printf("\n");

    if (t->status_seen)
    {
        printf(
            "STATUS             : 0x%04X\n",
            t->status
        );

        printf(
            "STATUS RT          : %u\n",
            t->status_rt
        );

        printf(
            "STATUS paridad     : %s\n",
            t->status_parity_ok
            ?
            "OK"
            :
            "ERROR"
        );

        printf(
            "Message Error      : %u\n",
            t->status_message_error
            ?
            1u
            :
            0u
        );
    }
    else
    {
        printf(
            "STATUS             : NO RECIBIDO\n"
        );
    }

    printf(
        "\n"
        "Secuencia          : %s\n",
        t->sequence_ok
        ?
        "OK"
        :
        "ERROR"
    );

    printf(
        "Completa           : %s\n",
        complete
        ?
        "SI"
        :
        "NO"
    );

    printf(
        "Transaccion valida : %s\n",
        t->valid
        ?
        "SI"
        :
        "NO"
    );

    printf(
        "----------------------------------------\n"
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
 * FINALIZAR TRANSACCION
 * ============================================================
 *
 * IMPORTANTE:
 *
 * No se imprime durante la captura.
 *
 * Guardamos las primeras transacciones y las invalidas
 * en memoria RAM.
 * ============================================================
 */

static void finish_transaction(
    mil1553_transaction_t *t,
    bm_stats_t *stats,
    bool complete)
{
    if (!complete)
    {
        t->valid = false;

        t->sequence_ok = false;
    }

    if (complete)
    {
        stats->transactions_completed++;
    }

    if (t->valid)
    {
        stats->transactions_valid++;

        if (t->tr == BUS_1553_TR_BC_TO_RT)
        {
            stats->bc_to_rt_valid++;
        }
        else
        {
            stats->rt_to_bc_valid++;
        }
    }
    else
    {
        stats->transactions_invalid++;
    }


    /* Estadisticas de diagnostico. */

    if (active_diag.rxstall)
    {
        diag_stall_transactions++;
    }

    if (active_diag.max_fifo > diag_max_fifo)
    {
        diag_max_fifo =
            active_diag.max_fifo;
    }

    if (active_diag.max_poll_gap_us >
        diag_max_poll_gap_us)
    {
        diag_max_poll_gap_us =
            active_diag.max_poll_gap_us;
    }

    if (active_diag.max_read_gap_us >
        diag_max_read_gap_us)
    {
        diag_max_read_gap_us =
            active_diag.max_read_gap_us;
    }


    /* Guardar solamente registros de interes. */

    if ((t->index <= PRINT_FIRST_TRANSACTIONS) ||
        (!t->valid))
    {
        if (saved_count < BM_DIAG_MAX_SAVED)
        {
            saved_diag[saved_count].transaction =
                *t;

            saved_diag[saved_count].diag =
                active_diag;

            saved_diag[saved_count].complete =
                complete;

            saved_count++;
        }
        else
        {
            saved_dropped++;
        }
    }
}


/* ============================================================
 * PROCESAR STATUS
 * ============================================================
 */

static void process_status(
    mil1553_transaction_t *t,
    bm_stats_t *stats,
    uint16_t word,
    bool parity_ok)
{
    t->status = word;

    t->status_seen = true;

    t->status_parity_ok = parity_ok;

    t->status_rt =
        BUS_1553_STATUS_RT(word);

    t->status_message_error =
        (
            BUS_1553_STATUS_MSG_ERROR(word)
            !=
            0u
        );

    if (!parity_ok)
    {
        stats->status_parity_errors++;

        t->valid = false;
    }

    if (t->status_rt != t->rt_address)
    {
        stats->status_rt_mismatch++;

        t->valid = false;
    }

    if (t->status_message_error)
    {
        stats->status_message_error++;
    }
}


/* ============================================================
 * PROCESAR DATA
 * ============================================================
 */

static void process_data(
    mil1553_transaction_t *t,
    bm_stats_t *stats,
    uint16_t word,
    bool parity_ok)
{
    if (t->data_count >= BM_MAX_DATA_WORDS)
    {
        stats->sequence_errors++;

        t->sequence_ok = false;

        t->valid = false;

        return;
    }

    uint8_t position =
        t->data_count;

    t->data[position] =
        word;

    t->data_parity_ok[position] =
        parity_ok;

    t->data_count++;

    stats->data_words_received++;

    if (!parity_ok)
    {
        stats->data_parity_errors++;

        t->valid = false;
    }
}


/* ============================================================
 * MONITOREAR P DESPUES DE DATA32
 * ============================================================
 *
 * Conservamos la estrategia que funciona actualmente.
 *
 * Rearme rapido SM0 y ventana de 200 us.
 * ============================================================
 */

static edge_diag_t monitor_p_after_data32(
    uint8_t pc_before_rearm)
{
    edge_diag_t d = {0};

    d.pc_before_rearm =
        pc_before_rearm;


    /* Rearme rapido SM0. */

    mil1553_rx_cmd_status_rearm();

    d.pc_after_rearm =
        (uint8_t)
        pio_sm_get_pc(
            BM_RX_PIO,
            BM_RX_SM_CMD
        );


    /* Nivel inicial de GP2. */

    uint8_t previous =
        gpio_get(BUS_PIN_P)
        ?
        1u
        :
        0u;

    d.initial_level =
        previous;

    uint64_t start_us =
        time_us_64();


    /* Ventana de observacion. */

    while ((time_us_64() - start_us)
           <
           EDGE_MONITOR_WINDOW_US)
    {
        uint8_t current =
            gpio_get(BUS_PIN_P)
            ?
            1u
            :
            0u;

        if (current != previous)
        {
            d.edges++;

            previous = current;
        }
    }

    d.final_level =
        previous;

    d.pc_after_window =
        (uint8_t)
        pio_sm_get_pc(
            BM_RX_PIO,
            BM_RX_SM_CMD
        );

    d.fifo_after_window =
        (uint8_t)
        pio_sm_get_rx_fifo_level(
            BM_RX_PIO,
            BM_RX_SM_CMD
        );

    return d;
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
     * INICIALIZACION RX
     * ========================================================
     */

    mil1553_rx_init();

    mil1553_rx_clear();

    data_diag_clear_flags();


    /* ========================================================
     * WARM-UP SM0
     * ========================================================
     */

    printf(
        "\nWarm-up SM0: %u rearme(s)\n",
        SM0_WARMUP_COUNT
    );

    for (uint32_t i = 0u;
         i < SM0_WARMUP_COUNT;
         i++)
    {
        mil1553_rx_cmd_status_rearm();
    }

    mil1553_rx_clear();

    data_diag_clear_flags();

    sleep_ms(100);


    /* ========================================================
     * CABECERA
     * ========================================================
     */

    printf(
        "\n"
        "========================================\n"
        "BM - TEST DIAGNOSTICO DATA / WC = 32\n"
        "========================================\n"
        "Test                 : BC -> RT\n"
        "Transacciones        : %lu\n"
        "WC                   : 32\n"
        "DATA32               : 0x8000\n"
        "Delay RT STATUS      : 8 us\n"
        "Ventana GPIO P       : %llu us\n"
        "Warm-up SM0          : %u rearme(s)\n"
        "Rearme SM0 DATA32    : HABILITADO\n"
        "Fix timeout          : HABILITADO\n"
        "CLKDIV sync          : HABILITADO\n"
        "Diagnostico DATA     : FIFO / RXSTALL / TIMING\n"
        "Impresion de detalle : DIFERIDA\n"
        "========================================\n\n",
        (unsigned long)TEST_TRANSACTIONS,
        (unsigned long long)EDGE_MONITOR_WINDOW_US,
        SM0_WARMUP_COUNT
    );


    /* ========================================================
     * ESTADO
     * ========================================================
     */

    bm_state_t state =
        BM_WAIT_COMMAND;

    mil1553_transaction_t transaction;

    transaction_reset(&transaction);


    /* ========================================================
     * ESTADISTICAS
     * ========================================================
     */

    bm_stats_t stats = {0};


    /* ========================================================
     * DIAGNOSTICO FISICO
     * ========================================================
     *
     * Memoria estatica para no ocupar la pila.
     * ========================================================
     */

    static edge_diag_t edge_diag[TEST_TRANSACTIONS] =
        {0};

    uint32_t total_edges = 0u;

    uint32_t transactions_with_edges = 0u;

    uint32_t transactions_without_edges = 0u;

    uint32_t fifo_nonzero_after_window = 0u;

    uint32_t min_edges = UINT32_MAX;

    uint32_t max_edges = 0u;


    /* ========================================================
     * CONTROL DE TIEMPO
     * ========================================================
     */

    bool test_started = false;

    bool report_done = false;

    uint64_t last_word_us =
        time_us_64();

    uint64_t last_activity_us =
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
         * WAIT COMMAND
         * ====================================================
         */

        if (state == BM_WAIT_COMMAND)
        {
            if (mil1553_rx_cmd_status_available())
            {
                uint32_t event =
                    mil1553_rx_get_cmd_status();

                uint16_t word = 0u;

                bool parity_ok = false;

                decode_rx_event(
                    event,
                    &word,
                    &parity_ok
                );

                test_started = true;

                last_activity_us = now_us;


                /* Validar paridad del COMMAND. */

                if (!parity_ok)
                {
                    stats.command_parity_errors++;

                    mil1553_rx_clear();

                    data_diag_clear_flags();

                    continue;
                }


                /* Decodificar COMMAND. */

                uint8_t rt =
                    BUS_1553_CMD_RT(word);

                uint8_t tr =
                    BUS_1553_CMD_TR(word);

                uint8_t subaddress =
                    BUS_1553_CMD_SUB(word);

                uint8_t wc =
                    decode_word_count(
                        BUS_1553_CMD_WC(word)
                    );


                /* Mode Codes no soportados. */

                if ((subaddress == 0u) ||
                    (subaddress == 31u))
                {
                    stats.unsupported_mode_codes++;

                    mil1553_rx_clear();

                    data_diag_clear_flags();

                    continue;
                }


                /* Iniciar transaccion. */

                transaction_reset(
                    &transaction
                );

                stats.transactions_started++;

                transaction.index =
                    stats.transactions_started;

                transaction.command =
                    word;

                transaction.command_parity_ok =
                    true;

                transaction.rt_address =
                    rt;

                transaction.tr =
                    tr;

                transaction.subaddress =
                    subaddress;

                transaction.word_count =
                    wc;


                /* Iniciar diagnostico. */

                data_diag_begin(tr);

                last_word_us =
                    now_us;


                /* Seleccionar direccion. */

                if (tr == BUS_1553_TR_BC_TO_RT)
                {
                    stats.bc_to_rt_started++;

                    data_diag_start_phase(
                        now_us
                    );

                    state =
                        BM_BC_TO_RT_DATA;
                }
                else
                {
                    stats.rt_to_bc_started++;

                    state =
                        BM_RT_TO_BC_STATUS;
                }
            }
        }


        /* ====================================================
         * BC -> RT : DATA
         * ====================================================
         */

        else if (state == BM_BC_TO_RT_DATA)
        {
            /* Medir demora entre iteraciones. */

            data_diag_poll(now_us);

            if (mil1553_rx_data_bc_to_rt_available())
            {
                /* Medir FIFO antes de retirar DATA. */

                data_diag_read(now_us);

                uint32_t event =
                    mil1553_rx_get_data_bc_to_rt();

                uint16_t word = 0u;

                bool parity_ok = false;

                decode_rx_event(
                    event,
                    &word,
                    &parity_ok
                );

                process_data(
                    &transaction,
                    &stats,
                    word,
                    parity_ok
                );

                last_word_us =
                    now_us;

                last_activity_us =
                    now_us;


                /* ============================================
                 * ULTIMO DATA RECIBIDO
                 * ============================================
                 */

                if (transaction.data_count >=
                    transaction.word_count)
                {
                    /*
                     * Capturar estado de SM DATA.
                     *
                     * RXSTALL, FIFO y PC.
                     */

                    data_diag_snapshot();


                    /* Estado SM0 antes del rearme. */

                    uint8_t pc_before =
                        (uint8_t)
                        pio_sm_get_pc(
                            BM_RX_PIO,
                            BM_RX_SM_CMD
                        );


                    /* Diagnostico fisico existente. */

                    edge_diag_t d =
                        monitor_p_after_data32(
                            pc_before
                        );


                    /* Guardar diagnostico GP2. */

                    if ((transaction.index >= 1u) &&
                        (transaction.index <=
                         TEST_TRANSACTIONS))
                    {
                        edge_diag[
                            transaction.index - 1u
                        ] = d;
                    }

                    total_edges +=
                        d.edges;

                    if (d.edges > 0u)
                    {
                        transactions_with_edges++;
                    }
                    else
                    {
                        transactions_without_edges++;
                    }

                    if (d.edges < min_edges)
                    {
                        min_edges = d.edges;
                    }

                    if (d.edges > max_edges)
                    {
                        max_edges = d.edges;
                    }

                    if (d.fifo_after_window > 0u)
                    {
                        fifo_nonzero_after_window++;
                    }


                    /*
                     * IMPORTANTE:
                     *
                     * No imprimir nada en este punto.
                     *
                     * Los resultados estan guardados
                     * en memoria RAM.
                     */

                    state =
                        BM_BC_TO_RT_STATUS;

                    last_word_us =
                        time_us_64();

                    last_activity_us =
                        last_word_us;
                }
            }
        }


        /* ====================================================
         * BC -> RT : STATUS
         * ====================================================
         */

        else if (state == BM_BC_TO_RT_STATUS)
        {
            if (mil1553_rx_cmd_status_available())
            {
                uint32_t event =
                    mil1553_rx_get_cmd_status();

                uint16_t word = 0u;

                bool parity_ok = false;

                decode_rx_event(
                    event,
                    &word,
                    &parity_ok
                );

                process_status(
                    &transaction,
                    &stats,
                    word,
                    parity_ok
                );

                last_activity_us =
                    now_us;


                /* Finalizar transaccion. */

                finish_transaction(
                    &transaction,
                    &stats,
                    true
                );


                /* Rearme DATA existente. */

                mil1553_rx_data_rearm();

                data_diag_clear_flags();

                transaction_reset(
                    &transaction
                );

                state =
                    BM_WAIT_COMMAND;

                last_word_us =
                    time_us_64();

                last_activity_us =
                    last_word_us;
            }
        }


        /* ====================================================
         * RT -> BC : STATUS
         * ====================================================
         */

        else if (state == BM_RT_TO_BC_STATUS)
        {
            if (mil1553_rx_cmd_status_available())
            {
                uint32_t event =
                    mil1553_rx_get_cmd_status();

                uint16_t word = 0u;

                bool parity_ok = false;

                decode_rx_event(
                    event,
                    &word,
                    &parity_ok
                );

                process_status(
                    &transaction,
                    &stats,
                    word,
                    parity_ok
                );

                last_word_us =
                    now_us;

                last_activity_us =
                    now_us;

                data_diag_start_phase(
                    now_us
                );

                state =
                    BM_RT_TO_BC_DATA;
            }
        }


        /* ====================================================
         * RT -> BC : DATA
         * ====================================================
         */

        else if (state == BM_RT_TO_BC_DATA)
        {
            data_diag_poll(now_us);

            if (mil1553_rx_data_rt_to_bc_available())
            {
                data_diag_read(now_us);

                uint32_t event =
                    mil1553_rx_get_data_rt_to_bc();

                uint16_t word = 0u;

                bool parity_ok = false;

                decode_rx_event(
                    event,
                    &word,
                    &parity_ok
                );

                process_data(
                    &transaction,
                    &stats,
                    word,
                    parity_ok
                );

                last_word_us =
                    now_us;

                last_activity_us =
                    now_us;


                /* Ultimo DATA. */

                if (transaction.data_count >=
                    transaction.word_count)
                {
                    data_diag_snapshot();

                    finish_transaction(
                        &transaction,
                        &stats,
                        true
                    );

                    mil1553_rx_data_rearm();

                    data_diag_clear_flags();

                    transaction_reset(
                        &transaction
                    );

                    state =
                        BM_WAIT_COMMAND;

                    last_word_us =
                        time_us_64();

                    last_activity_us =
                        last_word_us;
                }
            }
        }


        /* ====================================================
         * REFRESCAR TIEMPO
         * ====================================================
         *
         * Mantener el fix de timeout.
         * ====================================================
         */

        now_us =
            time_us_64();


        /* ====================================================
         * TIMEOUT
         * ====================================================
         */

        if ((state != BM_WAIT_COMMAND) &&
            ((now_us - last_word_us)
             >=
             BM_WORD_TIMEOUT_US))
        {
            /*
             * Capturar el estado del PIO ANTES de limpiarlo.
             */

            data_diag_timeout(state);

            stats.timeout_errors++;

            stats.sequence_errors++;

            transaction.valid =
                false;

            transaction.sequence_ok =
                false;


            /* Guardar transaccion invalida en RAM. */

            finish_transaction(
                &transaction,
                &stats,
                false
            );


            /* Recuperacion existente. */

            mil1553_rx_clear();

            data_diag_clear_flags();

            transaction_reset(
                &transaction
            );

            state =
                BM_WAIT_COMMAND;

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
            (state == BM_WAIT_COMMAND))
        {
            now_us =
                time_us_64();

            if ((now_us - last_activity_us)
                >=
                TEST_END_SILENCE_US)
            {
                report_done = true;

                if (min_edges == UINT32_MAX)
                {
                    min_edges = 0u;
                }


                /* ============================================
                 * INFORME GENERAL
                 * ============================================
                 */

                printf(
                    "\n\n"
                    "========================================\n"
                    "BM - RESULTADO DIAGNOSTICO DATA / WC = 32\n"
                    "========================================\n\n"
                );

                printf(
                    "--- TRANSACCIONES ---\n"
                );

                printf(
                    "Iniciadas               : %lu\n",
                    (unsigned long)
                        stats.transactions_started
                );

                printf(
                    "Completas               : %lu\n",
                    (unsigned long)
                        stats.transactions_completed
                );

                printf(
                    "Validas                 : %lu\n",
                    (unsigned long)
                        stats.transactions_valid
                );

                printf(
                    "Invalidas               : %lu\n\n",
                    (unsigned long)
                        stats.transactions_invalid
                );


                /* ============================================
                 * DIRECCIONES
                 * ============================================
                 */

                printf(
                    "--- DIRECCIONES ---\n"
                );

                printf(
                    "BC -> RT iniciadas      : %lu\n",
                    (unsigned long)
                        stats.bc_to_rt_started
                );

                printf(
                    "BC -> RT validas        : %lu\n",
                    (unsigned long)
                        stats.bc_to_rt_valid
                );

                printf(
                    "RT -> BC iniciadas      : %lu\n",
                    (unsigned long)
                        stats.rt_to_bc_started
                );

                printf(
                    "RT -> BC validas        : %lu\n\n",
                    (unsigned long)
                        stats.rt_to_bc_valid
                );


                /* ============================================
                 * PALABRAS
                 * ============================================
                 */

                printf(
                    "--- PALABRAS ---\n"
                );

                printf(
                    "DATA recibidos          : %lu\n\n",
                    (unsigned long)
                        stats.data_words_received
                );


                /* ============================================
                 * STATUS
                 * ============================================
                 */

                printf(
                    "--- STATUS ---\n"
                );

                printf(
                    "Message Error           : %lu\n",
                    (unsigned long)
                        stats.status_message_error
                );

                printf(
                    "RT STATUS incorrecto    : %lu\n\n",
                    (unsigned long)
                        stats.status_rt_mismatch
                );


                /* ============================================
                 * ERRORES
                 * ============================================
                 */

                printf(
                    "--- ERRORES ---\n"
                );

                printf(
                    "COMMAND paridad         : %lu\n",
                    (unsigned long)
                        stats.command_parity_errors
                );

                printf(
                    "DATA paridad            : %lu\n",
                    (unsigned long)
                        stats.data_parity_errors
                );

                printf(
                    "STATUS paridad          : %lu\n",
                    (unsigned long)
                        stats.status_parity_errors
                );

                printf(
                    "Secuencia               : %lu\n",
                    (unsigned long)
                        stats.sequence_errors
                );

                printf(
                    "Timeout                 : %lu\n",
                    (unsigned long)
                        stats.timeout_errors
                );

                printf(
                    "Mode Code no soportado  : %lu\n\n",
                    (unsigned long)
                        stats.unsupported_mode_codes
                );


                /* ============================================
                 * DIAGNOSTICO FISICO GP2
                 * ============================================
                 */

                printf(
                    "--- DIAGNOSTICO FISICO GP2 / P ---\n"
                );

                printf(
                    "Ventana por transaccion : %llu us\n",
                    (unsigned long long)
                        EDGE_MONITOR_WINDOW_US
                );

                printf(
                    "Transacciones con edges : %lu\n",
                    (unsigned long)
                        transactions_with_edges
                );

                printf(
                    "Transacciones sin edges : %lu\n",
                    (unsigned long)
                        transactions_without_edges
                );

                printf(
                    "Edges totales           : %lu\n",
                    (unsigned long)total_edges
                );

                printf(
                    "Edges minimos           : %lu\n",
                    (unsigned long)min_edges
                );

                printf(
                    "Edges maximos           : %lu\n",
                    (unsigned long)max_edges
                );

                printf(
                    "FIFO SM0 > 0 post ventana: %lu\n\n",
                    (unsigned long)
                        fifo_nonzero_after_window
                );


                /* ============================================
                 * DETALLE FISICO POR TRANSACCION
                 * ============================================
                 */

                printf(
                    "--- DETALLE POR TRANSACCION ---\n"
                );

                printf(
                    "TX  PCpre PCpost Pinicial Edges "
                    "Pfinal PCfin FIFO\n"
                );

                printf(
                    "----------------------------------------"
                    "----------------\n"
                );

                for (uint32_t i = 0u;
                     i < TEST_TRANSACTIONS;
                     i++)
                {
                    printf(
                        "%2lu    %2u     %2u      %u     %4lu"
                        "     %u     %2u    %u\n",
                        (unsigned long)(i + 1u),
                        edge_diag[i].pc_before_rearm,
                        edge_diag[i].pc_after_rearm,
                        edge_diag[i].initial_level,
                        (unsigned long)edge_diag[i].edges,
                        edge_diag[i].final_level,
                        edge_diag[i].pc_after_window,
                        edge_diag[i].fifo_after_window
                    );
                }


                /* ============================================
                 * RESUMEN DIAGNOSTICO DATA
                 * ============================================
                 */

                printf(
                    "\n"
                    "--- RESUMEN DIAGNOSTICO DATA ---\n"
                );

                printf(
                    "TX con RXSTALL fase DATA : %lu\n",
                    (unsigned long)
                        diag_stall_transactions
                );

                printf(
                    "FIFO maxima observada    : %u / 8\n",
                    diag_max_fifo
                );

                printf(
                    "Max intervalo polling    : %lu us\n",
                    (unsigned long)
                        diag_max_poll_gap_us
                );

                printf(
                    "Max intervalo lecturas   : %lu us\n",
                    (unsigned long)
                        diag_max_read_gap_us
                );

                printf(
                    "Registros guardados      : %lu / %u\n",
                    (unsigned long)saved_count,
                    BM_DIAG_MAX_SAVED
                );

                printf(
                    "Registros no guardados   : %lu\n",
                    (unsigned long)saved_dropped
                );


                /* ============================================
                 * MOSTRAR REGISTROS GUARDADOS
                 * ============================================
                 */

                for (uint32_t i = 0u;
                     i < saved_count;
                     i++)
                {
                    print_transaction(
                        &saved_diag[i].transaction,
                        saved_diag[i].complete
                    );

                    print_data_diag(
                        &saved_diag[i]
                    );
                }


                /* ============================================
                 * TASAS
                 * ============================================
                 */

                printf(
                    "\n"
                    "--- TASAS ---\n"
                );

                print_rate(
                    "Transacciones validas",
                    stats.transactions_valid,
                    TEST_TRANSACTIONS
                );

                print_rate(
                    "Transacciones completas",
                    stats.transactions_completed,
                    TEST_TRANSACTIONS
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