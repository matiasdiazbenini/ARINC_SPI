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
static const uint rx_sm = 0u;

static uint rx_offset = 0u;
static bool rx_initialized = false;

#define MIL1553_RX_PIO_CLOCK_HZ 8000000.0f



// ============================================================
// Inicialización
// ============================================================

void mil1553_rx_init(void)
{
    if (rx_initialized)
    {
        return;
    }


    // --------------------------------------------------------
    // Cargar programa PIO
    // --------------------------------------------------------

    rx_offset =
        pio_add_program(
            rx_pio,
            &mil1553_rx_program
        );


    pio_sm_config c =
        mil1553_rx_program_get_default_config(
            rx_offset
        );


    // --------------------------------------------------------
    // IN base
    //
    // IN PINS,1 leerá solamente:
    //
    //      GP3 = N
    // --------------------------------------------------------

    sm_config_set_in_pins(
        &c,
        BUS_PIN_N
    );


    sm_config_set_in_pin_count(
        &c,
        1u
    );


    // --------------------------------------------------------
    // JMP PIN
    //
    // JMP PIN observará:
    //
    //      GP2 = P
    // --------------------------------------------------------

    sm_config_set_jmp_pin(
        &c,
        BUS_PIN_P
    );


    // --------------------------------------------------------
    // ISR
    // --------------------------------------------------------

    sm_config_set_in_shift(
        &c,
        false,
        false,
        32u
    );


    // --------------------------------------------------------
    // FIFO RX
    // --------------------------------------------------------

    sm_config_set_fifo_join(
        &c,
        PIO_FIFO_JOIN_RX
    );


    // --------------------------------------------------------
    // PIO = 8 MHz
    // --------------------------------------------------------

    const float clkdiv =
        (float)clock_get_hz(clk_sys)
        / MIL1553_RX_PIO_CLOCK_HZ;


    sm_config_set_clkdiv(
        &c,
        clkdiv
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
     * Ambos GPIO son siempre entradas.
     */
    pio_sm_set_consecutive_pindirs(
        rx_pio,
        rx_sm,
        BUS_PIN_P,
        2u,
        false
    );


    /*
     * Reposo del banco de prueba:
     *
     * P/N = 00
     */
    gpio_pull_down(
        BUS_PIN_P
    );

    gpio_pull_down(
        BUS_PIN_N
    );


    // --------------------------------------------------------
    // Inicializar State Machine
    // --------------------------------------------------------

    pio_sm_init(
        rx_pio,
        rx_sm,
        rx_offset,
        &c
    );


    pio_sm_clear_fifos(
        rx_pio,
        rx_sm
    );


    pio_sm_restart(
        rx_pio,
        rx_sm
    );


    pio_sm_set_enabled(
        rx_pio,
        rx_sm,
        true
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


    return !pio_sm_is_rx_fifo_empty(
        rx_pio,
        rx_sm
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


    if (pio_sm_is_rx_fifo_empty(
            rx_pio,
            rx_sm))
    {
        return 0u;
    }


    return pio_sm_get(
        rx_pio,
        rx_sm
    );
}