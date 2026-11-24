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


/* ============================================================
 * STATE MACHINES
 * ============================================================
 *
 * SM0:
 *
 *      GP2 / P
 *      COMMAND / STATUS
 *
 * SM1:
 *
 *      GP3 / N
 *      DATA BC -> RT
 *      adquisicion directa
 *
 * SM2:
 *
 *      GP3 / N
 *      DATA RT -> BC
 *      gate por IRQ
 * ============================================================
 */

static const uint rx_sm_cmd =
    0u;


static const uint rx_sm_data_bc_to_rt =
    1u;


static const uint rx_sm_data_rt_to_bc =
    2u;


/* ============================================================
 * ESTADO
 * ============================================================
 */

static uint rx_offset =
    0u;


static bool rx_initialized =
    false;


/* ============================================================
 * CLOCK
 * ============================================================
 */

#define MIL1553_RX_PIO_CLOCK_HZ    8000000.0f


/* ============================================================
 * IRQ INTERNA
 * ============================================================
 */

#define MIL1553_RX_DATA_GATE_IRQ    0u


/* ============================================================
 * ENTRY PCs
 * ============================================================
 */

static inline uint cmd_entry_pc(void)
{
    return
        rx_offset +
        mil1553_rx_offset_cmd_entry;
}


static inline uint data_bc_to_rt_entry_pc(void)
{
    return
        rx_offset +
        mil1553_rx_offset_data_bc_to_rt_entry;
}


static inline uint data_rt_to_bc_entry_pc(void)
{
    return
        rx_offset +
        mil1553_rx_offset_data_rt_to_bc_entry;
}


/* ============================================================
 * CONFIGURAR SM
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
     * Shift izquierda.
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
     * CLOCK = 8 MHz
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
     * INIT
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
     * Todas las SM son receptores.
     *
     * Nunca conducen P ni N.
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
        rx_sm_data_bc_to_rt,
        BUS_PIN_P,
        2u,
        false
    );


    pio_sm_set_consecutive_pindirs(
        rx_pio,
        rx_sm_data_rt_to_bc,
        BUS_PIN_P,
        2u,
        false
    );


    /* --------------------------------------------------------
     * BUS IDLE
     * --------------------------------------------------------
     */

    gpio_pull_down(
        BUS_PIN_P
    );


    gpio_pull_down(
        BUS_PIN_N
    );


    /* --------------------------------------------------------
     * Limpiar IRQ
     * --------------------------------------------------------
     */

    pio_interrupt_clear(
        rx_pio,
        MIL1553_RX_DATA_GATE_IRQ
    );


    /* --------------------------------------------------------
     * SM0
     *
     * CMD / STATUS
     * P
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
     * DATA BC -> RT
     * N
     *
     * IMPORTANTE:
     *
     * ADQUISICION DIRECTA.
     *
     * NO espera IRQ del COMMAND.
     * --------------------------------------------------------
     */

    configure_rx_sm(
        rx_sm_data_bc_to_rt,
        BUS_PIN_N,
        data_bc_to_rt_entry_pc()
    );


    /* --------------------------------------------------------
     * SM2
     *
     * DATA RT -> BC
     * N
     *
     * Conserva gate por IRQ.
     * --------------------------------------------------------
     */

    configure_rx_sm(
        rx_sm_data_rt_to_bc,
        BUS_PIN_N,
        data_rt_to_bc_entry_pc()
    );


    /* --------------------------------------------------------
     * Arranque sincronizado
     * --------------------------------------------------------
     */

    const uint32_t sm_mask =
        (1u << rx_sm_cmd) |
        (1u << rx_sm_data_bc_to_rt) |
        (1u << rx_sm_data_rt_to_bc);


    pio_enable_sm_mask_in_sync(
    rx_pio,
    (1u << rx_sm_cmd) |
    (1u << rx_sm_data_bc_to_rt)
    );


    /*
     * Sincronizacion explicita de divisores.
     */
    pio_clkdiv_restart_sm_mask(
        rx_pio,
        sm_mask
    );


    rx_initialized =
        true;
}


