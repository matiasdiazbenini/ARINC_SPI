
#include "mil1553_rx.h"

#include <stdint.h>
#include <stdbool.h>

#include "pico/stdlib.h"

#include "hardware/clocks.h"
#include "hardware/pio.h"

#include "bus.h"
#include "mil1553_rx.pio.h"


/* ============================================================
 * CONFIGURACION DEL RECEPTOR
 * ============================================================
 */

static PIO rx_pio = pio1;


/*
 * SM0:
 * GP2 / P
 * COMMAND / STATUS
 *
 * SM1:
 * GP3 / N
 * DATA con gate por IRQ0
 */

static const uint rx_sm_cmd  = 0u;
static const uint rx_sm_data = 1u;


static uint rx_offset = 0u;

static bool rx_initialized = false;


/*
 * Frecuencia de trabajo PIO:
 *
 * 8 MHz
 *
 * 1 ciclo = 125 ns
 */

#define MIL1553_RX_PIO_CLOCK_HZ 8000000.0f


/*
 * IRQ interna del PIO utilizada para sincronizar
 * la recepcion COMMAND -> DATA.
 */

#define MIL1553_RX_DATA_GATE_IRQ 0u



/* ============================================================
 * PUNTOS DE ENTRADA DEL PROGRAMA PIO
 * ============================================================
 */

static inline uint cmd_entry_pc(void)
{
    return
        rx_offset +
        mil1553_rx_offset_cmd_entry;
}


static inline uint data_entry_pc(void)
{
    return
        rx_offset +
        mil1553_rx_offset_data_entry;
}



/* ============================================================
 * CONFIGURAR UNA STATE MACHINE RX
 * ============================================================
 */

static void configure_rx_sm(
    uint sm,
    uint input_pin,
    uint initial_pc)
{
    /*
     * Configuracion base generada por pioasm.
     */

    pio_sm_config c =
        mil1553_rx_program_get_default_config(
            rx_offset
        );


    /* --------------------------------------------------------
     * IN PINS
     * --------------------------------------------------------
     */

    sm_config_set_in_pins(
        &c,
        input_pin
    );


    /* --------------------------------------------------------
     * JMP PIN
     * --------------------------------------------------------
     */

    sm_config_set_jmp_pin(
        &c,
        input_pin
    );


    /* --------------------------------------------------------
     * FIFO RX dedicada
     *
     * Capacidad: 8 palabras.
     * --------------------------------------------------------
     */

    sm_config_set_fifo_join(
        &c,
        PIO_FIFO_JOIN_RX
    );


    /* --------------------------------------------------------
     * CONFIGURACION DEL ISR
     *
     * Shift hacia la izquierda.
     * Sin autopush.
     * PUSH explicito desde el PIO.
     * --------------------------------------------------------
     */

    sm_config_set_in_shift(
        &c,
        false,
        false,
        32u
    );


    /* --------------------------------------------------------
     * CLOCK DIVIDER
     * --------------------------------------------------------
     */

    const float clkdiv =
        (float)clock_get_hz(clk_sys)
        /
        MIL1553_RX_PIO_CLOCK_HZ;


    sm_config_set_clkdiv(
        &c,
        clkdiv
    );


    /* --------------------------------------------------------
     * INICIALIZAR STATE MACHINE
     *
     * SM0 comienza en cmd_entry.
     * SM1 comienza en data_entry.
     * --------------------------------------------------------
     */

    pio_sm_init(
        rx_pio,
        sm,
        initial_pc,
        &c
    );


    /* --------------------------------------------------------
     * LIMPIAR FIFO
     * --------------------------------------------------------
     */

    pio_sm_clear_fifos(
        rx_pio,
        sm
    );


    /* --------------------------------------------------------
     * REINICIAR LOGICA INTERNA
     * --------------------------------------------------------
     */

    pio_sm_restart(
        rx_pio,
        sm
    );
}



/* ============================================================
 * INICIALIZACION RX
 * ============================================================
 */

