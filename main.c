#include <stdio.h>
#include <stdint.h>

#include "pico/stdlib.h"
#include "hardware/clocks.h"
#include "hardware/pio.h"

#include "manchester_rx.pio.h"

#define RX_PIN_BASE 2u
#define RX_PIO      pio0
#define RX_SM       0u

#define BIT_PERIOD_US        1000u
#define SAMPLES_PER_BIT      10u
#define RX_SAMPLE_PERIOD_US  (BIT_PERIOD_US / SAMPLES_PER_BIT)

static void rx_sampler_init(PIO pio, uint sm, uint pin_base) {
    uint offset = pio_add_program(pio, &manchester_rx_program);
    pio_sm_config c = manchester_rx_program_get_default_config(offset);

    sm_config_set_in_pins(&c, pin_base);

    /*
     * shift_right = false:
     * las muestras van quedando en orden hacia MSB.
     * autopush = true cada 32 bits.
     * Cada muestra usa 2 bits, entonces cada palabra trae 16 muestras.
     */
    sm_config_set_in_shift(&c, false, true, 32);
    sm_config_set_fifo_join(&c, PIO_FIFO_JOIN_RX);

    pio_gpio_init(pio, pin_base);
    pio_gpio_init(pio, pin_base + 1u);
    pio_sm_set_consecutive_pindirs(pio, sm, pin_base, 2, false);

    /*
     * Programa: 1 instrucción por muestra.
     * Frecuencia de muestreo = SAMPLES_PER_BIT / BIT_PERIOD.
     */
    const float sample_hz = 1000000.0f / (float)RX_SAMPLE_PERIOD_US;
    const float clkdiv = (float)clock_get_hz(clk_sys) / sample_hz;

    sm_config_set_clkdiv(&c, clkdiv);

    pio_sm_init(pio, sm, offset, &c);
    pio_sm_set_enabled(pio, sm, true);
}

static uint8_t get_sample_from_word(uint32_t raw, int index) {
    /*
     * Con shift_left, tomamos las muestras desde MSB:
     * index 0 -> bits 31..30
     * index 1 -> bits 29..28
     * ...
     */
    const int shift = 30 - (index * 2);
    return (uint8_t)((raw >> shift) & 0x03u);
}

static const char *pn_name(uint8_t pn) {
    switch (pn) {
        case 0x01u: return "H";     // P=1,N=0
        case 0x02u: return "L";     // P=0,N=1
        case 0x00u: return "_";     // idle
        default:    return "X";     // invalid
    }
}

int main(void) {
    stdio_init_all();
    sleep_ms(1200);

    printf("RX PIO DIFFERENTIAL SAMPLER\n");
    rx_sampler_init(RX_PIO, RX_SM, RX_PIN_BASE);

    while (true) {
        uint32_t raw = pio_sm_get_blocking(RX_PIO, RX_SM);

        printf("SAMPLES: ");
        for (int i = 0; i < 16; i++) {
            uint8_t pn = get_sample_from_word(raw, i);
            printf("%s", pn_name(pn));
        }
        printf("\n");
    }
}