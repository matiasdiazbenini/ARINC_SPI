#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>

#include "pico/stdlib.h"
#include "hardware/clocks.h"
#include "hardware/pio.h"

#include "arinc429_logic.h"

#if ARINC429_LOGIC_MODE
#include "arinc429_logic.pio.h"
#else
#include "arinc_gpio_link.pio.h"
#endif

/*
 * Master con dos enlaces simplex ARINC-like:
 * - canal directo GP2/GP3: master -> slave
 * - canal inverso GP4/GP5: slave -> master (ACK)
 *
 * Esto se acerca mas a la topologia real de ARINC 429: un canal por sentido,
 * en lugar de forzar ida y vuelta sobre el mismo par de pines.
 */
#define ARINC_FWD_PIN_BASE      2u
#define ARINC_REV_PIN_BASE      4u
#ifndef MASTER_BIT_RATE_HZ
#define MASTER_BIT_RATE_HZ      100000u
#endif
#define BIT_RATE_HZ             ((uint32_t)MASTER_BIT_RATE_HZ)
#define HALF_CYCLES             5u
#ifndef MASTER_WORD_GAP_BITS
#define MASTER_WORD_GAP_BITS    4u
#endif
#define WORD_GAP_BITS           ((uint32_t)MASTER_WORD_GAP_BITS)
#ifndef MASTER_WORDS_PER_BATCH
#define MASTER_WORDS_PER_BATCH  1000u
#endif
#define WORDS_PER_BATCH         ((uint32_t)MASTER_WORDS_PER_BATCH)
#define TX_PATTERN_WORDS        10u
#define VALID_PATTERN_WORDS     3u
#define NOISE_PATTERN_WORDS     (TX_PATTERN_WORDS - VALID_PATTERN_WORDS)
#define STARTUP_DELAY_MS        2500u
#define ACK_LABEL               0xACu
#define ACK_SDI                 0x03u
#define ACK_WAIT_LOG_MS         1500u
#ifndef MASTER_POST_ACK_GUARD_US
#define MASTER_POST_ACK_GUARD_US 500u
#endif
#define POST_ACK_GUARD_US       ((uint32_t)MASTER_POST_ACK_GUARD_US)
#define ENABLE_TX_WORD_LOG      0u
#ifndef MASTER_ENABLE_TX_BATCH_LOG
#define MASTER_ENABLE_TX_BATCH_LOG 1u
#endif
#define ENABLE_TX_BATCH_LOG     MASTER_ENABLE_TX_BATCH_LOG
#ifndef MASTER_PROFILE_VARIANT
#define MASTER_PROFILE_VARIANT  0u
#endif
#ifndef ARINC429_LOGIC_MODE
#define ARINC429_LOGIC_MODE     0u
#endif

typedef struct {
    uint8_t label;
    uint8_t sdi;
    const char *name;
    bool is_signal;
} arinc_profile_t;

typedef struct {
    float temp_c;
    float speed_kt;
    float altitude_ft;
    uint32_t rng_state;
} signal_state_t;

typedef struct {
    uint32_t ack_ok;
    uint32_t ack_bad_label;
    uint32_t ack_bad_parity;
    uint32_t ack_timeout;
    uint32_t rx_invalid_symbol;
} master_link_stats_t;

#define PROFILE_SIGNAL(label, sdi, name) {label, sdi, name, true}
#define PROFILE_NOISE(label, sdi, name)  {label, sdi, name, false}

static uint8_t calc_odd_parity_31bits(uint32_t word_without_parity) {
#if ARINC429_LOGIC_MODE
    return arinc429_calc_odd_parity_31bits(word_without_parity);
#else
    int ones = 0;

    for (int i = 0; i < 31; ++i) {
        if ((word_without_parity >> i) & 1u) {
            ++ones;
        }
    }

    return (ones % 2 == 0) ? 1u : 0u;
#endif
}

static uint32_t build_arinc_word(uint8_t label, uint8_t sdi, uint32_t data, uint8_t ssm) {
#if ARINC429_LOGIC_MODE
    return arinc429_build_word_fields(label, sdi, data, ssm);
#else
    uint32_t word = 0;

    word |= ((uint32_t)(label & 0xFFu)) << 0;
    word |= ((uint32_t)(sdi & 0x03u)) << 8;
    word |= ((uint32_t)(data & 0x7FFFFu)) << 10;
    word |= ((uint32_t)(ssm & 0x03u)) << 29;
    word |= ((uint32_t)calc_odd_parity_31bits(word)) << 31;

    return word;
#endif
}

