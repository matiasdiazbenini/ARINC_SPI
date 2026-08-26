#include "mil1553_rx.h"

#include <stdint.h>
#include <stdbool.h>

#include "pico/stdlib.h"

#include "hardware/clocks.h"
#include "hardware/pio.h"

#include "bus.h"
#include "mil1553_rx.pio.h"


/* ============================================================
 * PIO utilizado por el receptor
 * ============================================================
 */

static PIO rx_pio = pio1;


/*
 * Dos State Machines independientes:
 *
 * SM0:
 *      observa GP2 / P
 *      detecta CMD / STATUS
 *
 * SM1:
 *      observa GP3 / N
 *      detecta DATA
 */
static const uint rx_sm_cmd  = 0u;
static const uint rx_sm_data = 1u;


static uint rx_offset = 0u;

static bool rx_initialized = false;


/*
 * PIO clock:
 *
 *      8 MHz
 *
 *      1 ciclo = 125 ns
 *
 * Esto permite:
 *
 *      4 ciclos = 500 ns
 *      8 ciclos = 1 us
 */
#define MIL1553_RX_PIO_CLOCK_HZ 8000000.0f



/* ============================================================
 * CONFIGURAR UNA STATE MACHINE RX
 * ============================================================
 */

static void configure_rx_sm(
    uint sm,
    uint input_pin)
{
    /*
     * Configuración base generada por pioasm.
     */
    pio_sm_config c =
        mil1553_rx_program_get_default_config(
            rx_offset
        );


    /* --------------------------------------------------------
     * IN PINS,1
     *
     * Cada SM observa solamente su propia línea.
     * --------------------------------------------------------
     */

    sm_config_set_in_pins(
        &c,
        input_pin
    );


    /* --------------------------------------------------------
     * JMP PIN
     *
     * También observa la misma línea que IN.
     * --------------------------------------------------------
     */

    sm_config_set_jmp_pin(
        &c,
        input_pin
    );


    /* --------------------------------------------------------
     * FIFO RX dedicada.
     *
     * La FIFO TX se agrega a RX:
     *
     *      8 posiciones RX.
     * --------------------------------------------------------
     */

    sm_config_set_fifo_join(
        &c,
        PIO_FIFO_JOIN_RX
    );


    /* --------------------------------------------------------
     * ISR
     *
     * shift_right = false
     *
     * Es decir:
     *
     *      shift hacia la izquierda
     *
     * No usamos autopush.
     *
     * El PUSH es explícito después de recibir:
     *
     *      16 bits + paridad
     *
     *      17 bits total.
     * --------------------------------------------------------
     */

    sm_config_set_in_shift(
        &c,
        false,      /* shift_right */
        false,      /* autopush    */
        32u
    );


    /* --------------------------------------------------------
     * Clock divider
     * --------------------------------------------------------
     */

    const float clkdiv =
        (float)clock_get_hz(clk_sys)
        / MIL1553_RX_PIO_CLOCK_HZ;


    sm_config_set_clkdiv(
        &c,
        clkdiv
    );


    /* --------------------------------------------------------
     * Inicializar State Machine
     * --------------------------------------------------------
     */

    pio_sm_init(
        rx_pio,
        sm,
        rx_offset,
        &c
    );


    /*
     * Vaciar cualquier dato anterior.
     */

    pio_sm_clear_fifos(
        rx_pio,
        sm
    );


    /*
     * Reiniciar lógica interna de la SM.
     */

    pio_sm_restart(
        rx_pio,
        sm
    );
}



/* ============================================================
 * INICIALIZACIÓN RX
 * ============================================================
 */

void mil1553_rx_init(void)
{
    if (rx_initialized)
    {
        return;
    }


    /* --------------------------------------------------------
     * Cargar programa PIO
     * --------------------------------------------------------
     */

    rx_offset =
        pio_add_program(
            rx_pio,
            &mil1553_rx_program
        );


    /* --------------------------------------------------------
     * GPIO
     * --------------------------------------------------------
     */

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
     * Ambos GPIO quedan como entrada.
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


    /* --------------------------------------------------------
     * Estado de reposo del banco GPIO directo.
     *
     * Cuando nadie conduce:
     *
     *      P = 0
     *      N = 0
     * --------------------------------------------------------
     */

    gpio_pull_down(
        BUS_PIN_P
    );


    gpio_pull_down(
        BUS_PIN_N
    );


    /* --------------------------------------------------------
     * Configurar SM0
     *
     * GP2 / P
     *
     * CMD / STATUS
     * --------------------------------------------------------
     */

    configure_rx_sm(
        rx_sm_cmd,
        BUS_PIN_P
    );


    /* --------------------------------------------------------
     * Configurar SM1
     *
     * GP3 / N
     *
     * DATA
     * --------------------------------------------------------
     */

    configure_rx_sm(
        rx_sm_data,
        BUS_PIN_N
    );


    /*
     * Ya NO necesitamos utilizar Y para identificar
     * las State Machines.
     *
     * Antes:
     *
     *      Y=0 -> CMD
     *      Y=1 -> DATA
     *
     * y el PIO utilizaba Y para invertir DATA.
     *
     * Ahora la inversión de DATA se realiza en Cortex,
     * por lo que eliminamos completamente esa lógica
     * del camino crítico del PIO.
     */


    /* --------------------------------------------------------
     * Arrancar ambas State Machines sincronizadas
     * --------------------------------------------------------
     */

    pio_enable_sm_mask_in_sync(
        rx_pio,
        (1u << rx_sm_cmd) |
        (1u << rx_sm_data)
    );


    rx_initialized = true;
}



/* ============================================================
 * ¿HAY ALGUNA PALABRA DISPONIBLE?
 * ============================================================
 */

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



/* ============================================================
 * OBTENER SIGUIENTE PALABRA
 * ============================================================
 */

uint32_t mil1553_rx_get(void)
{
    if (!rx_initialized)
    {
        return 0u;
    }


    // ========================================================
    // CMD / STATUS
    // ========================================================

    if (!pio_sm_is_rx_fifo_empty(
            rx_pio,
            rx_sm_cmd))
    {
        uint32_t raw =
            pio_sm_get(
                rx_pio,
                rx_sm_cmd
            );


        /*
         * SM0 observa P.
         *
         * Para CMD / STATUS,
         * los 17 bits recibidos ya tienen
         * la polaridad lógica correcta.
         */
        uint32_t payload =
            raw
            & MIL1553_RX_PAYLOAD_MASK;


        return

            MIL1553_RX_TYPE_CMD_STATUS
            |
            payload;
    }


    // ========================================================
    // DATA
    // ========================================================

    if (!pio_sm_is_rx_fifo_empty(
            rx_pio,
            rx_sm_data))
    {
        uint32_t raw =
            pio_sm_get(
                rx_pio,
                rx_sm_data
            );


        /*
         * SM1 observa N.
         *
         * Para DATA, la línea N contiene
         * la información complementada.
         *
         * Los 17 bits relevantes son:
         *
         *      bits 16..1 -> DATA[15:0]
         *      bit 0      -> paridad
         *
         * Por lo tanto invertimos esos bits
         * en Cortex.
         *
         * Primero invertimos todo el uint32_t
         * y posteriormente conservamos solamente
         * los 17 bits válidos.
         */
        uint32_t payload =
            (~raw)
            & MIL1553_RX_PAYLOAD_MASK;


        return

            MIL1553_RX_TYPE_DATA
            |
            payload;
    }


    return 0u;
}