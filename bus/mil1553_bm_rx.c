#include "mil1553_bm_rx.h"

#include <stdint.h>
#include <stdbool.h>

#include "pico/stdlib.h"

#include "hardware/clocks.h"
#include "hardware/pio.h"

#include "bus.h"
#include "mil1553_bm_rx.pio.h"


/* ============================================================
 * CONFIGURACION
 * ============================================================
 */

static PIO bm_rx_pio =
    pio1;


/*
 * Una sola State Machine.
 */
static const uint bm_rx_sm =
    0u;


static uint bm_rx_offset =
    0u;


static bool bm_rx_initialized =
    false;


/*
 * 8 MHz:
 *
 * ciclo = 125 ns
 */
#define MIL1553_BM_RX_PIO_CLOCK_HZ \
    8000000.0f


/* ============================================================
 * CONFIGURAR STATE MACHINE
 * ============================================================
 */

static void configure_bm_rx_sm(void)
{
    pio_sm_config c =
        mil1553_bm_rx_program_get_default_config(
            bm_rx_offset
        );


    /*
     * IN base:
     *
     * bit 0 = GP2 = P
     * bit 1 = GP3 = N
     */
    sm_config_set_in_pins(
        &c,
        BUS_PIN_P
    );


    /*
     * RP2350:
     *
     * MOV X,PINS solamente necesita
     * nuestros dos GPIO.
     */
    sm_config_set_in_pin_count(
        &c,
        2u
    );


    /*
     * JMP PIN observa P.
     *
     * Se utiliza para Manchester.
     */
    sm_config_set_jmp_pin(
        &c,
        BUS_PIN_P
    );


    /*
     * Toda la FIFO se dedica a RX.
     */
    sm_config_set_fifo_join(
        &c,
        PIO_FIFO_JOIN_RX
    );


    /*
     * Shift hacia la izquierda.
     *
     * Esto permite construir:
     *
     * TYPE
     * reservado
     * WORD
     * parity
     */
    sm_config_set_in_shift(
        &c,
        false,
        false,
        32u
    );


    /*
     * Clock PIO = 8 MHz.
     */
    const float clkdiv =
        (float)clock_get_hz(clk_sys)
        /
        MIL1553_BM_RX_PIO_CLOCK_HZ;


    sm_config_set_clkdiv(
        &c,
        clkdiv
    );


    /*
     * Inicializar SM.
     */
    pio_sm_init(
        bm_rx_pio,
        bm_rx_sm,
        bm_rx_offset,
        &c
    );


    pio_sm_clear_fifos(
        bm_rx_pio,
        bm_rx_sm
    );


    pio_sm_restart(
        bm_rx_pio,
        bm_rx_sm
    );
}


/* ============================================================
 * INIT
 * ============================================================
 */

void mil1553_bm_rx_init(void)
{
    if (bm_rx_initialized)
    {
        return;
    }


    bm_rx_offset =
        pio_add_program(
            bm_rx_pio,
            &mil1553_bm_rx_program
        );


    /*
     * P y N quedan bajo control del PIO,
     * pero exclusivamente como entradas.
     */
    pio_gpio_init(
        bm_rx_pio,
        BUS_PIN_P
    );


    pio_gpio_init(
        bm_rx_pio,
        BUS_PIN_N
    );


    pio_sm_set_consecutive_pindirs(
        bm_rx_pio,
        bm_rx_sm,
        BUS_PIN_P,
        2u,
        false
    );


    /*
     * Reposo:
     *
     * P = 0
     * N = 0
     */
    gpio_pull_down(
        BUS_PIN_P
    );


    gpio_pull_down(
        BUS_PIN_N
    );


    configure_bm_rx_sm();


    pio_sm_set_enabled(
        bm_rx_pio,
        bm_rx_sm,
        true
    );


    bm_rx_initialized =
        true;
}


/* ============================================================
 * AVAILABLE
 * ============================================================
 */

bool mil1553_bm_rx_available(void)
{
    if (!bm_rx_initialized)
    {
        return false;
    }


    return
        !pio_sm_is_rx_fifo_empty(
            bm_rx_pio,
            bm_rx_sm
        );
}


/* ============================================================
 * GET
 * ============================================================
 */

uint32_t mil1553_bm_rx_get(void)
{
    if (!bm_rx_initialized)
    {
        return 0u;
    }


    if (pio_sm_is_rx_fifo_empty(
            bm_rx_pio,
            bm_rx_sm))
    {
        return 0u;
    }


    uint32_t raw =
        pio_sm_get(
            bm_rx_pio,
            bm_rx_sm
        );


    /*
     * El propio PIO ya construyó:
     *
     * TYPE + WORD + PARITY.
     *
     * No hay que invertir DATA.
     */
    return
        raw &
        (
            MIL1553_BM_RX_TYPE_MASK |
            MIL1553_BM_RX_PAYLOAD_MASK
        );
}


/* ============================================================
 * CLEAR
 * ============================================================
 */

void mil1553_bm_rx_clear(void)
{
    if (!bm_rx_initialized)
    {
        return;
    }


    pio_sm_set_enabled(
        bm_rx_pio,
        bm_rx_sm,
        false
    );


    configure_bm_rx_sm();


    pio_sm_set_enabled(
        bm_rx_pio,
        bm_rx_sm,
        true
    );
}