/* ============================================================
 * AVAILABLE GENERICO
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
            rx_sm_data_bc_to_rt
        )
        ||
        !pio_sm_is_rx_fifo_empty(
            rx_pio,
            rx_sm_data_rt_to_bc
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


    /* --------------------------------------------------------
     * CMD / STATUS
     * --------------------------------------------------------
     */

    if (!pio_sm_is_rx_fifo_empty(
            rx_pio,
            rx_sm_cmd))
    {
        return
            mil1553_rx_get_cmd_status();
    }


    /* --------------------------------------------------------
     * DATA BC -> RT
     * --------------------------------------------------------
     */

    if (!pio_sm_is_rx_fifo_empty(
            rx_pio,
            rx_sm_data_bc_to_rt))
    {
        return
            mil1553_rx_get_data_bc_to_rt();
    }


    /* --------------------------------------------------------
     * DATA RT -> BC
     * --------------------------------------------------------
     */

    if (!pio_sm_is_rx_fifo_empty(
            rx_pio,
            rx_sm_data_rt_to_bc))
    {
        return
            mil1553_rx_get_data_rt_to_bc();
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
        (1u << rx_sm_data_bc_to_rt) |
        (1u << rx_sm_data_rt_to_bc);


    /* --------------------------------------------------------
     * Detener las tres SM
     * --------------------------------------------------------
     */

    pio_set_sm_mask_enabled(
        rx_pio,
        sm_mask,
        false
    );


    /* --------------------------------------------------------
     * Limpiar IRQ residual
     * --------------------------------------------------------
     */

    pio_interrupt_clear(
        rx_pio,
        MIL1553_RX_DATA_GATE_IRQ
    );


    /* --------------------------------------------------------
     * SM0
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
     * vuelve a adquisicion directa.
     * --------------------------------------------------------
     */

    configure_rx_sm(
        rx_sm_data_bc_to_rt,
        BUS_PIN_N,
        data_bc_to_rt_entry_pc()
    );


    /* --------------------------------------------------------
     * SM2
     *
     * vuelve a esperar IRQ.
     * --------------------------------------------------------
     */

    configure_rx_sm(
        rx_sm_data_rt_to_bc,
        BUS_PIN_N,
        data_rt_to_bc_entry_pc()
    );


    /* --------------------------------------------------------
     * Arranque sincronizado
     * --------------------------------------------------------
     */

    pio_enable_sm_mask_in_sync(
    rx_pio,
    (1u << rx_sm_cmd) |
    (1u << rx_sm_data_bc_to_rt)
    );


    /*
     * Mantener sincronizacion de divisores.
     */
    pio_clkdiv_restart_sm_mask(
        rx_pio,
        sm_mask
    );
}


/* ============================================================
 * CMD / STATUS AVAILABLE
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
 * CMD / STATUS GET
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
 * DATA BC -> RT AVAILABLE
 * ============================================================
 */

bool mil1553_rx_data_bc_to_rt_available(void)
{
    if (!rx_initialized)
    {
        return false;
    }


    return
        !pio_sm_is_rx_fifo_empty(
            rx_pio,
            rx_sm_data_bc_to_rt
        );
}


/* ============================================================
 * DATA BC -> RT GET
 * ============================================================
 */

uint32_t mil1553_rx_get_data_bc_to_rt(void)
{
    if (!rx_initialized)
    {
        return 0u;
    }


    if (pio_sm_is_rx_fifo_empty(
            rx_pio,
            rx_sm_data_bc_to_rt))
    {
        return 0u;
    }


    uint32_t raw =
        pio_sm_get(
            rx_pio,
            rx_sm_data_bc_to_rt
        );


    /*
     * DATA se observa desde N.
     *
     * Por eso invertimos los 17 bits utiles.
     */
    uint32_t payload =
        (~raw) &
        MIL1553_RX_PAYLOAD_MASK;


    return
        MIL1553_RX_TYPE_DATA |
        payload;
}


/* ============================================================
 * DATA RT -> BC AVAILABLE
 * ============================================================
 */

bool mil1553_rx_data_rt_to_bc_available(void)
{
    if (!rx_initialized)
    {
        return false;
    }


    return
        !pio_sm_is_rx_fifo_empty(
            rx_pio,
            rx_sm_data_rt_to_bc
        );
}


/* ============================================================
 * DATA RT -> BC GET
 * ============================================================
 */

uint32_t mil1553_rx_get_data_rt_to_bc(void)
{
    if (!rx_initialized)
    {
        return 0u;
    }


    if (pio_sm_is_rx_fifo_empty(
            rx_pio,
            rx_sm_data_rt_to_bc))
    {
        return 0u;
    }


    uint32_t raw =
        pio_sm_get(
            rx_pio,
            rx_sm_data_rt_to_bc
        );


    uint32_t payload =
        (~raw) &
        MIL1553_RX_PAYLOAD_MASK;


    return
        MIL1553_RX_TYPE_DATA |
        payload;
}


