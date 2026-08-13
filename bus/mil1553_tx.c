#include "mil1553_tx.h"

#include <stdint.h>
#include <stdbool.h>

#include "pico/stdlib.h"

#include "hardware/clocks.h"
#include "hardware/pio.h"

#include "mil1553_tx.pio.h"


/*
 * ============================================================
 * Configuración
 * ============================================================
 */

static PIO tx_pio = pio0;
static const uint tx_sm = 0u;

static uint tx_offset = 0u;
static bool tx_initialized = false;


/*
 * Símbolos diferenciales.
 *
 * bit 0 -> GP2 = P
 * bit 1 -> GP3 = N
 *
 * 01 -> P=1 N=0 -> H
 * 10 -> P=0 N=1 -> L
 */

#define MIL1553_SYMBOL_H 0x01u
#define MIL1553_SYMBOL_L 0x02u

#define MIL1553_SYMBOL_COUNT 40u
#define MIL1553_TX_WORDS      3u


/*
 * ============================================================
 * Construcción de la secuencia física
 * ============================================================
 */

static void append_symbol(uint32_t frame[MIL1553_TX_WORDS],
                          uint8_t *symbol_index,
                          uint8_t symbol)
{
    uint8_t index = *symbol_index;

    uint8_t fifo_word = index / 16u;
    uint8_t position  = index % 16u;

    uint32_t shift = (uint32_t)position * 2u;

    frame[fifo_word] |=
        ((uint32_t)(symbol & 0x03u) << shift);

    (*symbol_index)++;
}


static void append_manchester_bit(
    uint32_t frame[MIL1553_TX_WORDS],
    uint8_t *symbol_index,
    bool bit)
{
    /*
     * Manchester II utilizado:
     *
     * 1 -> H L
     * 0 -> L H
     */

    if (bit) {
        append_symbol(frame,
                      symbol_index,
                      MIL1553_SYMBOL_H);

        append_symbol(frame,
                      symbol_index,
                      MIL1553_SYMBOL_L);
    }
    else {
        append_symbol(frame,
                      symbol_index,
                      MIL1553_SYMBOL_L);

        append_symbol(frame,
                      symbol_index,
                      MIL1553_SYMBOL_H);
    }
}


/*
 * ============================================================
 * Inicialización
 * ============================================================
 */

void mil1553_tx_init(void)
{
    if (tx_initialized) {
        return;
    }

    tx_offset =
        pio_add_program(tx_pio,
                        &mil1553_tx_program);

    pio_sm_config c =
        mil1553_tx_program_get_default_config(tx_offset);

    /*
     * OUT controla GP2 y GP3.
     */
    sm_config_set_out_pins(&c,
                           BUS_PIN_P,
                           2u);

    /*
     * SET también controla GP2 y GP3,
     * para llevar ambas líneas a cero al finalizar.
     */
    sm_config_set_set_pins(&c,
                           BUS_PIN_P,
                           2u);

    /*
     * Shift hacia la derecha:
     * los símbolos se consumen desde los bits menos
     * significativos.
     *
     * Autopull cada 32 bits = 16 símbolos.
     */
    sm_config_set_out_shift(&c,
                            true,
                            true,
                            32u);

    /*
     * Toda la FIFO dedicada a TX.
     */
    sm_config_set_fifo_join(&c,
                            PIO_FIFO_JOIN_TX);


    /*
     * Cada medio bit ocupa 8 ciclos PIO.
     *
     * Tbit actual = 500 us
     * Thalf       = 250 us
     */

    const float half_bit_us =
        ((float)BIT_PERIOD_US) / 2.0f;

    const float pio_cycles_per_half_bit =
        8.0f;

    const float clkdiv =
        ((float)clock_get_hz(clk_sys) *
         (half_bit_us / 1000000.0f))
        /
        pio_cycles_per_half_bit;

    sm_config_set_clkdiv(&c, clkdiv);


    /*
     * Inicializar pines.
     */
    pio_gpio_init(tx_pio, BUS_PIN_P);
    pio_gpio_init(tx_pio, BUS_PIN_N);

    pio_sm_set_consecutive_pindirs(tx_pio,
                                   tx_sm,
                                   BUS_PIN_P,
                                   2u,
                                   true);

    pio_sm_init(tx_pio,
                tx_sm,
                tx_offset,
                &c);

    /*
     * Arranque en estado neutro.
     */
    pio_sm_set_pins_with_mask(
        tx_pio,
        tx_sm,
        0u,
        (1u << BUS_PIN_P) |
        (1u << BUS_PIN_N));

    pio_sm_set_enabled(tx_pio,
                       tx_sm,
                       true);

    tx_initialized = true;
}


