#include <stdio.h>
#include <stdint.h>

#include "pico/stdlib.h"
#include "hardware/clocks.h"
#include "hardware/pio.h"

#include "arinc_gpio_link.pio.h"

/*
 * Enlace ARINC-like sobre dos GPIO:
 * - GP2 representa el simbolo de bit 0
 * - GP3 representa el simbolo de bit 1
 * No hay linea SYNC: la separacion entre palabras se hace con idle gap.
 *
 * Cambio importante:
 * el master ya no transmite una sola vez y termina. Ahora emite lotes
 * consecutivos de 1000 palabras. Eso evita que el slave o Flask queden
 * "fuera de fase" si arrancan unos segundos tarde.
 */
#define ARINC_TX_PIN_BASE    2u
#define BIT_RATE_HZ          100000u
#define HALF_CYCLES          5u
#define WORD_GAP_BITS        4u
#define WORDS_PER_BATCH      1000u
#define BATCH_GAP_MS         1500u
#define STARTUP_DELAY_MS     2500u
#define ENABLE_TX_WORD_LOG   0u
#define ENABLE_TX_BATCH_LOG  1u

typedef struct {
    uint8_t label;
    uint8_t sdi;
    const char *name;
} arinc_profile_t;

static uint8_t calc_odd_parity_31bits(uint32_t word_without_parity) {
    int ones = 0;

    for (int i = 0; i < 31; ++i) {
        if ((word_without_parity >> i) & 1u) {
            ++ones;
        }
    }

    return (ones % 2 == 0) ? 1u : 0u;
}

static uint32_t build_arinc_word(uint8_t label, uint8_t sdi, uint32_t data, uint8_t ssm) {
    uint32_t word = 0;

    word |= ((uint32_t)(label & 0xFFu)) << 0;
    word |= ((uint32_t)(sdi & 0x03u)) << 8;
    word |= ((uint32_t)(data & 0x7FFFFu)) << 10;
    word |= ((uint32_t)(ssm & 0x03u)) << 29;
    word |= ((uint32_t)calc_odd_parity_31bits(word)) << 31;

    return word;
}

static uint32_t encode_temperature(float temp_c) {
    float raw = (temp_c + 50.0f) / 0.25f;

    if (raw < 0.0f) {
        raw = 0.0f;
    }
    if (raw > 0x7FFFF) {
        raw = (float)0x7FFFF;
    }

    return (uint32_t)(raw + 0.5f);
}

static uint32_t encode_speed(float speed_kt) {
    if (speed_kt < 0.0f) {
        speed_kt = 0.0f;
    }
    if (speed_kt > 0x7FFFF) {
        speed_kt = (float)0x7FFFF;
    }

    return (uint32_t)(speed_kt + 0.5f);
}

static uint32_t encode_altitude(float altitude_ft) {
    float raw = altitude_ft / 10.0f;

    if (raw < 0.0f) {
        raw = 0.0f;
    }
    if (raw > 0x7FFFF) {
        raw = (float)0x7FFFF;
    }

    return (uint32_t)(raw + 0.5f);
}

static const char *ssm_to_text(uint8_t ssm) {
    switch (ssm) {
        case 3: return "NORMAL";
        case 1: return "NCD";
        case 2: return "FUNCTIONAL_TEST";
        case 0: return "FAILURE";
        default: return "DESCONOCIDO";
    }
}

/*
 * El CPU solo encola palabras; la temporizacion fina del bit y del gap
 * entre palabras queda dentro de la maquina de estado PIO.
 */
static void arinc_tx_program_init(PIO pio, uint sm, uint offset, uint pin_base, float bit_rate_hz) {
    pio_gpio_init(pio, pin_base + 0);
    pio_gpio_init(pio, pin_base + 1);

    pio_sm_set_consecutive_pindirs(pio, sm, pin_base, 2, true);

    pio_sm_config c = arinc_gpio_link_tx_program_get_default_config(offset);

    sm_config_set_set_pins(&c, pin_base, 2);
    sm_config_set_out_shift(&c, true, false, 32);
    sm_config_set_fifo_join(&c, PIO_FIFO_JOIN_TX);

    const float cycles_per_bit = 2.0f * (float)HALF_CYCLES;
    const float clkdiv = (float)clock_get_hz(clk_sys) / (bit_rate_hz * cycles_per_bit);

    sm_config_set_clkdiv(&c, clkdiv);

    pio_sm_init(pio, sm, offset, &c);
    pio_sm_set_pins_with_mask(pio, sm, 0u, (1u << pin_base) | (1u << (pin_base + 1)));
    pio_sm_set_enabled(pio, sm, true);
}