static float clamp_float(float value, float min_value, float max_value) {
    if (value < min_value) {
        return min_value;
    }
    if (value > max_value) {
        return max_value;
    }
    return value;
}

static uint32_t prng_next(uint32_t *state) {
    *state = (*state * 1664525u) + 1013904223u;
    return *state;
}

static float random_range(uint32_t *state, float min_value, float max_value) {
    const uint32_t sample = prng_next(state) & 0xFFFFu;
    const float normalized = (float)sample / 65535.0f;
    return min_value + ((max_value - min_value) * normalized);
}

static uint8_t random_ssm(uint32_t *state) {
    const uint32_t sample = prng_next(state) % 100u;

    if (sample < 86u) {
        return 3u; /* NORMAL */
    }
    if (sample < 91u) {
        return 1u; /* NCD */
    }
    if (sample < 96u) {
        return 2u; /* FUNCTIONAL_TEST */
    }
    return 0u;     /* FAILURE */
}

static uint32_t encode_temperature(float temp_c) {
    float raw = (temp_c + 50.0f) / 0.25f;
    raw = clamp_float(raw, 0.0f, (float)0x7FFFFu);
    return (uint32_t)(raw + 0.5f);
}

static uint32_t encode_speed(float speed_kt) {
    speed_kt = clamp_float(speed_kt, 0.0f, (float)0x7FFFFu);
    return (uint32_t)(speed_kt + 0.5f);
}

static uint32_t encode_altitude(float altitude_ft) {
    float raw = altitude_ft / 10.0f;
    raw = clamp_float(raw, 0.0f, (float)0x7FFFFu);
    return (uint32_t)(raw + 0.5f);
}

static uint32_t build_noise_payload(uint32_t *state, uint32_t slot) {
    const uint32_t sample = prng_next(state) & 0x7FFFFu;

    switch (slot % 4u) {
        case 0u:
            return sample;
        case 1u:
            return (sample ^ 0x15555u) & 0x7FFFFu;
        case 2u:
            return ((sample >> 2u) | ((sample & 0x3u) << 17u)) & 0x7FFFFu;
        default:
            return (sample + (slot * 137u)) & 0x7FFFFu;
    }
}

static const char *profile_variant_text(void) {
    return (MASTER_PROFILE_VARIANT == 0u) ? "baseline" : "stress";
}

