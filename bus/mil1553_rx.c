#include "mil1553_rx.h"

#include <stdint.h>
#include <stdbool.h>

#include "pico/stdlib.h"

#include "hardware/clocks.h"
#include "hardware/pio.h"
#include "hardware/pio_instructions.h"

#include "bus.h"
#include "mil1553_rx.pio.h"


/* ============================================================
 * PIO
 * ============================================================
 */

static PIO rx_pio =
    pio1;


/*
 * SM0:
 *
 *      P
 *      CMD / STATUS
 *
 * SM1:
 *
 *      N
 *      DATA
 */
static const uint rx_sm_cmd =
    0u;


static const uint rx_sm_data =
    1u;


static uint rx_offset =
    0u;


static bool rx_initialized =
    false;


/* ============================================================
 * CLOCK RX
 * ============================================================
 */

#define MIL1553_RX_PIO_CLOCK_HZ    8000000.0f


/* ============================================================
 * IRQ INTERNA PIO
 * ============================================================
 *
 * IRQ 0:
 *
 * SM0 la genera despues de terminar
 * una palabra CMD / STATUS.
 *
 * SM1 puede esperar esta bandera
 * directamente dentro del PIO.
 * ============================================================
 */

#define MIL1553_RX_DATA_GATE_IRQ    0u


/* ============================================================
 * DIRECCIONES DE ENTRADA
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
 * CONFIGURAR STATE MACHINE
 * ============================================================
 */

static void configure_rx_sm(
    uint sm,
    uint input_pin,
    uint initial_pc)
{
    pio_sm_config c =
        mil1553_rx_program_get_default_config(
            rx_offset
        );


    /* --------------------------------------------------------
     * IN
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
     * FIFO RX ampliada
     * --------------------------------------------------------
     */

    sm_config_set_fifo_join(
        &c,
        PIO_FIFO_JOIN_RX
    );


    /* --------------------------------------------------------
     * ISR
     *
     * Shift hacia izquierda.
     * PUSH manual.
     * --------------------------------------------------------
     */

    sm_config_set_in_shift(
        &c,
        false,
        false,
        32u
    );


    /* --------------------------------------------------------
     * Clock = 8 MHz
     * --------------------------------------------------------
     */

    const float clkdiv =
        (float)clock_get_hz(
            clk_sys
        )
        /
        MIL1553_RX_PIO_CLOCK_HZ;


    sm_config_set_clkdiv(
        &c,
        clkdiv
    );


    /* --------------------------------------------------------
     * Inicializar SM en PC seleccionado.
     * --------------------------------------------------------
     */

    pio_sm_init(
        rx_pio,
        sm,
        initial_pc,
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


/* ============================================================
 * INICIALIZACION
 * ============================================================
 */

void mil1553_rx_init(void)
{
    if (rx_initialized)
    {
        return;
    }


    /* --------------------------------------------------------
     * Cargar programa
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
     * Receptor:
     *
     * P y N siempre entrada.
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


    gpio_pull_down(
        BUS_PIN_P
    );


    gpio_pull_down(
        BUS_PIN_N
    );


    /* --------------------------------------------------------
     * Limpiar IRQ interna.
     * --------------------------------------------------------
     */

    pio_interrupt_clear(
        rx_pio,
        MIL1553_RX_DATA_GATE_IRQ
    );


    /* --------------------------------------------------------
     * SM0
     *
     * Entrada normal:
     *
     *      cmd_entry
     * --------------------------------------------------------
     */

    configure_rx_sm(
        rx_sm_cmd,
        BUS_PIN_P,
        cmd_entry_pc()
    );


    /* --------------------------------------------------------
     * SM1
     *
     * Entrada especial:
     *
     *      data_entry
     *
     * Queda esperando IRQ 0.
     * --------------------------------------------------------
     */

    configure_rx_sm(
        rx_sm_data,
        BUS_PIN_N,
        data_entry_pc()
    );


    /* --------------------------------------------------------
     * Arrancar ambas sincronizadas.
     * --------------------------------------------------------
     */

    pio_enable_sm_mask_in_sync(
        rx_pio,
        (1u << rx_sm_cmd) |
        (1u << rx_sm_data)
    );


    rx_initialized =
        true;
}


/* ============================================================
 * HAY ALGUNA PALABRA
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
 * GET GENERICO
 * ============================================================
 */

uint32_t mil1553_rx_get(void)
{
    if (!rx_initialized)
    {
        return 0u;
    }


    /* ========================================================
     * CMD / STATUS
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


        uint32_t payload =
            raw &
            MIL1553_RX_PAYLOAD_MASK;


        return

            MIL1553_RX_TYPE_CMD_STATUS |
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
         * DATA se encuentra complementado.
         */
        uint32_t payload =
            (~raw) &
            MIL1553_RX_PAYLOAD_MASK;


        return

            MIL1553_RX_TYPE_DATA |
            payload;
    }


    return 0u;
}


/* ============================================================
 * CLEAR COMPLETO
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
     * Detener ambas SM.
     * --------------------------------------------------------
     */

    pio_set_sm_mask_enabled(
        rx_pio,
        sm_mask,
        false
    );


    /* --------------------------------------------------------
     * Muy importante:
     *
     * eliminar cualquier IRQ residual generada
     * por CMD / STATUS / DATA anterior.
     * --------------------------------------------------------
     */

    pio_interrupt_clear(
        rx_pio,
        MIL1553_RX_DATA_GATE_IRQ
    );


    /* --------------------------------------------------------
     * SM0 vuelve al receptor normal.
     * --------------------------------------------------------
     */

    configure_rx_sm(
        rx_sm_cmd,
        BUS_PIN_P,
        cmd_entry_pc()
    );


    /* --------------------------------------------------------
     * SM1 vuelve a esperar IRQ.
     * --------------------------------------------------------
     */

    configure_rx_sm(
        rx_sm_data,
        BUS_PIN_N,
        data_entry_pc()
    );


    /* --------------------------------------------------------
     * Arranque sincronizado.
     * --------------------------------------------------------
     */

    pio_enable_sm_mask_in_sync(
        rx_pio,
        sm_mask
    );
}


