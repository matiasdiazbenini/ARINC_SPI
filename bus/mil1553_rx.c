#include "mil1553_rx.h"

#include <stdint.h>
#include <stdbool.h>

#include "pico/stdlib.h"
#include "hardware/clocks.h"
#include "hardware/pio.h"

#include "bus.h"
#include "mil1553_rx.pio.h"


// ============================================================
// Configuración
// ============================================================

static PIO rx_pio = pio1;

/*
 * SM0 observa GP2/P -> CMD/STATUS
 * SM1 observa GP3/N -> DATA WORD
 */
static const uint rx_sm_cmd  = 0u;
static const uint rx_sm_data = 1u;

static uint rx_offset = 0u;
static bool rx_initialized = false;

#define MIL1553_RX_PIO_CLOCK_HZ 8000000.0f



// ============================================================
// Configurar una State Machine
// ============================================================

static void mil1553_rx_configure_sm(
    uint sm,
    uint input_pin)
{
    pio_sm_config c =
        mil1553_rx_program_get_default_config(
            rx_offset
        );


    /*
     * WAIT PIN 0 será relativo a este IN base.
     */
    sm_config_set_in_pins(
        &c,
        input_pin
    );


    /*
     * JMP PIN observa la misma línea.
     */
    sm_config_set_jmp_pin(
        &c,
        input_pin
    );


    // FIFO dedicada a RX.
    sm_config_set_fifo_join(
        &c,
        PIO_FIFO_JOIN_RX
    );


    sm_config_set_in_shift(
        &c,
        false,
        false,
        32u
    );


    const float clkdiv =
        (float)clock_get_hz(clk_sys)
        / MIL1553_RX_PIO_CLOCK_HZ;


    sm_config_set_clkdiv(
        &c,
        clkdiv
    );


    pio_sm_init(
        rx_pio,
        sm,
        rx_offset,
        &c
    );


    pio_sm_clear_fifos(
        rx_pio,
        sm
    );


    pio_sm_restart(
        rx_pio,
        sm
    );
}



// ============================================================
// Inicialización
// ============================================================

void mil1553_rx_init(void)
{
    if (rx_initialized)
    {
        return;
    }


    // Cargar el programa PIO una sola vez.
    rx_offset =
        pio_add_program(
            rx_pio,
            &mil1553_rx_program
        );


    // --------------------------------------------------------
    // GPIO
    // --------------------------------------------------------

    pio_gpio_init(
        rx_pio,
        BUS_PIN_P
    );

    pio_gpio_init(
        rx_pio,
        BUS_PIN_N
    );


    /*
     * El receptor nunca conduce las líneas.
     */
    pio_sm_set_consecutive_pindirs(
        rx_pio,
        rx_sm_cmd,
        BUS_PIN_P,
        2u,
        false
    );

    pio_sm_set_consecutive_pindirs(
        rx_pio,
        rx_sm_data,
        BUS_PIN_P,
        2u,
        false
    );


    /*
     * Reposo del banco GPIO directo:
     *
     * P/N = 00
     */
    gpio_pull_down(
        BUS_PIN_P
    );

    gpio_pull_down(
        BUS_PIN_N
    );


    // Lock inicialmente libre.
    pio_interrupt_clear(
        rx_pio,
        0u
    );


    // --------------------------------------------------------
    // SM0 -> GP2 / P -> CMD
    // --------------------------------------------------------

    mil1553_rx_configure_sm(
        rx_sm_cmd,
        BUS_PIN_P
    );


    // --------------------------------------------------------
    // SM1 -> GP3 / N -> DATA
    // --------------------------------------------------------

    mil1553_rx_configure_sm(
        rx_sm_data,
        BUS_PIN_N
    );


    // --------------------------------------------------------
    // Arrancar ambas simultáneamente.
    // --------------------------------------------------------

    pio_enable_sm_mask_in_sync(
        rx_pio,
        (1u << rx_sm_cmd) |
        (1u << rx_sm_data)
    );


    rx_initialized = true;
}



// ============================================================
// Evento disponible
// ============================================================

bool mil1553_rx_available(void)
{
    if (!rx_initialized)
    {
        return false;
    }


    return
        !pio_sm_is_rx_fifo_empty(
            rx_pio,
            rx_sm_cmd
        )
        ||
        !pio_sm_is_rx_fifo_empty(
            rx_pio,
            rx_sm_data
        );
}



// ============================================================
// Leer evento
// ============================================================

uint32_t mil1553_rx_get(void)
{
    if (!rx_initialized)
    {
        return 0u;
    }


    // --------------------------------------------------------
    // SM0 -> CMD / STATUS
    // --------------------------------------------------------

    if (!pio_sm_is_rx_fifo_empty(
            rx_pio,
            rx_sm_cmd))
    {
        (void)pio_sm_get(
            rx_pio,
            rx_sm_cmd
        );

        return 1u;
    }


    // --------------------------------------------------------
    // SM1 -> DATA WORD
    // --------------------------------------------------------

    if (!pio_sm_is_rx_fifo_empty(
            rx_pio,
            rx_sm_data))
    {
        (void)pio_sm_get(
            rx_pio,
            rx_sm_data
        );

        return 2u;
    }


    return 0u;
}