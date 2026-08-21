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
     * WAIT PIN 0 observa la línea propia.
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



void mil1553_rx_init(void)
{
    if (rx_initialized)
    {
        return;
    }


    // --------------------------------------------------------
    // Cargar programa
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
     * El receptor nunca conduce P/N.
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
     * Reposo del banco GPIO:
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
    // Configurar ambas State Machines
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
    // Identidad de cada State Machine
    //
    // Y se mantiene constante durante todo el programa:
    //
    // SM0 -> Y=0 -> observa P -> CMD
    // SM1 -> Y=1 -> observa N -> DATA
    //
    // Esto permite que el propio PIO normalice Manchester.
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
    // Arrancar ambas juntas
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
        uint32_t bit =
            pio_sm_get(
                rx_pio,
                rx_sm_cmd
            ) & 0x01u;


        if (bit == 0u)
        {
            return MIL1553_RX_EVENT_CMD_BIT0;
        }

        return MIL1553_RX_EVENT_CMD_BIT1;
    }


    // --------------------------------------------------------
    // DATA WORD
    // --------------------------------------------------------

    if (!pio_sm_is_rx_fifo_empty(
            rx_pio,
            rx_sm_data))
    {
        uint32_t bit =
            pio_sm_get(
                rx_pio,
                rx_sm_data
            ) & 0x01u;


        if (bit == 0u)
        {
            return MIL1553_RX_EVENT_DATA_BIT0;
        }

        return MIL1553_RX_EVENT_DATA_BIT1;
    }


    return 0u;
}