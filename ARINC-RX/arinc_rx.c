#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

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
 * Slave con dos canales simplex ARINC-like:
 * - canal directo GP2/GP3: master -> slave
 * - canal inverso GP4/GP5: slave -> master (ACK)
 *
 * Se elimina el half-duplex sobre un solo par y se separan ambos sentidos,
 * acercando la arquitectura al uso real de ARINC 429.
 */
#define ARINC_FWD_PIN_BASE      2u
#define ARINC_REV_PIN_BASE      4u
#define BIT_RATE_HZ             100000u
#define HALF_CYCLES             5u
#define WORD_GAP_BITS           4u
#ifndef RX_STREAM_MODE
#define RX_STREAM_MODE          0u
#endif
#ifndef RX_STREAM_SEND_ACK
#define RX_STREAM_SEND_ACK      0u
#endif
#ifndef RX_STREAM_STATS_EVERY
#define RX_STREAM_STATS_EVERY   10000u
#endif
#ifndef RX_EXPECTED_BATCH_WORDS
#define RX_EXPECTED_BATCH_WORDS 1000u
#endif
#define EXPECTED_BATCH_WORDS    ((uint32_t)RX_EXPECTED_BATCH_WORDS)
#define STARTUP_DELAY_MS        1200u
#define ENABLE_MATCH_LOG        0u
#ifndef RX_ENABLE_BATCH_LOG
#define RX_ENABLE_BATCH_LOG     1u
#endif
#define ENABLE_BATCH_LOG        RX_ENABLE_BATCH_LOG
#define ENABLE_WARN_LOG         1u
#define ENABLE_FRAME_FILTER     1u
#define CAPTURE_BUFFER_WORDS    1200u
#define IDLE_FLUSH_US           8000u
#define COMPACT_LOG_FORMAT      1u
#define ACK_LABEL               0xACu
#define ACK_SDI                 0x03u
#define ACK_TX_GUARD_US         100u
#ifndef ARINC429_LOGIC_MODE
#define ARINC429_LOGIC_MODE     0u
#endif

typedef struct {
    uint8_t label;
    uint8_t sdi;
} arinc_filter_entry_t;

typedef struct {
    uint32_t received_words;
    uint32_t accepted_words;
    uint32_t filtered_words;
    uint32_t parity_errors;
    uint32_t dumped_batches;
    uint32_t overflow_events;
    uint32_t ack_sent;
    uint32_t invalid_symbol_events;
    uint32_t missing_null_events;
    uint32_t incomplete_batch_count;
} rx_stats_t;

static uint32_t capture_buffer[CAPTURE_BUFFER_WORDS];
static uint32_t g_last_stats_report_batch = 0u;

static const arinc_filter_entry_t k_rx_filter_whitelist[] = {
    {0xA5, 0x0},
    {0xB1, 0x1},
    {0xC2, 0x2},
};

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

static const char *link_mode_text(void) {
    return ARINC429_LOGIC_MODE ? "arinc429_logic" : "legacy";
}

static const char *rx_run_mode_text(void) {
    if (RX_STREAM_MODE) {
        return RX_STREAM_SEND_ACK ? "stream_with_optional_ack" : "stream_no_ack";
    }
    return "lab_batch_ack";
}

static const char *label_to_name(uint8_t label) {
    switch (label) {
        case 0xA5: return "TEMPERATURA";
        case 0xB1: return "VELOCIDAD";
        case 0xC2: return "ALTITUD";
        case ACK_LABEL: return "ACK_BATCH";
        default: return "DESCONOCIDO";
    }
}