/* ============================================================
 * DATA GENERICO AVAILABLE
 * ============================================================
 */

bool mil1553_rx_data_available(void)
{
    if (!rx_initialized)
    {
        return false;
    }


    return
        mil1553_rx_data_bc_to_rt_available()
        ||
        mil1553_rx_data_rt_to_bc_available();
}


/* ============================================================
 * DATA GENERICO GET
 * ============================================================
 */

uint32_t mil1553_rx_get_data(void)
{
    if (!rx_initialized)
    {
        return 0u;
    }


    /*
     * Para compatibilidad con mains anteriores,
     * damos prioridad al camino BC -> RT.
     */
    if (mil1553_rx_data_bc_to_rt_available())
    {
        return
            mil1553_rx_get_data_bc_to_rt();
    }


    if (mil1553_rx_data_rt_to_bc_available())
    {
        return
            mil1553_rx_get_data_rt_to_bc();
    }


    return 0u;
}


/* ============================================================
 * REARME DATA
 * ============================================================
 *
 * SM1:
 *
 *      vuelve a adquisicion directa.
 *
 * SM2:
 *
 *      vuelve a data_rt_to_bc_entry
 *      esperando IRQ.
 * ============================================================
 */

void mil1553_rx_data_rearm(void)
{
    if (!rx_initialized)
    {
        return;
    }


    const uint32_t data_mask =
        (1u << rx_sm_data_bc_to_rt) |
        (1u << rx_sm_data_rt_to_bc);


    /* --------------------------------------------------------
     * Detener DATA
     * --------------------------------------------------------
     */

    pio_set_sm_mask_enabled(
        rx_pio,
        data_mask,
        false
    );


    /* --------------------------------------------------------
     * Limpiar IRQ residual
     * --------------------------------------------------------
     */

    pio_interrupt_clear(
        rx_pio,
        MIL1553_RX_DATA_GATE_IRQ
    );


    /* --------------------------------------------------------
     * SM1 BC -> RT
     *
     * ADQUISICION DIRECTA
     * --------------------------------------------------------
     */

    configure_rx_sm(
        rx_sm_data_bc_to_rt,
        BUS_PIN_N,
        data_bc_to_rt_entry_pc()
    );


    /* --------------------------------------------------------
     * SM2 RT -> BC
     *
     * GATE IRQ
     * --------------------------------------------------------
     */

    configure_rx_sm(
        rx_sm_data_rt_to_bc,
        BUS_PIN_N,
        data_rt_to_bc_entry_pc()
    );


    /* --------------------------------------------------------
     * Habilitar ambas
     * --------------------------------------------------------
     */

    pio_enable_sm_mask_in_sync(
    rx_pio,
    (1u << rx_sm_data_bc_to_rt)
    );

    /*
     * Mantener la resincronizacion de divisores
     * que ya habia dado buenos resultados.
     */
    pio_clkdiv_restart_sm_mask(
        rx_pio,
        (1u << rx_sm_cmd) |
        data_mask
    );
}


/* ============================================================
 * REARME RAPIDO CMD / STATUS
 * ============================================================
 */

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


/* ============================================================
 * DESHABILITAR DATA
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
        rx_sm_data_bc_to_rt,
        false
    );


    pio_sm_set_enabled(
        rx_pio,
        rx_sm_data_rt_to_bc,
        false
    );
}

/* Seleccionar RT -> BC tras validar el COMMAND.
 * SM0 sigue recibiendo STATUS sin interrupcion.
 * SM2 espera una sola IRQ (la del STATUS); el COMMAND ya se recibio.
 */
void mil1553_rx_select_rt_to_bc(void)
{
    if (!rx_initialized) return;
    const uint32_t data_mask = (1u << rx_sm_data_bc_to_rt) |
                               (1u << rx_sm_data_rt_to_bc);
    pio_set_sm_mask_enabled(rx_pio, data_mask, false);
    pio_sm_clear_fifos(rx_pio, rx_sm_data_bc_to_rt);
    pio_sm_clear_fifos(rx_pio, rx_sm_data_rt_to_bc);
    pio_sm_restart(rx_pio, rx_sm_data_rt_to_bc);
    pio_sm_exec(rx_pio, rx_sm_data_rt_to_bc,
                pio_encode_jmp(data_bc_to_rt_entry_pc()));
    pio_interrupt_clear(rx_pio, MIL1553_RX_DATA_GATE_IRQ);
    pio_sm_set_enabled(rx_pio, rx_sm_data_rt_to_bc, true);
}
