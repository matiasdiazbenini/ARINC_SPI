#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>

#include "pico/stdlib.h"
#include "hardware/clocks.h"
#include "hardware/pio.h"

#include "manchester_rx.pio.h"

#define RX_PIN_BASE 2u
#define RX_PIO      pio0
#define RX_SM       0u

#define BIT_PERIOD_US        1000u
#define SAMPLES_PER_BIT      16u
#define RX_SAMPLE_PERIOD_US  (BIT_PERIOD_US / SAMPLES_PER_BIT)

#define PN_IDLE    0x00u
#define PN_HIGH    0x01u   // P=1, N=0
#define PN_LOW     0x02u   // P=0, N=1
#define PN_INVALID 0x03u

#define CAPTURE_SAMPLES 512u

static uint32_t sample_word = 0;
static int sample_index = 16;

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

static uint8_t read_sample(void) {
    if (sample_index >= 16) {
        sample_word = pio_sm_get_blocking(RX_PIO, RX_SM);
        sample_index = 0;
    }

    uint8_t sample = get_sample_from_word(sample_word, sample_index);
    sample_index++;

    return sample;
}

static void capture_samples(uint8_t *buffer, uint32_t count) {
    for (uint32_t i = 0; i < count; i++) {
        buffer[i] = read_sample();
    }
}

static uint8_t majority_range(const uint8_t *buffer, int start, int count) {
    int h = 0;
    int l = 0;

    for (int i = start; i < start + count; i++) {
        if (buffer[i] == PN_HIGH) {
            h++;
        } else if (buffer[i] == PN_LOW) {
            l++;
        }
    }

    if (h > l) return PN_HIGH;
    if (l > h) return PN_LOW;
    return PN_INVALID;
}

static bool decode_bit_at_phase(const uint8_t *buffer, int start, bool *bit) {
    const int half = SAMPLES_PER_BIT / 2;

    uint8_t first = majority_range(buffer, start, half);
    uint8_t second = majority_range(buffer, start + half, half);

    if (first == PN_HIGH && second == PN_LOW) {
        *bit = true;
        return true;
    }

    if (first == PN_LOW && second == PN_HIGH) {
        *bit = false;
        return true;
    }

    return false;
}

static bool decode_byte_at_phase(const uint8_t *buffer, int start, uint8_t *byte) {
    uint8_t value = 0;

    for (int b = 0; b < 8; b++) {
        bool bit = false;
        int bit_start = start + b * SAMPLES_PER_BIT;

        if (!decode_bit_at_phase(buffer, bit_start, &bit)) {
            return false;
        }

        value = (uint8_t)((value << 1) | (bit ? 1u : 0u));
    }

    *byte = value;
    return true;
}

static void print_sample_line(const uint8_t *buffer, int start, int count) {
    for (int i = start; i < start + count; i++) {
        if (buffer[i] == PN_HIGH) {
            printf("H");
        } else if (buffer[i] == PN_LOW) {
            printf("L");
        } else if (buffer[i] == PN_IDLE) {
            printf("_");
        } else {
            printf("X");
        }
    }
}

static void rx_sampler_init(PIO pio, uint sm, uint pin_base) {
    uint offset = pio_add_program(pio, &manchester_rx_program);
    pio_sm_config c = manchester_rx_program_get_default_config(offset);

    sm_config_set_in_pins(&c, pin_base);

    /*
     * shift_right = false:
     * las muestras van quedando hacia MSB.
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

int main(void) {
    stdio_init_all();
    sleep_ms(1200);

    printf("RX PIO DIFFERENTIAL PHASE SCAN\n");
    printf("BIT_PERIOD_US=%u | SAMPLES_PER_BIT=%u | SAMPLE_PERIOD_US=%u\n",
           BIT_PERIOD_US,
           SAMPLES_PER_BIT,
           RX_SAMPLE_PERIOD_US);

    rx_sampler_init(RX_PIO, RX_SM, RX_PIN_BASE);

    while (true) {
        uint8_t samples[CAPTURE_SAMPLES];

        printf("CAPTURING...\n");
        capture_samples(samples, CAPTURE_SAMPLES);

        for (int phase = 0; phase < SAMPLES_PER_BIT; phase++) {
            int ok_count = 0;
            int f0_count = 0;
            int inv_f0_count = 0;

            printf("PHASE=%02d ", phase);

            for (int offset = phase;
                 offset + (8 * SAMPLES_PER_BIT) < CAPTURE_SAMPLES;
                 offset += (8 * SAMPLES_PER_BIT)) {
                uint8_t byte = 0;

                if (decode_byte_at_phase(samples, offset, &byte)) {
                    uint8_t inv = (uint8_t)(~byte);

                    ok_count++;

                    if (byte == 0xF0u) {
                        f0_count++;
                    }

                    if (inv == 0xF0u) {
                        inv_f0_count++;
                    }

                    printf("0x%02X ", byte);
                } else {
                    printf("-- ");
                }
            }

            printf("| OK=%d F0=%d INV_F0=%d | ",
                   ok_count,
                   f0_count,
                   inv_f0_count);

            print_sample_line(samples, phase, 64);
            printf("\n");
        }

        sleep_ms(500);
    }
}