static const char *label_to_unit(uint8_t label) {
    switch (label) {
        case 0xA5: return "C";
        case 0xB1: return "kt";
        case 0xC2: return "ft";
        case ACK_LABEL: return "batch";
        default: return "-";
    }
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

static float decode_temperature(uint32_t raw) {
    return ((float)raw * 0.25f) - 50.0f;
}

static float decode_speed(uint32_t raw) {
    return (float)raw;
}

static float decode_altitude(uint32_t raw) {
    return (float)raw * 10.0f;
}

static float decode_value(uint8_t label, uint32_t raw) {
    switch (label) {
        case 0xA5: return decode_temperature(raw);
        case 0xB1: return decode_speed(raw);
        case 0xC2: return decode_altitude(raw);
        case ACK_LABEL: return (float)raw;
        default: return 0.0f;
    }
}

static bool frame_passes_filter(uint8_t label, uint8_t sdi) {
#if !ENABLE_FRAME_FILTER
    (void)label;
    (void)sdi;
    return true;
#else
    for (size_t i = 0; i < count_of(k_rx_filter_whitelist); ++i) {
        if (k_rx_filter_whitelist[i].label == label && k_rx_filter_whitelist[i].sdi == sdi) {
            return true;
        }
    }

    return false;
#endif
}

static uint32_t word_time_us(void) {
    return ((32u + WORD_GAP_BITS) * 1000000u) / BIT_RATE_HZ;
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

static void start_rx_channel(PIO pio, uint sm_rx, uint pin_base) {
    pio_sm_set_enabled(pio, sm_rx, false);
    pio_sm_set_consecutive_pindirs(pio, sm_rx, pin_base, 2, false);
    pio_sm_clear_fifos(pio, sm_rx);
    pio_sm_restart(pio, sm_rx);
    pio_sm_set_enabled(pio, sm_rx, true);
}

static void start_tx_channel(PIO pio, uint sm_tx, uint pin_base) {
    pio_sm_set_enabled(pio, sm_tx, false);
    pio_sm_clear_fifos(pio, sm_tx);
    pio_sm_restart(pio, sm_tx);
    pio_sm_set_consecutive_pindirs(pio, sm_tx, pin_base, 2, true);
    pio_sm_set_pins_with_mask(pio, sm_tx, 0u, (1u << pin_base) | (1u << (pin_base + 1)));
    pio_sm_set_enabled(pio, sm_tx, true);
}

static void wait_tx_drain(PIO pio, uint sm_tx) {
    while (!pio_sm_is_tx_fifo_empty(pio, sm_tx)) {
        tight_loop_contents();
    }

    sleep_us(word_time_us() + 50u);
}

static void print_decoded_record(uint32_t word, rx_stats_t *stats) {
#if ARINC429_LOGIC_MODE
    const uint8_t label = arinc429_word_label(word);
    const uint8_t sdi = arinc429_word_sdi(word);
    const uint32_t raw = arinc429_word_data(word);
    const uint8_t ssm = arinc429_word_ssm(word);
#else
    const uint8_t label = (word >> 0) & 0xFFu;
    const uint8_t sdi = (word >> 8) & 0x03u;
    const uint32_t raw = (word >> 10) & 0x7FFFFu;
    const uint8_t ssm = (word >> 29) & 0x03u;
    const uint8_t parity_rx = (word >> 31) & 0x01u;
#endif

#if ARINC429_LOGIC_MODE
    const bool parity_ok = arinc429_parity_check(word);
#else
    const uint32_t word_31 = word & 0x7FFFFFFFu;
    const uint8_t parity_exp = calc_odd_parity_31bits(word_31);
    const bool parity_ok = (parity_rx == parity_exp);
#endif

    if (!parity_ok) {
        ++stats->parity_errors;
        ++stats->invalid_symbol_events;
        return;
    }

    if (!frame_passes_filter(label, sdi)) {
        ++stats->filtered_words;
        return;
    }

    ++stats->accepted_words;

    const char *name = label_to_name(label);
    const char *unit = label_to_unit(label);
    const char *parity_text = "OK";
    const float value = decode_value(label, raw);

#if ENABLE_MATCH_LOG
#if COMPACT_LOG_FORMAT
    printf("MATCH,%02X,%s,%lu,%.1f,%s,%u,%u,%s,%s\r\n",
           label,
           name,
           (unsigned long)raw,
           value,
           unit,
           sdi,
           ssm,
           ssm_to_text(ssm),
           parity_text);
#else
    printf("MATCH -> LABEL: 0x%02X (%s) | RAW: %lu | VAL: %.1f %s | SDI: %u | SSM: %u | SSM_TXT: %s | PARITY: %s\r\n",
           label,
           name,
           (unsigned long)raw,
           value,
           unit,
           sdi,
           ssm,
           ssm_to_text(ssm),
           parity_text);
#endif
#endif
}

static void process_received_word(uint32_t word, rx_stats_t *stats) {
    if (word == 0u) {
        return;
    }

    ++stats->received_words;
    print_decoded_record(word, stats);
}

static void send_ack(PIO pio,
                     uint sm_ack_tx,
                     uint32_t batch_number,
                     uint32_t word_count,
                     rx_stats_t *stats) {
    const uint32_t ack_word = build_arinc_word(ACK_LABEL, ACK_SDI, batch_number, 3u);

    sleep_us(ACK_TX_GUARD_US);

    printf("[ACK] SLAVE -> enviando ACK por GP4/GP5 | batch=%lu | words=%lu\r\n",
           (unsigned long)batch_number,
           (unsigned long)word_count);

    pio_sm_put_blocking(pio, sm_ack_tx, ack_word);
    wait_tx_drain(pio, sm_ack_tx);
    ++stats->ack_sent;

    printf("[ACK] SLAVE -> ACK enviado | total_ack=%lu\r\n",
           (unsigned long)stats->ack_sent);
}

static void dump_buffer(const uint32_t *buffer,
                        uint32_t word_count,
                        rx_stats_t *stats,
                        PIO pio,
                        uint sm_ack_tx) {
    if (word_count == 0u) {
        return;
    }

    ++stats->dumped_batches;
    uint32_t effective_word_count = 0u;
    const uint32_t parity_errors_before = stats->parity_errors;

#if ENABLE_BATCH_LOG
    printf("[INFO] Volcando lote %lu con %lu palabras capturadas\r\n",
           (unsigned long)stats->dumped_batches,
           (unsigned long)word_count);
#endif

    for (uint32_t i = 0; i < word_count; ++i) {
        if (buffer[i] == 0u) {
            continue;
        }
        ++effective_word_count;
        process_received_word(buffer[i], stats);
    }

#if ENABLE_BATCH_LOG
    printf("[INFO] Lote %lu procesado | RX=%lu | OK/FILTRO=%lu | DESCARTADAS=%lu | PARITY_ERR=%lu | OVERFLOW=%lu | ACK=%lu\r\n",
           (unsigned long)stats->dumped_batches,
           (unsigned long)stats->received_words,
           (unsigned long)stats->accepted_words,
           (unsigned long)stats->filtered_words,
           (unsigned long)stats->parity_errors,
           (unsigned long)stats->overflow_events,
           (unsigned long)stats->ack_sent);
#endif

    const uint32_t batch_parity_errors = stats->parity_errors - parity_errors_before;
    if (effective_word_count >= EXPECTED_BATCH_WORDS && batch_parity_errors == 0u) {
        send_ack(pio, sm_ack_tx, stats->dumped_batches, effective_word_count, stats);
    } else {
        ++stats->incomplete_batch_count;
        if (effective_word_count < EXPECTED_BATCH_WORDS) {
            ++stats->missing_null_events;
        }
#if ENABLE_WARN_LOG
        printf("[WARN] SLAVE -> lote no integro, no se envia ACK | words=%lu/%u | parity_errors=%lu\r\n",
               (unsigned long)effective_word_count,
               EXPECTED_BATCH_WORDS,
               (unsigned long)batch_parity_errors);
#endif
    }
}

int main(void) {
    stdio_init_all();
    sleep_ms(STARTUP_DELAY_MS);

    printf("SLAVE - ARINC-like con dos canales simplex\r\n");
    printf("RX directo: GP2=FWD_A, GP3=FWD_B\r\n");
    printf("Modo RX: %s\r\n", rx_run_mode_text());
#if RX_STREAM_MODE && !RX_STREAM_SEND_ACK
    printf("TX reverso: inactivo en modo stream_no_ack | GP4/GP5 quedan sin emision ARINC\r\n");
#else
    printf("TX reverso: GP4=REV_A, GP5=REV_B\r\n");
#endif
#if !RX_STREAM_MODE || RX_STREAM_SEND_ACK
    printf("Bit rate REV/ACK: %u bps\r\n", BIT_RATE_HZ);
#else
    printf("RX FWD: autodeteccion por flancos/nivel activo | REV TX deshabilitado\r\n");
#endif
    printf("Filtro por whitelist: %s\r\n", ENABLE_FRAME_FILTER ? "ACTIVO" : "INACTIVO");
    if (RX_STREAM_MODE) {
        printf("Stream: procesa palabras sin esperar lote fijo | stats cada %u palabras | ACK=%s\r\n",
               (uint32_t)RX_STREAM_STATS_EVERY,
               RX_STREAM_SEND_ACK ? "ON" : "OFF");
    } else {
        printf("Batch esperado: %u palabras | ACK label: 0x%02X\r\n",
               EXPECTED_BATCH_WORDS,
               ACK_LABEL);
    }
    printf("Modo de enlace: %s\r\n",
           link_mode_text());
    printf("Buffer de captura: %u palabras | flush por idle: %u us\r\n\r\n",
           CAPTURE_BUFFER_WORDS,
           IDLE_FLUSH_US);

    PIO pio = pio0;
    const uint sm_fwd_rx = 0;
#if !RX_STREAM_MODE || RX_STREAM_SEND_ACK
    const uint sm_rev_tx = 1;
#endif
#if ARINC429_LOGIC_MODE
    const uint rx_offset = pio_add_program(pio, &arinc429_logic_rx_program);
#if !RX_STREAM_MODE || RX_STREAM_SEND_ACK
    const uint tx_offset = pio_add_program(pio, &arinc429_logic_tx_program);
#endif
#else
    const uint rx_offset = pio_add_program(pio, &arinc_gpio_link_rx_program);
#if !RX_STREAM_MODE || RX_STREAM_SEND_ACK
    const uint tx_offset = pio_add_program(pio, &arinc_gpio_link_tx_program);
#endif
#endif

    arinc_rx_program_init(pio, sm_fwd_rx, rx_offset, ARINC_FWD_PIN_BASE);
#if RX_STREAM_MODE && !RX_STREAM_SEND_ACK
    gpio_init(ARINC_REV_PIN_BASE + 0u);
    gpio_init(ARINC_REV_PIN_BASE + 1u);
    gpio_set_dir(ARINC_REV_PIN_BASE + 0u, GPIO_IN);
    gpio_set_dir(ARINC_REV_PIN_BASE + 1u, GPIO_IN);
    gpio_pull_down(ARINC_REV_PIN_BASE + 0u);
    gpio_pull_down(ARINC_REV_PIN_BASE + 1u);
#else
    arinc_tx_program_init(pio, sm_rev_tx, tx_offset, ARINC_REV_PIN_BASE, (float)BIT_RATE_HZ);
#endif
    start_rx_channel(pio, sm_fwd_rx, ARINC_FWD_PIN_BASE);
#if !RX_STREAM_MODE || RX_STREAM_SEND_ACK
    start_tx_channel(pio, sm_rev_tx, ARINC_REV_PIN_BASE);
#endif

    rx_stats_t stats = {0};

#if RX_STREAM_MODE
    while (true) {
        bool drained_any = false;

        while (!pio_sm_is_rx_fifo_empty(pio, sm_fwd_rx)) {
            drained_any = true;
            const uint32_t word = pio_sm_get(pio, sm_fwd_rx);
            const uint32_t received_before = stats.received_words;
            process_received_word(word, &stats);

            if (RX_STREAM_STATS_EVERY > 0u &&
                stats.received_words != received_before &&
                stats.received_words > 0u &&
                ((stats.received_words % RX_STREAM_STATS_EVERY) == 0u)) {
                printf("[STREAM] SLAVE | RX=%lu | OK/FILTRO=%lu | DESCARTADAS=%lu | PARITY_ERR=%lu | OVERFLOW=%lu | ACK=%lu\r\n",
                       (unsigned long)stats.received_words,
                       (unsigned long)stats.accepted_words,
                       (unsigned long)stats.filtered_words,
                       (unsigned long)stats.parity_errors,
                       (unsigned long)stats.overflow_events,
                       (unsigned long)stats.ack_sent);
            }
        }

        if (!drained_any) {
            tight_loop_contents();
        }
    }
#else
    uint32_t buffered_words = 0;
    bool capture_active = false;
    absolute_time_t last_rx_time = get_absolute_time();

    while (true) {
        bool drained_any = false;

        while (!pio_sm_is_rx_fifo_empty(pio, sm_fwd_rx)) {
            drained_any = true;
            const uint32_t word = pio_sm_get(pio, sm_fwd_rx);

            if (word == 0u) {
                continue;
            }

            capture_active = true;
            last_rx_time = get_absolute_time();

            if (buffered_words < CAPTURE_BUFFER_WORDS) {
                capture_buffer[buffered_words++] = word;
            } else {
                ++stats.overflow_events;
            }
        }

        if (capture_active && buffered_words > 0u) {
            const int64_t idle_us = absolute_time_diff_us(last_rx_time, get_absolute_time());
            if (idle_us >= (int64_t)IDLE_FLUSH_US) {
                dump_buffer(capture_buffer, buffered_words, &stats, pio, sm_rev_tx);
                buffered_words = 0u;
                capture_active = false;
            }
        }

        if ((stats.dumped_batches > 0u) &&
            (stats.dumped_batches != g_last_stats_report_batch) &&
            ((stats.dumped_batches % 64u) == 0u) &&
            !capture_active) {
            g_last_stats_report_batch = stats.dumped_batches;
            printf("[STATS] SLAVE | invalid_symbol=%lu | missing_null=%lu | ack_tx=%lu | incomplete=%lu\r\n",
                   (unsigned long)stats.invalid_symbol_events,
                   (unsigned long)stats.missing_null_events,
                   (unsigned long)stats.ack_sent,
                   (unsigned long)stats.incomplete_batch_count);
        }

        if (!drained_any) {
            tight_loop_contents();
        }
    }
#endif
}