/* ============================================================
 * CMD / STATUS DISPONIBLE
 * ============================================================
 */

bool mil1553_rx_cmd_status_available(void)
{
    if (!rx_initialized)
    {
        return false;
    }


    return

        !pio_sm_is_rx_fifo_empty(
            rx_pio,
            rx_sm_cmd
        );
}


/* ============================================================
 * DATA DISPONIBLE
 * ============================================================
 */

bool mil1553_rx_data_available(void)
{
    if (!rx_initialized)
    {
        return false;
    }


    return

        !pio_sm_is_rx_fifo_empty(
            rx_pio,
            rx_sm_data
        );
}


/* ============================================================
 * GET CMD / STATUS
 * ============================================================
 */

uint32_t mil1553_rx_get_cmd_status(void)
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


    uint32_t raw =
        pio_sm_get(
            rx_pio,
            rx_sm_cmd
        );


    uint32_t payload =
        raw &
        MIL1553_RX_PAYLOAD_MASK;


    return

        MIL1553_RX_TYPE_CMD_STATUS |
        payload;
}


/* ============================================================
 * GET DATA
 * ============================================================
 */

uint32_t mil1553_rx_get_data(void)
{
    if (!rx_initialized)
    {
        return 0u;
    }


    if (pio_sm_is_rx_fifo_empty(
            rx_pio,
            rx_sm_data))
    {
        return 0u;
    }


    uint32_t raw =
        pio_sm_get(
            rx_pio,
            rx_sm_data
        );


    uint32_t payload =
        (~raw) &
        MIL1553_RX_PAYLOAD_MASK;


    return

        MIL1553_RX_TYPE_DATA |
        payload;
}


/* ============================================================
 * REARMAR SM1
 * ============================================================
 *
 * SM1 vuelve a:
 *
 *      data_entry
 *
 * donde queda esperando:
 *
 *      WAIT 1 IRQ 0
 *
 * Tambien eliminamos cualquier IRQ residual.
 * ============================================================
 */

void mil1553_rx_data_rearm(void)
{
    if (!rx_initialized)
    {
        return;
    }


    /* --------------------------------------------------------
     * Detener solamente DATA.
     * --------------------------------------------------------
     */

    pio_sm_set_enabled(
        rx_pio,
        rx_sm_data,
        false
    );


    /* --------------------------------------------------------
     * Eliminar IRQ residual.
     * --------------------------------------------------------
     */

    pio_interrupt_clear(
        rx_pio,
        MIL1553_RX_DATA_GATE_IRQ
    );


    /* --------------------------------------------------------
     * Reiniciar SM1 directamente en data_entry.
     * --------------------------------------------------------
     */

    configure_rx_sm(
        rx_sm_data,
        BUS_PIN_N,
        data_entry_pc()
    );


    /* --------------------------------------------------------
     * Volver a habilitar SM1.
     * --------------------------------------------------------
     */

    pio_sm_set_enabled(
        rx_pio,
        rx_sm_data,
        true
    );


    /*
     * Conservamos la resincronizacion de divisores
     * que veniamos probando.
     */
    pio_clkdiv_restart_sm_mask(
        rx_pio,
        (1u << rx_sm_cmd) |
        (1u << rx_sm_data)
    );
}


/* ============================================================
 * DESHABILITAR SM1
 * ============================================================
 */

void mil1553_rx_data_disable(void)
{
    if (!rx_initialized)
    {
        return;
    }


    pio_sm_set_enabled(
        rx_pio,
        rx_sm_data,
        false
    );
}
void mil1553_rx_cmd_status_rearm(void)
{
    if (!rx_initialized)
    {
        return;
    }

    pio_sm_set_enabled(
        rx_pio,
        rx_sm_cmd,
        false
    );

    pio_sm_clear_fifos(
        rx_pio,
        rx_sm_cmd
    );

    pio_sm_restart(
        rx_pio,
        rx_sm_cmd
    );

    pio_sm_exec(
        rx_pio,
        rx_sm_cmd,
        pio_encode_jmp(
            cmd_entry_pc()
        )
    );

    pio_sm_set_enabled(
        rx_pio,
        rx_sm_cmd,
        true
    );
}