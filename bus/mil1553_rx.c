#include "mil1553_rx.h"

#include <stdint.h>
#include <stdbool.h>

#include "pico/stdlib.h"

#include "hardware/clocks.h"
#include "hardware/pio.h"
#include "hardware/pio_instructions.h"

#include "bus.h"
#include "mil1553_rx.pio.h"


static PIO rx_pio = pio1;


/*
 * SM0 -> GP2 / P -> CMD/STATUS
 * SM1 -> GP3 / N -> DATA
 */
static const uint rx_sm_cmd  = 0u;
static const uint rx_sm_data = 1u;


static uint rx_offset = 0u;
static bool rx_initialized = false;


#define MIL1553_RX_PIO_CLOCK_HZ 8000000.0f



static void configure_rx_sm(
    uint sm,
    uint input_pin)
{
    pio_sm_config c =
        mil1553_rx_program_get_default_config(
            rx_offset
        );


    /*
     * IN PINS,1 y WAIT PIN 0 observarán
     * la línea propia de cada State Machine.
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


    /*
     * FIFO completa dedicada a RX.
     */
    sm_config_set_fifo_join(
        &c,
        PIO_FIFO_JOIN_RX
    );


    /*
     * Shift hacia la izquierda.
     *
     * Al recibir:
     *
     * b15, b14, ... b0
     *
     * terminamos con la palabra en los 16 bits bajos
     * del ISR.
     */
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
     * El RX nunca conduce P/N.
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


    // --------------------------------------------------------
    // Configurar SM0 y SM1
    // --------------------------------------------------------

    configure_rx_sm(
        rx_sm_cmd,
        BUS_PIN_P
    );

    configure_rx_sm(
        rx_sm_data,
        BUS_PIN_N
    );


    // --------------------------------------------------------
    // Identidad permanente de las State Machines
    //
    // SM0:
    //      Y=0
    //
    // SM1:
    //      Y=1
    //
    // El PIO utiliza Y solamente al terminar los 16 bits
    // para decidir si debe invertir la palabra.
    // --------------------------------------------------------

    pio_sm_exec(
        rx_pio,
        rx_sm_cmd,
        pio_encode_set(pio_y, 0)
    );

    pio_sm_exec(
        rx_pio,
        rx_sm_data,
        pio_encode_set(pio_y, 1)
    );


    // --------------------------------------------------------
    // Arrancar simultáneamente
    // --------------------------------------------------------

    pio_enable_sm_mask_in_sync(
        rx_pio,
        (1u << rx_sm_cmd) |
        (1u << rx_sm_data)
    );


    rx_initialized = true;
}



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



uint32_t mil1553_rx_get(void)
{
    if (!rx_initialized)
    {
        return 0u;
    }


    // --------------------------------------------------------
    // CMD / STATUS
    // --------------------------------------------------------

    if (!pio_sm_is_rx_fifo_empty(
            rx_pio,
            rx_sm_cmd))
    {
        uint32_t raw =
            pio_sm_get(
                rx_pio,
                rx_sm_cmd
            );

        uint32_t payload =
            raw & MIL1553_RX_PAYLOAD_MASK;

        return
            MIL1553_RX_TYPE_CMD_STATUS |
            payload;
    }

    // --------------------------------------------------------
    // DATA WORD
    // --------------------------------------------------------

    if (!pio_sm_is_rx_fifo_empty(
        rx_pio,
        rx_sm_data))
    {
        uint32_t raw =
            pio_sm_get(
                rx_pio,
                rx_sm_data
            );

        uint32_t payload =
            raw & MIL1553_RX_PAYLOAD_MASK;

        return
            MIL1553_RX_TYPE_DATA |
            payload;
    }


    return 0u;
}