void mil1553_rx_init(void)
{
    if (rx_initialized)
    {
        return;
    }


    /* --------------------------------------------------------
     * CARGAR PROGRAMA PIO
     * --------------------------------------------------------
     */

    rx_offset =
        pio_add_program(
            rx_pio,
            &mil1553_rx_program
        );


    /* --------------------------------------------------------
     * CONFIGURACION GPIO
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


    /* --------------------------------------------------------
     * PINES COMO ENTRADA
     * --------------------------------------------------------
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
     * ESTADO DE REPOSO
     * --------------------------------------------------------
     */

    gpio_pull_down(
        BUS_PIN_P
    );


    gpio_pull_down(
        BUS_PIN_N
    );


    /* --------------------------------------------------------
     * LIMPIAR IRQ0
     *
     * Evita que SM1 arranque utilizando
     * una bandera residual.
     * --------------------------------------------------------
     */

    pio_interrupt_clear(
        rx_pio,
        MIL1553_RX_DATA_GATE_IRQ
    );


    /* --------------------------------------------------------
     * CONFIGURAR SM0
     *
     * GP2 / P
     *
     * Adquisicion directa COMMAND / STATUS.
     * --------------------------------------------------------
     */

    configure_rx_sm(
        rx_sm_cmd,
        BUS_PIN_P,
        cmd_entry_pc()
    );


    /* --------------------------------------------------------
     * CONFIGURAR SM1
     *
     * GP3 / N
     *
     * Espera inicial de IRQ0.
     * --------------------------------------------------------
     */

    configure_rx_sm(
        rx_sm_data,
        BUS_PIN_N,
        data_entry_pc()
    );


    /* --------------------------------------------------------
     * ARRANCAR AMBAS STATE MACHINES SINCRONIZADAS
     * --------------------------------------------------------
     */

    const uint32_t sm_mask =
        (1u << rx_sm_cmd) |
        (1u << rx_sm_data);


    pio_enable_sm_mask_in_sync(
        rx_pio,
        sm_mask
    );


    rx_initialized = true;
}



/* ============================================================
 * HAY ALGUNA PALABRA DISPONIBLE?
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


    /* ========================================================
     * COMMAND / STATUS
     * ========================================================
     */

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
         * No requiere inversion de polaridad.
         */

        uint32_t payload =
            raw &
            MIL1553_RX_PAYLOAD_MASK;


        return

            MIL1553_RX_TYPE_CMD_STATUS
            |
            payload;
    }


    /* ========================================================
     * DATA
     * ========================================================
     */

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
         * Invertir los 17 bits relevantes:
         *
         * Bits 16..1 = DATA
         * Bit  0     = PARITY
         */

        uint32_t payload =
            (~raw) &
            MIL1553_RX_PAYLOAD_MASK;


        return

            MIL1553_RX_TYPE_DATA
            |
            payload;
    }


    return 0u;
}



/* ============================================================
 * LIMPIAR Y REARMAR RECEPTOR
 * ============================================================
 *
 * Se ejecuta:
 *
 * - Durante la inicializacion.
 * - Despues de transmitir STATUS.
 * - Despues de un timeout.
 *
 * SM0 vuelve a esperar COMMAND.
 *
 * SM1 vuelve a esperar IRQ0.
 *
 * ============================================================
 */

void mil1553_rx_clear(void)
{
    if (!rx_initialized)
    {
        return;
    }


    const uint32_t sm_mask =
        (1u << rx_sm_cmd) |
        (1u << rx_sm_data);


    /* --------------------------------------------------------
     * DETENER AMBAS STATE MACHINES
     * --------------------------------------------------------
     */

    pio_set_sm_mask_enabled(
        rx_pio,
        sm_mask,
        false
    );


    /* --------------------------------------------------------
     * LIMPIAR IRQ0
     *
     * Las palabras DATA anteriores pueden haber dejado
     * IRQ0 activada.
     *
     * Se limpia con ambas SM detenidas.
     * --------------------------------------------------------
     */

    pio_interrupt_clear(
        rx_pio,
        MIL1553_RX_DATA_GATE_IRQ
    );


    /* --------------------------------------------------------
     * REINICIALIZAR SM0
     *
     * Esperar nuevo COMMAND.
     * --------------------------------------------------------
     */

    configure_rx_sm(
        rx_sm_cmd,
        BUS_PIN_P,
        cmd_entry_pc()
    );


    /* --------------------------------------------------------
     * REINICIALIZAR SM1
     *
     * Esperar IRQ0 del nuevo COMMAND.
     * --------------------------------------------------------
     */

    configure_rx_sm(
        rx_sm_data,
        BUS_PIN_N,
        data_entry_pc()
    );


    /* --------------------------------------------------------
     * ARRANCAR AMBAS SM SINCRONIZADAS
     * --------------------------------------------------------
     */

    pio_enable_sm_mask_in_sync(
        rx_pio,
        sm_mask
    );
}
