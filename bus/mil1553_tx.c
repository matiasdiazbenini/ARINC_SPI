#include "mil1553_tx.h"

#include <stdint.h>
#include <stdbool.h>

#include "pico/stdlib.h"

#include "hardware/clocks.h"
#include "hardware/pio.h"

#include "mil1553_tx.pio.h"


/* ============================================================
 * PIO TX
 * ============================================================
 */

static PIO tx_pio =
    pio0;


static const uint tx_sm =
    0u;


static uint tx_offset =
    0u;


static bool tx_initialized =
    false;


/* ============================================================
 * CLOCK
 * ============================================================
 */

#define MIL1553_PIO_CLOCK_HZ    8000000.0f


/* ============================================================
 * INICIALIZACION
 * ============================================================
 */

void mil1553_tx_init(void)
{
    if (tx_initialized)
    {
        return;
    }


    /* --------------------------------------------------------
     * Cargar programa PIO
     * --------------------------------------------------------
     */

    tx_offset =
        pio_add_program(
            tx_pio,
            &mil1553_tx_program
        );


    pio_sm_config c =
        mil1553_tx_program_get_default_config(
            tx_offset
        );


    /* --------------------------------------------------------
     * SIDE SET
     *
     * GP2 = P
     * GP3 = N
     *
     * side 0 -> P=0 N=0
     * side 1 -> P=1 N=0
     * side 2 -> P=0 N=1
     * --------------------------------------------------------
     */

    sm_config_set_sideset_pins(
        &c,
        BUS_PIN_P
    );


    /* --------------------------------------------------------
     * SET PINDIRS
     * --------------------------------------------------------
     */

    sm_config_set_set_pins(
        &c,
        BUS_PIN_P,
        2u
    );


    /* --------------------------------------------------------
     * OSR
     *
     * Shift hacia izquierda.
     * Autopull deshabilitado.
     * --------------------------------------------------------
     */

    sm_config_set_out_shift(
        &c,
        false,
        false,
        32u
    );


    /* --------------------------------------------------------
     * FIFO TX ampliada
     * --------------------------------------------------------
     */

    sm_config_set_fifo_join(
        &c,
        PIO_FIFO_JOIN_TX
    );


    /* --------------------------------------------------------
     * CLOCK = 8 MHz
     * --------------------------------------------------------
     */

    const float clkdiv =
        (float)clock_get_hz(
            clk_sys
        )
        /
        MIL1553_PIO_CLOCK_HZ;


    sm_config_set_clkdiv(
        &c,
        clkdiv
    );


    /* --------------------------------------------------------
     * GPIO
     * --------------------------------------------------------
     */

    pio_gpio_init(
        tx_pio,
        BUS_PIN_P
    );


    pio_gpio_init(
        tx_pio,
        BUS_PIN_N
    );


    /*
     * Inicialmente P y N quedan como entradas.
     *
     * El propio programa PIO habilita las salidas
     * al comenzar una palabra.
     */
    pio_sm_set_consecutive_pindirs(
        tx_pio,
        tx_sm,
        BUS_PIN_P,
        2u,
        false
    );


    /* --------------------------------------------------------
     * STATE MACHINE
     * --------------------------------------------------------
     */

    pio_sm_init(
        tx_pio,
        tx_sm,
        tx_offset,
        &c
    );


    /* --------------------------------------------------------
     * ESTADO INICIAL
     * --------------------------------------------------------
     */

    pio_sm_set_pins_with_mask(
        tx_pio,
        tx_sm,
        0u,
        (1u << BUS_PIN_P) |
        (1u << BUS_PIN_N)
    );


    /* --------------------------------------------------------
     * HABILITAR
     * --------------------------------------------------------
     */

    pio_sm_set_enabled(
        tx_pio,
        tx_sm,
        true
    );


    tx_initialized =
        true;
}


/* ============================================================
 * TRANSMITIR PALABRA
 * ============================================================
 */

void mil1553_tx_send_word(
    bus_1553_sync_t sync_type,
    uint16_t word)
{
    mil1553_tx_init();


    uint32_t frame =
        0u;


    /* --------------------------------------------------------
     * SELECTOR SYNC
     *
     * bit31:
     *
     * 1 -> CMD / STATUS
     * 0 -> DATA
     * --------------------------------------------------------
     */

    if (sync_type ==
        BUS_1553_SYNC_CMD_STATUS)
    {
        frame |=
            (1u << 31);
    }


    /* --------------------------------------------------------
     * WORD
     *
     * bits 30..15
     * --------------------------------------------------------
     */

    frame |=
        ((uint32_t)word << 15);


    /* --------------------------------------------------------
     * PARIDAD IMPAR
     *
     * bit14
     * --------------------------------------------------------
     */

    uint8_t parity =
        bus_compute_odd_parity(
            word
        );


    frame |=
        ((uint32_t)(
            parity &
            0x01u
        ) << 14);


    /* --------------------------------------------------------
     * ENTREGAR AL PIO
     * --------------------------------------------------------
     */

    pio_sm_put_blocking(
        tx_pio,
        tx_sm,
        frame
    );
}


/* ============================================================
 * TRANSMITIR UNA PALABRA BLOQUEANTE
 * ============================================================
 */

void mil1553_tx_send_word_blocking(
    bus_1553_sync_t sync_type,
    uint16_t word)
{
    mil1553_tx_send_word(
        sync_type,
        word
    );


    mil1553_tx_wait_idle();
}


/* ============================================================
 * ESPERAR FINALIZACION FISICA
 * ============================================================
 */

void mil1553_tx_wait_idle(void)
{
    if (!tx_initialized)
    {
        return;
    }


    /*
     * Esperar hasta que el PIO retire de FIFO
     * la ultima palabra pendiente.
     */
    while (!pio_sm_is_tx_fifo_empty(
                tx_pio,
                tx_sm))
    {
        tight_loop_contents();
    }


    /*
     * FIFO vacia significa que la ultima palabra
     * ya fue cargada al OSR.
     *
     * Desde ese instante todavia debe transmitirse:
     *
     *      3 us  SYNC
     *     16 us  DATA
     *      1 us  PARITY
     *
     * Total aproximado = 20 us.
     *
     * Esperamos 21 us para garantizar que el PIO
     * haya ejecutado:
     *
     *      set pindirs, 0 side 0
     *
     * y liberado fisicamente el bus.
     */
    busy_wait_us_32(
        21u
    );
}