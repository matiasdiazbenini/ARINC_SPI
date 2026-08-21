#include "mil1553_rx.h"

#include <stdint.h>
#include <stdbool.h>

#include "pico/stdlib.h"
#include "hardware/clocks.h"
#include "hardware/pio.h"

#include "bus.h"
#include "mil1553_rx.pio.h"


// ============================================================
// Configuración del receptor PIO
// ============================================================

static PIO rx_pio = pio1;

/*
 * Para esta prueba utilizamos solamente SM0.
 *
 * SM0 observa:
 *
 *      GP2 = P
 *
 * y detecta únicamente sincronismos CMD/STATUS.
 */
static const uint rx_sm_cmd = 0u;


static uint rx_offset = 0u;
static bool rx_initialized = false;


/*
 * Reloj interno del PIO:
 *
 *      8 MHz
 *
 * 1 ciclo = 125 ns
 */
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


    // --------------------------------------------------------
    // Obtener configuración base del programa
    // --------------------------------------------------------

    pio_sm_config c =
        mil1553_rx_program_get_default_config(
            rx_offset
        );


    // --------------------------------------------------------
    // Entrada
    //
    // Para esta prueba:
    //
    //      pin 0 del programa PIO = GP2 / P
    //
    // Por eso:
    //
    //      WAIT ... PIN 0
    //
    // observa directamente GP2.
    // --------------------------------------------------------

    sm_config_set_in_pins(
        &c,
        BUS_PIN_P
    );


    // --------------------------------------------------------
    // JMP PIN
    //
    // Todas las instrucciones:
    //
    //      JMP PIN ...
    //
    // observarán también GP2 / P.
    // --------------------------------------------------------

    sm_config_set_jmp_pin(
        &c,
        BUS_PIN_P
    );


    // --------------------------------------------------------
    // RX FIFO
    // --------------------------------------------------------

    sm_config_set_fifo_join(
        &c,
        PIO_FIFO_JOIN_RX
    );


    // --------------------------------------------------------
    // ISR
    //
    // Todavía no reconstruimos la palabra completa.
    // Solamente colocamos:
    //
    //      1 = CMD/STATUS detectado
    //
    // en la RX FIFO.
    // --------------------------------------------------------

    sm_config_set_in_shift(
        &c,
        false,
        false,
        32u
    );


    // --------------------------------------------------------
    // Clock de la State Machine
    //
    // fPIO = 8 MHz
    // --------------------------------------------------------

    const float clkdiv =
        (float)clock_get_hz(clk_sys)
        / MIL1553_RX_PIO_CLOCK_HZ;


    sm_config_set_clkdiv(
        &c,
        clkdiv
    );


    // --------------------------------------------------------
    // Inicialización GPIO
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
     * El receptor nunca conduce el bus.
     *
     * GP2/P = entrada
     * GP3/N = entrada
     */
    pio_sm_set_consecutive_pindirs(
        rx_pio,
        rx_sm_cmd,
        BUS_PIN_P,
        2u,
        false
    );


    /*
     * Pull-downs débiles para obtener:
     *
     *      P/N = 00
     *
     * cuando el transmisor libera el bus.
     */
    gpio_pull_down(
        BUS_PIN_P
    );

    gpio_pull_down(
        BUS_PIN_N
    );


    // --------------------------------------------------------
    // Inicializar SM0
    // --------------------------------------------------------

    pio_sm_init(
        rx_pio,
        rx_sm_cmd,
        rx_offset,
        &c
    );


    // Vaciar FIFO anterior
    pio_sm_clear_fifos(
        rx_pio,
        rx_sm_cmd
    );


    // Reiniciar la SM
    pio_sm_restart(
        rx_pio,
        rx_sm_cmd
    );


    // --------------------------------------------------------
    // Habilitar SOLAMENTE SM0
    //
    // SM1 no participa en esta prueba.
    // --------------------------------------------------------

    pio_sm_set_enabled(
        rx_pio,
        rx_sm_cmd,
        true
    );


    rx_initialized = true;
}



// ============================================================
// ¿Hay un evento disponible?
// ============================================================

bool mil1553_rx_available(void)
{
    if (!rx_initialized)
    {
        return false;
    }


    return !pio_sm_is_rx_fifo_empty(
        rx_pio,
        rx_sm_cmd
    );
}



// ============================================================
// Obtener evento
// ============================================================

uint32_t mil1553_rx_get(void)
{
    if (!rx_initialized)
    {
        return 0u;
    }


    if (pio_sm_is_rx_fifo_empty(
            rx_pio,
            rx_sm_cmd))
    {
        return 0u;
    }


    /*
     * El PIO coloca:
     *
     *      1 = CMD/STATUS detectado
     *
     * directamente en la RX FIFO.
     */
    return pio_sm_get(
        rx_pio,
        rx_sm_cmd
    );
}