int main(void) {
    stdio_init_all();
    sleep_ms(STARTUP_DELAY_MS);

    printf("MASTER - ARINC-like por PIO sin SYNC\r\n");
    printf("TX pins: GP2=LINE_A, GP3=LINE_B\r\n");
    printf("Bit rate: %u bps\r\n", BIT_RATE_HZ);
    printf("Lote continuo: %u palabras por batch\r\n\r\n", WORDS_PER_BATCH);

    PIO pio = pio0;
    uint sm = 0;
    uint offset = pio_add_program(pio, &arinc_gpio_link_tx_program);

    arinc_tx_program_init(pio, sm, offset, ARINC_TX_PIN_BASE, (float)BIT_RATE_HZ);

    const arinc_profile_t profiles[] = {
        {0xA5, 0x0, "TEMPERATURA"},
        {0xB1, 0x1, "VELOCIDAD"},
        {0xC2, 0x2, "ALTITUD"},
    };

    float temp_c = 20.0f;
    float speed_kt = 120.0f;
    float altitude_ft = 1000.0f;
    uint32_t batch_number = 0;
    uint32_t global_word_index = 0;

    while (true) {
        ++batch_number;

#if ENABLE_TX_BATCH_LOG
        printf("[INFO] Iniciando batch %lu de %u palabras\r\n",
               (unsigned long)batch_number,
               WORDS_PER_BATCH);
#endif

        for (uint32_t i = 0; i < WORDS_PER_BATCH; ++i) {
            const uint32_t idx = global_word_index % 3u;
            const arinc_profile_t profile = profiles[idx];

            uint8_t ssm;
            const uint32_t ssm_cycle = (global_word_index / 3u) % 4u;

            if (ssm_cycle == 0u) {
                ssm = 3u;
            } else if (ssm_cycle == 1u) {
                ssm = 1u;
            } else if (ssm_cycle == 2u) {
                ssm = 2u;
            } else {
                ssm = 0u;
            }

            uint32_t raw;
            if (idx == 0u) {
                raw = encode_temperature(temp_c);
            } else if (idx == 1u) {
                raw = encode_speed(speed_kt);
            } else {
                raw = encode_altitude(altitude_ft);
            }

            const uint32_t word = build_arinc_word(profile.label, profile.sdi, raw, ssm);
            pio_sm_put_blocking(pio, sm, word);

#if ENABLE_TX_WORD_LOG
            printf("TX %08lu -> WORD: 0x%08lX | LABEL: 0x%02X (%s) | RAW: %lu | SDI: %u | SSM: %u (%s)\r\n",
                   (unsigned long)(global_word_index + 1u),
                   (unsigned long)word,
                   profile.label,
                   profile.name,
                   (unsigned long)raw,
                   profile.sdi,
                   ssm,
                   ssm_to_text(ssm));
#endif

            if (idx == 0u) {
                temp_c += 1.5f;
            } else if (idx == 1u) {
                speed_kt += 7.0f;
            } else {
                altitude_ft += 250.0f;
            }

            ++global_word_index;
        }

        const uint32_t drain_time_us = ((32u + WORD_GAP_BITS) * 1000000u) / BIT_RATE_HZ;
        sleep_us(drain_time_us + 20u);

#if ENABLE_TX_BATCH_LOG
        printf("[INFO] Batch %lu enviado. Pausa de %u ms antes del siguiente.\r\n",
               (unsigned long)batch_number,
               BATCH_GAP_MS);
#endif

        sleep_ms(BATCH_GAP_MS);
    }
}