static const char *link_mode_text(void) {
    return ARINC429_LOGIC_MODE ? "arinc429_logic" : "legacy";
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

static void evolve_signal(signal_state_t *signals, uint32_t idx) {
    if (idx == 0u) {
        signals->temp_c += random_range(&signals->rng_state, -0.8f, 0.9f);
        signals->temp_c = clamp_float(signals->temp_c, -20.0f, 60.0f);
    } else if (idx == 1u) {
        signals->speed_kt += random_range(&signals->rng_state, -6.0f, 8.0f);
        signals->speed_kt = clamp_float(signals->speed_kt, 0.0f, 320.0f);
    } else {
        signals->altitude_ft += random_range(&signals->rng_state, -180.0f, 260.0f);
        signals->altitude_ft = clamp_float(signals->altitude_ft, 0.0f, 12000.0f);
    }
}

static uint32_t encode_profile_value(const signal_state_t *signals, uint32_t idx) {
    if (idx == 0u) {
        return encode_temperature(signals->temp_c);
    }
    if (idx == 1u) {
        return encode_speed(signals->speed_kt);
    }
    return encode_altitude(signals->altitude_ft);
}

static uint32_t word_time_us(void) {
    return ((32u + WORD_GAP_BITS) * 1000000u) / BIT_RATE_HZ;
}

static void arinc_tx_program_init(PIO pio, uint sm, uint offset, uint pin_base, float bit_rate_hz) {
    pio_gpio_init(pio, pin_base + 0);
    pio_gpio_init(pio, pin_base + 1);

#if ARINC429_LOGIC_MODE
    pio_sm_config c = arinc429_logic_tx_program_get_default_config(offset);
#else
    pio_sm_config c = arinc_gpio_link_tx_program_get_default_config(offset);
#endif

    sm_config_set_set_pins(&c, pin_base, 2);
    sm_config_set_out_shift(&c, true, false, 32);
    sm_config_set_fifo_join(&c, PIO_FIFO_JOIN_TX);

    const float cycles_per_bit = 2.0f * (float)HALF_CYCLES;
    const float clkdiv = (float)clock_get_hz(clk_sys) / (bit_rate_hz * cycles_per_bit);

    sm_config_set_clkdiv(&c, clkdiv);

    pio_sm_init(pio, sm, offset, &c);
    pio_sm_set_enabled(pio, sm, false);
}

static void arinc_rx_program_init(PIO pio, uint sm, uint offset, uint pin_base) {
    pio_gpio_init(pio, pin_base + 0);
    pio_gpio_init(pio, pin_base + 1);
    gpio_pull_down(pin_base + 0);
    gpio_pull_down(pin_base + 1);

#if ARINC429_LOGIC_MODE
    pio_sm_config c = arinc429_logic_rx_program_get_default_config(offset);
#else
    pio_sm_config c = arinc_gpio_link_rx_program_get_default_config(offset);
#endif

    sm_config_set_in_pins(&c, pin_base);
    sm_config_set_jmp_pin(&c, pin_base + 1);
    sm_config_set_in_shift(&c, true, false, 32);
    sm_config_set_clkdiv(&c, 1.0f);

    pio_sm_init(pio, sm, offset, &c);
    pio_sm_set_enabled(pio, sm, false);
}

static void start_tx_channel(PIO pio, uint sm_tx, uint pin_base) {
    pio_sm_set_enabled(pio, sm_tx, false);
    pio_sm_clear_fifos(pio, sm_tx);
    pio_sm_restart(pio, sm_tx);
    pio_sm_set_consecutive_pindirs(pio, sm_tx, pin_base, 2, true);
    pio_sm_set_pins_with_mask(pio, sm_tx, 0u, (1u << pin_base) | (1u << (pin_base + 1)));
    pio_sm_set_enabled(pio, sm_tx, true);
}

static void start_rx_channel(PIO pio, uint sm_rx, uint pin_base) {
    pio_sm_set_enabled(pio, sm_rx, false);
    pio_sm_set_consecutive_pindirs(pio, sm_rx, pin_base, 2, false);
    pio_sm_clear_fifos(pio, sm_rx);
    pio_sm_restart(pio, sm_rx);
    pio_sm_set_enabled(pio, sm_rx, true);
}

static void reset_rx_channel(PIO pio, uint sm_rx) {
    pio_sm_set_enabled(pio, sm_rx, false);
    pio_sm_clear_fifos(pio, sm_rx);
    pio_sm_restart(pio, sm_rx);
    pio_sm_set_enabled(pio, sm_rx, true);
}

static void wait_tx_drain(PIO pio, uint sm_tx) {
    while (!pio_sm_is_tx_fifo_empty(pio, sm_tx)) {
        tight_loop_contents();
    }

    sleep_us(word_time_us() + 50u);
}

static bool is_valid_ack(uint32_t word, uint32_t *ack_batch, master_link_stats_t *stats) {
#if ARINC429_LOGIC_MODE
    const uint8_t label = arinc429_word_label(word);
    const uint8_t sdi = arinc429_word_sdi(word);
    const uint32_t raw = arinc429_word_data(word);
    const uint8_t ssm = arinc429_word_ssm(word);
    const bool parity_ok = arinc429_parity_check(word);
#else
    const uint8_t label = (word >> 0) & 0xFFu;
    const uint8_t sdi = (word >> 8) & 0x03u;
    const uint32_t raw = (word >> 10) & 0x7FFFFu;
    const uint8_t ssm = (word >> 29) & 0x03u;
    const uint8_t parity_rx = (word >> 31) & 0x01u;
    const uint8_t parity_exp = calc_odd_parity_31bits(word & 0x7FFFFFFFu);
    const bool parity_ok = (parity_rx == parity_exp);
#endif

    if (!parity_ok) {
        ++stats->ack_bad_parity;
        ++stats->rx_invalid_symbol;
        return false;
    }

    if (label == ACK_LABEL && sdi == ACK_SDI && ssm == 3u) {
        *ack_batch = raw;
        ++stats->ack_ok;
        return true;
    }

    ++stats->ack_bad_label;
    return false;
}

static uint32_t wait_for_ack(PIO pio, uint sm_ack_rx, uint32_t batch_number, master_link_stats_t *stats) {
    absolute_time_t last_log_time = get_absolute_time();

    while (true) {
        while (!pio_sm_is_rx_fifo_empty(pio, sm_ack_rx)) {
            const uint32_t word = pio_sm_get(pio, sm_ack_rx);
            uint32_t ack_batch = 0;

            if (is_valid_ack(word, &ack_batch, stats)) {
                printf("[ACK] MASTER <- ACK recibido | batch_rx=%lu | batch_tx=%lu\r\n",
                       (unsigned long)ack_batch,
                       (unsigned long)batch_number);

                if (ack_batch != batch_number) {
                    printf("[WARN] MASTER <- ACK valido pero numero distinto al esperado\r\n");
                }

                return ack_batch;
            }

            if (word == 0u) {
                continue;
            }

            printf("[WARN] MASTER <- palabra no ACK ignorada en canal reverso: 0x%08lX\r\n",
                   (unsigned long)word);
        }

        const int64_t idle_us = absolute_time_diff_us(last_log_time, get_absolute_time());
        if (idle_us >= (int64_t)(ACK_WAIT_LOG_MS * 1000u)) {
            ++stats->ack_timeout;
            printf("[INFO] MASTER -> esperando ACK del slave para batch %lu por GP4/GP5\r\n",
                   (unsigned long)batch_number);
            last_log_time = get_absolute_time();
        }

        tight_loop_contents();
    }
}

int main(void) {
    stdio_init_all();
    sleep_ms(STARTUP_DELAY_MS);

    printf("MASTER - ARINC-like con dos canales simplex\r\n");
    printf("TX directo: GP2=FWD_A, GP3=FWD_B\r\n");
    printf("RX reverso: GP4=REV_A, GP5=REV_B\r\n");
    printf("Bit rate: %u bps\r\n", BIT_RATE_HZ);
    printf("Batch: %u palabras | ACK label: 0x%02X\r\n\r\n", WORDS_PER_BATCH, ACK_LABEL);
    printf("Guardia post-ACK: %u us | perfil: %s\r\n",
           POST_ACK_GUARD_US,
           profile_variant_text());
    printf("Modo de enlace: %s\r\n",
           link_mode_text());

    PIO pio = pio0;
    const uint sm_fwd_tx = 0;
    const uint sm_rev_rx = 1;
#if ARINC429_LOGIC_MODE
    const uint tx_offset = pio_add_program(pio, &arinc429_logic_tx_program);
    const uint rx_offset = pio_add_program(pio, &arinc429_logic_rx_program);
#else
    const uint tx_offset = pio_add_program(pio, &arinc_gpio_link_tx_program);
    const uint rx_offset = pio_add_program(pio, &arinc_gpio_link_rx_program);
#endif

    arinc_tx_program_init(pio, sm_fwd_tx, tx_offset, ARINC_FWD_PIN_BASE, (float)BIT_RATE_HZ);
    arinc_rx_program_init(pio, sm_rev_rx, rx_offset, ARINC_REV_PIN_BASE);
    start_tx_channel(pio, sm_fwd_tx, ARINC_FWD_PIN_BASE);
    start_rx_channel(pio, sm_rev_rx, ARINC_REV_PIN_BASE);

    const arinc_profile_t profiles[] = {
        PROFILE_SIGNAL(0xA5, 0x0, "TEMPERATURA"),
        PROFILE_SIGNAL(0xB1, 0x1, "VELOCIDAD"),
        PROFILE_SIGNAL(0xC2, 0x2, "ALTITUD"),
        PROFILE_NOISE(0x11, 0x0, "NOISE_NAV"),
        PROFILE_NOISE(0x24, 0x1, "NOISE_FMS"),
        PROFILE_NOISE(0x39, 0x2, "NOISE_MAINT"),
        PROFILE_NOISE(0x4E, 0x3, "NOISE_MISC"),
        PROFILE_NOISE(0x57, 0x0, "NOISE_TEST"),
        PROFILE_NOISE(0x6A, 0x2, "NOISE_DIAG"),
        PROFILE_NOISE(0x7D, 0x1, "NOISE_SPARE"),
#if MASTER_PROFILE_VARIANT != 0u
        PROFILE_NOISE(0x11, 0x1, "NOISE_NAV_SDI1"),
        PROFILE_NOISE(0x24, 0x2, "NOISE_FMS_SDI2"),
        PROFILE_NOISE(0x39, 0x3, "NOISE_MAINT_SDI3"),
        PROFILE_NOISE(0x4E, 0x0, "NOISE_MISC_SDI0"),
        PROFILE_NOISE(0x57, 0x1, "NOISE_TEST_SDI1"),
        PROFILE_NOISE(0x6A, 0x3, "NOISE_DIAG_SDI3"),
        PROFILE_NOISE(0x7D, 0x2, "NOISE_SPARE_SDI2"),
        PROFILE_SIGNAL(0xA5, 0x1, "TEMPERATURA_SDI1"),
        PROFILE_SIGNAL(0xB1, 0x2, "VELOCIDAD_SDI2"),
        PROFILE_SIGNAL(0xC2, 0x3, "ALTITUD_SDI3"),
        PROFILE_NOISE(0x11, 0x2, "NOISE_NAV_SDI2"),
        PROFILE_NOISE(0x24, 0x3, "NOISE_FMS_SDI3"),
        PROFILE_NOISE(0x39, 0x0, "NOISE_MAINT_SDI0"),
        PROFILE_NOISE(0x57, 0x2, "NOISE_TEST_SDI2"),
        PROFILE_NOISE(0x6A, 0x0, "NOISE_DIAG_SDI0"),
        PROFILE_NOISE(0x7D, 0x3, "NOISE_SPARE_SDI3"),
#endif
    };

    printf("Patron TX: %u utiles + %u basura cada %u palabras | labels activas: %u\r\n\r\n",
           VALID_PATTERN_WORDS,
           NOISE_PATTERN_WORDS,
           TX_PATTERN_WORDS,
           (unsigned)count_of(profiles));

    signal_state_t signals = {
        .temp_c = 20.0f,
        .speed_kt = 120.0f,
        .altitude_ft = 1000.0f,
        .rng_state = 0xC0FFEEu,
    };

    uint32_t batch_number = 0;
    uint32_t global_word_index = 0;
    master_link_stats_t link_stats = {0};

    while (true) {
        ++batch_number;
        uint32_t valid_words_in_batch = 0u;
        uint32_t noise_words_in_batch = 0u;
        reset_rx_channel(pio, sm_rev_rx);

#if ENABLE_TX_BATCH_LOG
        printf("[INFO] MASTER -> iniciando batch %lu de %u palabras | T=%.1f C | V=%.1f kt | ALT=%.1f ft\r\n",
               (unsigned long)batch_number,
               WORDS_PER_BATCH,
               signals.temp_c,
               signals.speed_kt,
               signals.altitude_ft);
#endif

        for (uint32_t i = 0; i < WORDS_PER_BATCH; ++i) {
            const uint32_t idx = global_word_index % count_of(profiles);
            const arinc_profile_t profile = profiles[idx];
            const uint8_t ssm = random_ssm(&signals.rng_state);
            uint32_t raw = 0u;

            if (profile.is_signal) {
                raw = encode_profile_value(&signals, idx);
                ++valid_words_in_batch;
            } else {
                raw = build_noise_payload(&signals.rng_state, idx);
                ++noise_words_in_batch;
            }

            const uint32_t word = build_arinc_word(profile.label, profile.sdi, raw, ssm);

            pio_sm_put_blocking(pio, sm_fwd_tx, word);

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

            if (profile.is_signal) {
                evolve_signal(&signals, idx);
            }
            ++global_word_index;
        }

        wait_tx_drain(pio, sm_fwd_tx);

#if ENABLE_TX_BATCH_LOG
        printf("[INFO] MASTER -> batch %lu enviado por GP2/GP3 | utiles=%lu | basura=%lu | esperando ACK en GP4/GP5\r\n",
               (unsigned long)batch_number,
               (unsigned long)valid_words_in_batch,
               (unsigned long)noise_words_in_batch);
#endif

        (void)wait_for_ack(pio, sm_rev_rx, batch_number, &link_stats);
        sleep_us(POST_ACK_GUARD_US);

        if ((batch_number % 64u) == 0u) {
            printf("[STATS] MASTER | ack_ok=%lu | ack_bad_label=%lu | ack_bad_parity=%lu | ack_timeout=%lu | rx_invalid=%lu\r\n",
                   (unsigned long)link_stats.ack_ok,
                   (unsigned long)link_stats.ack_bad_label,
                   (unsigned long)link_stats.ack_bad_parity,
                   (unsigned long)link_stats.ack_timeout,
                   (unsigned long)link_stats.rx_invalid_symbol);
        }
    }
}