/*
 * ============================================================
 * Transmisión de palabra MIL-STD-1553
 * ============================================================
 */

void mil1553_tx_send_word(bus_1553_sync_t sync_type,
                          uint16_t word)
{
    pio_sm_set_consecutive_pindirs(
    tx_pio,
    tx_sm,
    BUS_PIN_P,
    2u,
    true);
    mil1553_tx_init();

    uint32_t frame[MIL1553_TX_WORDS] = {
        0u,
        0u,
        0u
    };

    uint8_t symbol_index = 0u;


    /*
     * --------------------------------------------------------
     * SYNC
     * --------------------------------------------------------
     *
     * Command / Status:
     *
     * H H H L L L
     *
     * = H durante 1.5 T
     *   L durante 1.5 T
     *
     *
     * Data:
     *
     * L L L H H H
     */

    if (sync_type == BUS_1553_SYNC_CMD_STATUS) {

        append_symbol(frame, &symbol_index,
                      MIL1553_SYMBOL_H);

        append_symbol(frame, &symbol_index,
                      MIL1553_SYMBOL_H);

        append_symbol(frame, &symbol_index,
                      MIL1553_SYMBOL_H);

        append_symbol(frame, &symbol_index,
                      MIL1553_SYMBOL_L);

        append_symbol(frame, &symbol_index,
                      MIL1553_SYMBOL_L);

        append_symbol(frame, &symbol_index,
                      MIL1553_SYMBOL_L);
    }
    else {

        append_symbol(frame, &symbol_index,
                      MIL1553_SYMBOL_L);

        append_symbol(frame, &symbol_index,
                      MIL1553_SYMBOL_L);

        append_symbol(frame, &symbol_index,
                      MIL1553_SYMBOL_L);

        append_symbol(frame, &symbol_index,
                      MIL1553_SYMBOL_H);

        append_symbol(frame, &symbol_index,
                      MIL1553_SYMBOL_H);

        append_symbol(frame, &symbol_index,
                      MIL1553_SYMBOL_H);
    }


    /*
     * --------------------------------------------------------
     * 16 bits de información
     * --------------------------------------------------------
     *
     * MSB primero.
     */

    for (int bit = 15; bit >= 0; bit--) {

        bool value =
            ((word >> bit) & 0x01u) != 0u;

        append_manchester_bit(frame,
                              &symbol_index,
                              value);
    }


    /*
     * --------------------------------------------------------
     * Paridad impar
     * --------------------------------------------------------
     */

    uint8_t parity =
        bus_compute_odd_parity(word);

    append_manchester_bit(frame,
                          &symbol_index,
                          parity != 0u);


    /*
     * Tenemos exactamente:
     *
     * 6  símbolos SYNC
     * 32 símbolos DATA
     * 2  símbolos PARITY
     *
     * = 40 símbolos.
     */


    /*
     * --------------------------------------------------------
     * Enviar al PIO
     * --------------------------------------------------------
     */

    for (uint8_t i = 0;
         i < MIL1553_TX_WORDS;
         i++) {

        pio_sm_put_blocking(tx_pio,
                            tx_sm,
                            frame[i]);
    }
}
void mil1553_tx_release_bus(void)
{
    /*
     * La palabra ocupa exactamente 20 tiempos de bit.
     * Dejamos un pequeño margen antes de liberar P/N.
     */
    sleep_us((20u * BIT_PERIOD_US) +
             (BIT_PERIOD_US / 2u));

    /*
     * Alta impedancia lógica:
     * dejamos de conducir P y N.
     */
    pio_sm_set_consecutive_pindirs(
        tx_pio,
        tx_sm,
        BUS_PIN_P,
        2u,
        false);
}