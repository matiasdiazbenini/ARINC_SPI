#include "mil1553_tx.h"

#include <stdint.h>
#include <stdbool.h>

#include "pico/stdlib.h"

#include "hardware/clocks.h"
#include "hardware/pio.h"

#include "mil1553_tx.pio.h"


/* ============================================================
 * Configuración del PIO TX
 * ============================================================
 */

static PIO tx_pio = pio0;
static const uint tx_sm = 0u;

static uint tx_offset = 0u;
static bool tx_initialized = false;


/*
 * Frecuencia interna de ejecución del PIO.
 *
 * Bus MIL-STD-1553:
 *
 *      bitrate = 1 Mbit/s
 *      Tbit    = 1 us
 *      Thalf   = 500 ns
 *
 * PIO:
 *
 *      8 MHz
 *      1 ciclo = 125 ns
 *
 * Entonces:
 *
 *      medio bit = 4 ciclos PIO
 *      bit        = 8 ciclos PIO
 */

#define MIL1553_PIO_CLOCK_HZ 8000000.0f


/* ============================================================
 * Inicialización
 * ============================================================
 */

void mil1553_tx_init(void)
{
    if (tx_initialized) {
        return;
    }


    /* --------------------------------------------------------
     * Cargar programa PIO
     * --------------------------------------------------------
     */

    tx_offset =
        pio_add_program(
            tx_pio,
            &mil1553_tx_program);


    pio_sm_config c =
        mil1553_tx_program_get_default_config(
            tx_offset);


    /* --------------------------------------------------------
     * SIDE-SET
     *
     * Dos GPIO consecutivos:
     *
     * GP2 = P
     * GP3 = N
     *
     * side 0 -> P=0 N=0
     * side 1 -> P=1 N=0
     * side 2 -> P=0 N=1
     * --------------------------------------------------------
     */

    sm_config_set_sideset_pins(
        &c,
        BUS_PIN_P);

    /*
    * SET PINDIRS actuará sobre:
    *
    * bit 0 -> GP2 = P
    * bit 1 -> GP3 = N
    */
    sm_config_set_set_pins(
        &c,
        BUS_PIN_P,
        2u);
    /* --------------------------------------------------------
     * OSR
     *
     * Shift hacia la IZQUIERDA.
     *
     * Esto nos permite colocar el primer bit a transmitir
     * en el bit 31 del uint32_t.
     *
     * No usamos autopull:
     *
     * cada palabra MIL-STD-1553 corresponde exactamente
     * a un único pull explícito en el programa PIO.
     * --------------------------------------------------------
     */

    sm_config_set_out_shift(
        &c,
        false,      /* shift_right = false */
        false,      /* autopull    = false */
        32u);


    /* --------------------------------------------------------
     * FIFO dedicada a transmisión.
     * --------------------------------------------------------
     */

    sm_config_set_fifo_join(
        &c,
        PIO_FIFO_JOIN_TX);


    /* --------------------------------------------------------
     * Clock PIO = 8 MHz
     * --------------------------------------------------------
     */

    const float clkdiv =
        (float)clock_get_hz(clk_sys) /
        MIL1553_PIO_CLOCK_HZ;

    sm_config_set_clkdiv(
        &c,
        clkdiv);


    /* --------------------------------------------------------
     * GPIO
     * --------------------------------------------------------
     */

    pio_gpio_init(
        tx_pio,
        BUS_PIN_P);

    pio_gpio_init(
        tx_pio,
        BUS_PIN_N);


    /*
     * P y N son salidas.
     */

    pio_sm_set_consecutive_pindirs(
        tx_pio,
        tx_sm,
        BUS_PIN_P,
        2u,
        false);


    /* --------------------------------------------------------
     * Inicializar State Machine
     * --------------------------------------------------------
     */

    pio_sm_init(
        tx_pio,
        tx_sm,
        tx_offset,
        &c);


    /*
     * Estado inicial:
     *
     * P = 0
     * N = 0
     */

    pio_sm_set_pins_with_mask(
        tx_pio,
        tx_sm,
        0u,
        (1u << BUS_PIN_P) |
        (1u << BUS_PIN_N));


    /* --------------------------------------------------------
     * Habilitar State Machine
     * --------------------------------------------------------
     */

    pio_sm_set_enabled(
        tx_pio,
        tx_sm,
        true);


    tx_initialized = true;
}


/* ============================================================
 * Transmitir una palabra MIL-STD-1553
 * ============================================================
 */

void mil1553_tx_send_word(
    bus_1553_sync_t sync_type,
    uint16_t word)
{
    mil1553_tx_init();


    /* --------------------------------------------------------
     * La CPU entrega UNA sola palabra de 32 bits al PIO.
     *
     * Layout:
     *
     * bit 31:
     *
     *      selector de SYNC
     *
     *      1 = Command / Status
     *      0 = Data
     *
     *
     * bits 30..15:
     *
     *      16 bits de información
     *      MSB primero
     *
     *
     * bit 14:
     *
     *      paridad impar
     *
     *
     * bits 13..0:
     *
     *      no utilizados
     *
     *
     *  31     30                 15 14       0
     *
     * +----+-----------------------+--+-------+
     * |SYNC|      DATA[15:0]       |P | unused|
     * +----+-----------------------+--+-------+
     *
     * --------------------------------------------------------
     */


    uint32_t frame = 0u;


    /* --------------------------------------------------------
     * Selector de SYNC
     * --------------------------------------------------------
     */

    if (sync_type ==
        BUS_1553_SYNC_CMD_STATUS) {

        frame |= (1u << 31);
    }


    /* --------------------------------------------------------
     * Los 16 bits se colocan en posiciones 30..15.
     * --------------------------------------------------------
     */

    frame |=
        ((uint32_t)word << 15);


    /* --------------------------------------------------------
     * Paridad impar
     * --------------------------------------------------------
     */

    uint8_t parity =
        bus_compute_odd_parity(word);

    frame |=
        ((uint32_t)(parity & 0x01u) << 14);


    /* --------------------------------------------------------
     * ENTREGAR UNA SOLA PALABRA AL HARDWARE PIO.
     *
     * A partir de este punto la CPU no genera:
     *
     * - Manchester
     * - medios bits
     * - P/N
     * - SYNC temporal
     *
     * Eso lo hace la State Machine.
     * --------------------------------------------------------
     */

    pio_sm_put_blocking(
        tx_pio,
        tx_sm,
        frame);
}