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
#define SAMPLES_PER_BIT      8u
#define RX_SAMPLE_PERIOD_US  (BIT_PERIOD_US / SAMPLES_PER_BIT)

#define PN_IDLE    0x00u
#define PN_HIGH    0x01u
#define PN_LOW     0x02u
#define PN_INVALID 0x03u

static uint32_t sample_word = 0;
static int sample_index = 16;

static void rx_sampler_init(PIO pio, uint sm, uint pin_base) {
    uint offset = pio_add_program(pio, &manchester_rx_program);
    pio_sm_config c = manchester_rx_program_get_default_config(offset);

    sm_config_set_in_pins(&c, pin_base);
    sm_config_set_in_shift(&c, false, true, 32);
    sm_config_set_fifo_join(&c, PIO_FIFO_JOIN_RX);

    pio_gpio_init(pio, pin_base);
    pio_gpio_init(pio, pin_base + 1u);
    pio_sm_set_consecutive_pindirs(pio, sm, pin_base, 2, false);

    const float sample_hz = 1000000.0f / (float)RX_SAMPLE_PERIOD_US;
    const float clkdiv = (float)clock_get_hz(clk_sys) / sample_hz;
    sm_config_set_clkdiv(&c, clkdiv);

    pio_sm_init(pio, sm, offset, &c);
    pio_sm_set_enabled(pio, sm, true);
}

static uint8_t get_sample_from_word(uint32_t raw, int index) {
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

static bool is_valid_level(uint8_t pn) {
    return pn == PN_HIGH || pn == PN_LOW;
}

static uint8_t majority_level(uint8_t *samples, int start, int count) {
    int h = 0;
    int l = 0;

    for (int i = start; i < start + count; i++) {
        if (samples[i] == PN_HIGH) {
            h++;
        } else if (samples[i] == PN_LOW) {
            l++;
        }
    }

    if (h > l) return PN_HIGH;
    if (l > h) return PN_LOW;
    return PN_INVALID;
}

/*
 * Busca alternancia H/L sostenida, típica de 0xAA.
 * No decodifica bytes todavía; solo engancha fase aproximada.
 */
static bool wait_preamble_lock(void) {
    uint8_t prev = PN_IDLE;
    uint8_t curr = PN_IDLE;
    int transitions = 0;

    printf("WAIT_PREAMBLE\n");

    while (true) {
        curr = read_sample();

        if (!is_valid_level(curr)) {
            transitions = 0;
            prev = curr;
            continue;
        }

        if (is_valid_level(prev) && curr != prev) {
            transitions++;

            if (transitions >= 40) {
                printf("PREAMBLE_LOCKED\n");
                return true;
            }
        }

        prev = curr;
    }
}

/*
 * Lee un bit usando 8 muestras.
 * Primera mitad y segunda mitad por mayoría.
 */
static bool read_bit_windowed(bool *bit) {
    uint8_t samples[SAMPLES_PER_BIT];

    for (int i = 0; i < SAMPLES_PER_BIT; i++) {
        samples[i] = read_sample();
    }

    uint8_t first = majority_level(samples, 0, SAMPLES_PER_BIT / 2);
    uint8_t second = majority_level(samples, SAMPLES_PER_BIT / 2, SAMPLES_PER_BIT / 2);

    if (first == PN_HIGH && second == PN_LOW) {
        *bit = true;
        return true;
    }

    if (first == PN_LOW && second == PN_HIGH) {
        *bit = false;
        return true;
    }

    printf("BIT_WINDOW_ERROR|");
    for (int i = 0; i < SAMPLES_PER_BIT; i++) {
        if (samples[i] == PN_HIGH) printf("H");
        else if (samples[i] == PN_LOW) printf("L");
        else if (samples[i] == PN_IDLE) printf("_");
        else printf("X");
    }
    printf("\n");

    return false;
}

static bool read_byte_windowed(uint8_t *byte) {
    uint8_t value = 0;

    for (int i = 0; i < 8; i++) {
        bool bit = false;

        if (!read_bit_windowed(&bit)) {
            return false;
        }

        value = (uint8_t)((value << 1) | (bit ? 1u : 0u));
    }

    *byte = value;
    return true;
}

int main(void) {
    stdio_init_all();
    sleep_ms(1200);

    printf("RX PREAMBLE LOCK TEST\n");
    rx_sampler_init(RX_PIO, RX_SM, RX_PIN_BASE);

    while (true) {
        wait_preamble_lock();

        /*
         * Después del lock descartamos algunas muestras para caer más cerca
         * del límite de byte siguiente. Esto se ajusta si hace falta.
         */
        for (int phase = 0; phase < 16; phase++) {
            for (int i = 0; i < phase; i++) {
                (void)read_sample();
            }

            uint8_t sync = 0;

            if (read_byte_windowed(&sync)) {
                printf("PHASE=%d|RX=0x%02X\n", phase, sync);
            } else {
                printf("PHASE=%d|READ_ERROR\n", phase);
            }
        }

        uint8_t sync = 0;

        if (read_byte_windowed(&sync)) {
            printf("RX_AFTER_PREAMBLE=0x%02X\n", sync);

            if (sync == 0xF0u) {
                printf("RX_SYNC_OK\n");
            } else {
                printf("RX_SYNC_BAD\n");
            }
        } else {
            printf("RX_SYNC_READ_ERROR\n");
        }

        sleep_ms(100);
    }
}