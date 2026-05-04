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
#define SAMPLES_PER_BIT      12u
#define RX_SAMPLE_PERIOD_US  (BIT_PERIOD_US / SAMPLES_PER_BIT)

#define RX_PHASE 5u

#define PN_IDLE    0x00u
#define PN_HIGH    0x01u   // P=1, N=0
#define PN_LOW     0x02u   // P=0, N=1
#define PN_INVALID 0x03u

#define CAPTURE_SAMPLES 2048u

static uint32_t sample_word = 0;
static int sample_index = 16;

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

    if (h > l) {
        return PN_HIGH;
    }

    if (l > h) {
        return PN_LOW;
    }

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

    if (byte == NULL) {
        return false;
    }

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
static bool find_byte_near(const uint8_t *samples,
                           int center,
                           int radius,
                           uint8_t target,
                           uint8_t *found_byte,
                           int *found_offset) {
    for (int delta = -radius; delta <= radius; delta++) {
        int pos = center + delta;

        if (pos < 0) {
            continue;
        }

        if (pos + (8 * SAMPLES_PER_BIT) >= CAPTURE_SAMPLES) {
            continue;
        }

        uint8_t b = 0;

        if (decode_byte_at_phase(samples, pos, &b)) {
            if (b == target) {
                if (found_byte != NULL) {
                    *found_byte = b;
                }

                if (found_offset != NULL) {
                    *found_offset = pos;
                }

                return true;
            }
        }
    }

    return false;
}
static void rx_sampler_init(PIO pio, uint sm, uint pin_base) {
    uint offset = pio_add_program(pio, &manchester_rx_program);
    pio_sm_config c = manchester_rx_program_get_default_config(offset);

    sm_config_set_in_pins(&c, pin_base);

    /*
     * Cada muestra usa 2 bits: P,N.
     * Autopush cada 32 bits => 16 muestras por palabra FIFO.
     */
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

int main(void) {
    stdio_init_all();
    sleep_ms(1200);

    printf("RX PIO FIXED PHASE DECODER\n");
    printf("BIT_PERIOD_US=%u | SAMPLES_PER_BIT=%u | RX_PHASE=%u\n",
           BIT_PERIOD_US,
           SAMPLES_PER_BIT,
           RX_PHASE);

    rx_sampler_init(RX_PIO, RX_SM, RX_PIN_BASE);

    while (true) {
        uint8_t samples[CAPTURE_SAMPLES];

        capture_samples(samples, CAPTURE_SAMPLES);

        int sync_count = 0;
        int decoded_count = 0;
        int first_sync_offset = -1;
        int bad_prints = 0;

        const int byte_samples = 8 * SAMPLES_PER_BIT;

        for (int offset = 0;
            offset + (5 * byte_samples) < CAPTURE_SAMPLES;
            offset++) {

            uint8_t sync = 0;

            if (decode_byte_at_phase(samples, offset, &sync)) {
                decoded_count++;

                if (sync == 0xF0u) {
                    sync_count++;

                    const int search_radius = 12;

                    uint8_t b1 = 0;
                    uint8_t b2 = 0;
                    uint8_t d0h = 0;
                    uint8_t d0l = 0;

                    int off_b1 = -1;
                    int off_b2 = -1;
                    int off_d0h = -1;
                    int off_d0l = -1;

                    bool ok_b1 = find_byte_near(samples,
                                                offset + 1 * byte_samples,
                                                search_radius,
                                                0x18u,
                                                &b1,
                                                &off_b1);

                    bool ok_b2 = find_byte_near(samples,
                                                offset + 2 * byte_samples,
                                                search_radius,
                                                0x23u,
                                                &b2,
                                                &off_b2);

                    bool ok_d0h = find_byte_near(samples,
                                                offset + 3 * byte_samples,
                                                search_radius,
                                                0xA0u,
                                                &d0h,
                                                &off_d0h);

                    bool ok_d0l = find_byte_near(samples,
                                                offset + 4 * byte_samples,
                                                search_radius,
                                                0x00u,
                                                &d0l,
                                                &off_d0l);

                    if (ok_b1 && ok_b2 && ok_d0h && ok_d0l) {
                        uint16_t cmd = ((uint16_t)b1 << 8) | b2;
                        uint16_t d0 = ((uint16_t)d0h << 8) | d0l;

                        printf("FRAME_OK|CMD=0x%04X|D0=0x%04X|OFF=%d,%d,%d,%d\n",
                            cmd,
                            d0,
                            off_b1,
                            off_b2,
                            off_d0h,
                            off_d0l);
                    } else {
                        printf("FRAME_SEARCH_FAIL|ok=%u%u%u%u|B=%02X %02X|D0=%02X %02X\n",
                            ok_b1 ? 1u : 0u,
                            ok_b2 ? 1u : 0u,
                            ok_d0h ? 1u : 0u,
                            ok_d0l ? 1u : 0u,
                            b1,
                            b2,
                            d0h,
                            d0l);
                    }
                }
            }
        }

        printf("SLIDING_SCAN | DECODED=%d | SYNC_COUNT=%d",
            decoded_count,
            sync_count);

        if (sync_count >= 2 && first_sync_offset >= 0) {
            printf(" | SYNC_LOCKED | OFFSET=%d\n", first_sync_offset);
        } else {
            printf(" | SYNC_NOT_FOUND\n");
        }

        sleep_ms(100);
    }
}