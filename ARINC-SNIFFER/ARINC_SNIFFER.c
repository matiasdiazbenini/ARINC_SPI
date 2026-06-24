#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include <string.h>

#include "pico/stdlib.h"
#include "hardware/irq.h"
#include "hardware/pio.h"
#include "hardware/regs/spi.h"
#include "hardware/sync.h"
#include "hardware/spi.h"

#include "arinc429_logic.h"

#if ARINC429_LOGIC_MODE
#include "arinc429_logic.pio.h"
#else
#include "arinc_gpio_link.pio.h"
#endif
#ifndef SNIFFER_SPI_PIO_FRAME_TRANSPORT
#define SNIFFER_SPI_PIO_FRAME_TRANSPORT 0u
#endif
#if SNIFFER_SPI_PIO_FRAME_TRANSPORT
#include "spi_frame_slave.pio.h"
#endif
#include "sniffer_spi_protocol.h"

/*
 * Sniffer pasivo sobre dos canales simplex:
 * - canal directo GP2/GP3: master -> slave
 * - canal inverso GP4/GP5: slave -> master (ACK)
 *
 * Se mantiene la salida MATCH,... por USB para debug, pero ahora ademas
 * exporta por SPI a la Raspberry Pi 3B+ eventos y estadisticas ya filtrados.
 */
#define ARINC_FWD_PIN_BASE        2u
#define ARINC_REV_PIN_BASE        4u
#define BIT_RATE_HZ               100000u
#define STARTUP_DELAY_MS          1200u
#define ENABLE_MATCH_LOG          1u
#define ENABLE_WARN_LOG           1u
#define ENABLE_LOGIC_REJECT_LOG   0u
#define MATCH_LOG_STRIDE          64u
#define FWD_RESYNC_LOG_EVERY      32u
#define SPI_DIAG_OK_LOG_STRIDE    64u
#define ENABLE_FRAME_FILTER       1u
#define ENABLE_SPI_DIAG_LOG       1u
#define CAPTURE_BUFFER_WORDS      8192u
#define PRINT_BUDGET_PER_LOOP     8u
#define COMPACT_LOG_FORMAT        1u
#define ACK_LABEL                 0xACu
#define ACK_SDI                   0x03u
#define FILTER_SDI_ANY            0xFFu
#define RESYNC_WORD_THRESHOLD     32u
#define REV_RESYNC_LOG_EVERY      32u
#define FWD_STALL_RESYNC_US       500000u
#define CHANNEL_FWD               0u
#define CHANNEL_REV               1u
#define SPI_EVENT_QUEUE_DEPTH     512u
#define LATEST_SLOT_CAPACITY      16u
#define ENABLE_SPI_EVENT_DEBUG_QUEUE 0u
#if SNIFFER_SPI_PIO_FRAME_TRANSPORT
#define ENABLE_SPI_IRQ_TRANSPORT 0u
#else
#define ENABLE_SPI_IRQ_TRANSPORT 1u
#endif
#define ENABLE_SPI_TX_DMA 0u
#ifndef SNIFFER_SPI_DRDY_PURE
#define SNIFFER_SPI_DRDY_PURE SNIFFER_SPI_PIO_FRAME_TRANSPORT
#endif
#define SNIFFER_SPI_PORT          spi0
#define SNIFFER_SPI_IRQ           SPI0_IRQ
#define SNIFFER_SPI_PIO           pio1
#define SNIFFER_SPI_PIO_SM_RX     0u
#define SNIFFER_SPI_PIO_SM_TX     1u
#define SNIFFER_SPI_RX_PIN        16u
#define SNIFFER_SPI_CSN_PIN       17u
#define SNIFFER_SPI_SCK_PIN       18u
#define SNIFFER_SPI_TX_PIN        19u
#define SNIFFER_SPI_DRDY_PIN      20u
#ifndef ARINC429_LOGIC_MODE
#define ARINC429_LOGIC_MODE       0u
#endif
#if SNIFFER_SPI_PIO_FRAME_TRANSPORT && ARINC429_LOGIC_MODE
#define SNIFFER_BUILD_TAG         "SPI-PIOFRAME-ARINC429-STRICTPARITY-DRDY-V3"
#elif SNIFFER_SPI_PIO_FRAME_TRANSPORT
#define SNIFFER_BUILD_TAG         "SPI-PIOFRAME-RL1"
#elif ARINC429_LOGIC_MODE
#define SNIFFER_BUILD_TAG         "SPI-IRQFRAME-ARINC429"
#else
#define SNIFFER_BUILD_TAG         "SPI-IRQFRAME-RL1"
#endif
#define SPI_DIAG_HEARTBEAT_HALF_PERIOD_US 500000u
#define SPI_DIAG_PULSE_US         120000u
#define SPI_TRANSPORT_STALE_GAP_US 10000u

typedef struct {
    uint8_t label;
    uint8_t sdi;
} arinc_filter_entry_t;

typedef struct {
    uint32_t word;
    uint8_t channel;
} capture_entry_t;

typedef struct {
    uint32_t received_words;
    uint32_t accepted_words;
    uint32_t filtered_words;
    uint32_t parity_errors;
    uint32_t overflow_events;
    uint32_t fwd_resync_events;
    uint32_t rev_resync_events;
    uint32_t fwd_startup_resync_events;
    uint32_t fwd_operational_resync_events;
    uint32_t spi_drop_events;
    uint32_t fwd_invalid_symbol_events;
    uint32_t rev_invalid_symbol_events;
    uint32_t fwd_missing_null_events;
    uint32_t rev_missing_null_events;
    uint32_t fwd_raw_words;
    uint32_t rev_raw_words;
    uint32_t fwd_rejected_words;
    uint32_t rev_rejected_words;
    bool fwd_link_armed;
} rx_stats_t;

typedef struct {
    bool pass_all;
    uint8_t count;
    arinc_filter_entry_t entries[SNIFFER_SPI_FILTER_CAPACITY];
} filter_state_t;

typedef struct {
    uint32_t event_counter;
    uint32_t raw_value;
    int32_t scaled_tenths;
    uint8_t channel;
    uint8_t label;
    uint8_t sdi;
    uint8_t ssm;
    bool parity_ok;
} spi_event_t;

typedef struct {
    spi_event_t entries[SPI_EVENT_QUEUE_DEPTH];
    uint16_t read_index;
    uint16_t write_index;
    uint16_t queued;
    uint32_t next_event_counter;
} event_queue_t;

typedef struct {
    bool valid;
    bool parity_ok;
    uint8_t channel;
    uint8_t label;
    uint8_t sdi;
    uint8_t ssm;
    uint32_t raw_value;
    int32_t scaled_tenths;
    uint32_t update_counter;
    uint32_t hit_count;
} latest_slot_t;

typedef struct {
    latest_slot_t slots[LATEST_SLOT_CAPACITY];
    uint8_t valid_count;
    uint32_t snapshot_revision;
    uint32_t slot_evictions;
    uint32_t last_update_counter;
} latest_snapshot_state_t;

typedef enum {
    SPI_LINK_PHASE_IDLE = 0,
    SPI_LINK_PHASE_COLLECT_REQUEST,
    SPI_LINK_PHASE_REQUEST_READY,
    SPI_LINK_PHASE_STREAM_RESPONSE,
} spi_link_phase_t;

typedef struct {
    sniffer_spi_packet_t tx_packet;
    uint8_t rx_bytes[SNIFFER_SPI_PACKET_SIZE];
    uint8_t next_tx;
    uint16_t rx_count;
    uint16_t tx_index;
    uint16_t response_rx_count;
    uint16_t next_sequence;
    uint32_t transaction_counter;
    uint32_t reset_counter;
    uint32_t irq_count;
    uint32_t rx_irq_count;
    uint32_t tx_irq_count;
    uint32_t rt_irq_count;
    uint32_t ror_irq_count;
    int tx_dma_chan;
    uint32_t tx_dma_start_count;
    uint32_t tx_dma_done_count;
    uint32_t tx_dma_abort_count;
    PIO pio_spi;
    uint pio_sm_rx;
    uint pio_sm_tx;
    uint pio_rx_offset;
    uint pio_tx_offset;
    uint32_t pio_frame_count;
    uint32_t pio_request_frame_count;
    uint32_t pio_response_frame_count;
    uint32_t pio_discarded_frame_count;
    bool tx_dma_ready;
    volatile bool tx_dma_active;
    volatile bool request_ready;
    volatile bool request_overrun;
    uint64_t last_byte_us;
    spi_link_phase_t phase;
} spi_link_state_t;

typedef struct {
    bool drdy_active;
    bool heartbeat_level;
    uint64_t next_heartbeat_toggle_us;
    uint64_t spi_pulse_until_us;
} spi_diag_gpio_state_t;

static capture_entry_t capture_buffer[CAPTURE_BUFFER_WORDS];
static spi_diag_gpio_state_t g_spi_diag_gpio = {0};
static spi_link_state_t *g_spi_irq_link = NULL;

static void __not_in_flash_func(spi_irq_handler)(void);

static const arinc_filter_entry_t k_default_filter_entries[] = {
    {0xA5, 0x0},
    {0xB1, 0x1},
    {0xC2, 0x2},
    {ACK_LABEL, ACK_SDI},
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

static bool frame_is_ack(uint8_t label, uint8_t sdi) {
    return label == ACK_LABEL && sdi == ACK_SDI;
}

static bool frame_is_strict_ack(uint8_t channel,
                                uint8_t label,
                                uint8_t sdi,
                                uint8_t ssm,
                                bool parity_ok) {
    return channel == CHANNEL_REV &&
           label == ACK_LABEL &&
           sdi == ACK_SDI &&
           ssm == 3u &&
           parity_ok;
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
        default: return "raw";
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
        default: return (float)raw;
    }
}

static int32_t decode_value_tenths(uint8_t label, uint32_t raw) {
    const float value = decode_value(label, raw);
    const float scaled = value * 10.0f;
    return (int32_t)(scaled >= 0.0f ? (scaled + 0.5f) : (scaled - 0.5f));
}

static bool parity_is_ok(uint32_t word) {
#if ARINC429_LOGIC_MODE
    return arinc429_parity_check(word);
#else
    const uint32_t word_31 = word & 0x7FFFFFFFu;
    const uint8_t parity_rx = (word >> 31) & 0x01u;
    const uint8_t parity_exp = calc_odd_parity_31bits(word_31);
    return parity_rx == parity_exp;
#endif
}

static bool payload_is_plausible(uint8_t label, float value) {
    switch (label) {
        case 0xA5:
            return value >= -50.0f && value <= 100.0f;
        case 0xB1:
            return value >= 0.0f && value <= 400.0f;
        case 0xC2:
            return value >= 0.0f && value <= 15000.0f;
        case ACK_LABEL:
            return value >= 0.0f && value <= 100000.0f;
        default:
            return true;
    }
}

static void filter_reset_to_defaults(filter_state_t *filter) {
    filter->pass_all = false;
    filter->count = (uint8_t)count_of(k_default_filter_entries);
    memcpy(filter->entries,
           k_default_filter_entries,
           sizeof(k_default_filter_entries));
}

static bool filter_entry_matches(const arinc_filter_entry_t *entry, uint8_t label, uint8_t sdi) {
    return entry->label == label &&
           (entry->sdi == FILTER_SDI_ANY || entry->sdi == sdi);
}

static bool frame_passes_filter(const filter_state_t *filter,
                                uint8_t channel,
                                uint8_t label,
                                uint8_t sdi,
                                uint8_t ssm,
                                bool parity_ok) {
    if (label == ACK_LABEL) {
        return frame_is_strict_ack(channel, label, sdi, ssm, parity_ok);
    }

    if (frame_is_strict_ack(channel, label, sdi, ssm, parity_ok)) {
        return true;
    }

#if !ENABLE_FRAME_FILTER
    (void)filter;
    (void)channel;
    (void)label;
    (void)sdi;
    (void)ssm;
    (void)parity_ok;
    return true;
#else
    if (filter->pass_all) {
        return true;
    }

    for (uint8_t i = 0; i < filter->count; ++i) {
        if (filter_entry_matches(&filter->entries[i], label, sdi)) {
            return true;
        }
    }

    return false;
#endif
}

static bool should_buffer_word(uint32_t word, uint8_t channel, const filter_state_t *filter) {
    if (word == 0u) {
        return false;
    }

    const uint8_t label = ARINC429_LOGIC_MODE ? arinc429_word_label(word) : (uint8_t)((word >> 0) & 0xFFu);
    const uint8_t sdi = ARINC429_LOGIC_MODE ? arinc429_word_sdi(word) : (uint8_t)((word >> 8) & 0x03u);
    const uint32_t raw = ARINC429_LOGIC_MODE ? arinc429_word_data(word) : ((word >> 10) & 0x7FFFFu);
    const uint8_t ssm = ARINC429_LOGIC_MODE ? arinc429_word_ssm(word) : (uint8_t)((word >> 29) & 0x03u);
    const bool parity_ok = parity_is_ok(word);
    const float value = decode_value(label, raw);

    if (channel == CHANNEL_REV) {
        return frame_is_strict_ack(channel, label, sdi, ssm, parity_ok) &&
               payload_is_plausible(label, value);
    }

    if (!frame_passes_filter(filter, channel, label, sdi, ssm, parity_ok)) {
        return false;
    }

    return payload_is_plausible(label, value);
}

static void log_logic_rejected_word(uint32_t word,
                                    uint8_t channel,
                                    const filter_state_t *filter,
                                    rx_stats_t *stats) {
#if ARINC429_LOGIC_MODE
    const uint8_t label = arinc429_word_label(word);
    const uint8_t sdi = arinc429_word_sdi(word);
    const uint32_t raw = arinc429_word_data(word);
    const uint8_t ssm = arinc429_word_ssm(word);
    const bool parity_ok = parity_is_ok(word);
    const float value = decode_value(label, raw);
    const bool filter_ok = frame_passes_filter(filter, channel, label, sdi, ssm, parity_ok);
    const bool plausible = payload_is_plausible(label, value);
    uint32_t *reject_counter = (channel == CHANNEL_FWD) ? &stats->fwd_rejected_words : &stats->rev_rejected_words;

    ++(*reject_counter);

#if ENABLE_WARN_LOG && ENABLE_LOGIC_REJECT_LOG
    if (*reject_counter <= 4u || ((*reject_counter % 64u) == 0u)) {
        printf("[RX-DBG] canal=%s raw=0x%08lX label=0x%02X sdi=%u ssm=%u parity=%s filter=%s plausible=%s\r\n",
               channel == CHANNEL_FWD ? "FWD" : "REV",
               (unsigned long)word,
               label,
               sdi,
               ssm,
               parity_ok ? "OK" : "ERROR",
               filter_ok ? "OK" : "NO",
               plausible ? "OK" : "NO");
    }
#endif
#else
    (void)word;
    (void)channel;
    (void)filter;
    (void)stats;
#endif
}

static void arinc_rx_program_init(PIO pio, uint sm, uint offset, uint pin_base) {
    pio_gpio_init(pio, pin_base + 0);
    pio_gpio_init(pio, pin_base + 1);
    gpio_pull_down(pin_base + 0);
    gpio_pull_down(pin_base + 1);
    pio_sm_set_consecutive_pindirs(pio, sm, pin_base, 2, false);

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
    pio_sm_set_enabled(pio, sm, true);
}

static void restart_rx_state_machine(PIO pio, uint sm) {
    pio_sm_set_enabled(pio, sm, false);
    pio_sm_clear_fifos(pio, sm);
    pio_sm_restart(pio, sm);
    pio_sm_set_enabled(pio, sm, true);
}

static void note_channel_resync(uint8_t channel, const char *reason, rx_stats_t *stats) {
    uint32_t *counter = (channel == CHANNEL_FWD) ? &stats->fwd_resync_events : &stats->rev_resync_events;

    ++(*counter);
    if (channel == CHANNEL_FWD) {
        if (stats->fwd_link_armed) {
            ++stats->fwd_operational_resync_events;
        } else {
            ++stats->fwd_startup_resync_events;
        }
    }
    if (ARINC429_LOGIC_MODE && strcmp(reason, "invalid_streak") == 0) {
        if (channel == CHANNEL_FWD) {
            ++stats->fwd_invalid_symbol_events;
        } else {
            ++stats->rev_invalid_symbol_events;
        }
    }
    if (ARINC429_LOGIC_MODE && strcmp(reason, "timeout") == 0) {
        if (channel == CHANNEL_FWD) {
            ++stats->fwd_missing_null_events;
        } else {
            ++stats->rev_missing_null_events;
        }
    }

#if ENABLE_WARN_LOG
    if ((channel == CHANNEL_FWD && (*counter <= 4u || ((*counter % FWD_RESYNC_LOG_EVERY) == 0u))) ||
        *counter <= 4u ||
        ((*counter % REV_RESYNC_LOG_EVERY) == 0u)) {
        printf("[WARN] SNIFFER -> RX resincronizada en canal %s | motivo=%s | count=%lu\r\n",
               channel == CHANNEL_FWD ? "FWD" : "REV",
               reason,
               (unsigned long)(*counter));
    }
#else
    (void)reason;
#endif
}

static bool capture_queue_push(uint32_t word,
                               uint8_t channel,
                               uint32_t *write_index,
                               uint32_t *queued_words,
                               rx_stats_t *stats) {
    if (*queued_words >= CAPTURE_BUFFER_WORDS) {
        ++stats->overflow_events;
        return false;
    }

    capture_buffer[*write_index].word = word;
    capture_buffer[*write_index].channel = channel;
    *write_index = (*write_index + 1u) % CAPTURE_BUFFER_WORDS;
    ++(*queued_words);
    return true;
}

static bool capture_queue_pop(capture_entry_t *entry,
                              uint32_t *read_index,
                              uint32_t *queued_words) {
    if (*queued_words == 0u) {
        return false;
    }

    *entry = capture_buffer[*read_index];
    *read_index = (*read_index + 1u) % CAPTURE_BUFFER_WORDS;
    --(*queued_words);
    return true;
}

static void event_queue_clear(event_queue_t *queue) {
    queue->read_index = 0u;
    queue->write_index = 0u;
    queue->queued = 0u;
    queue->next_event_counter = 0u;
}

static void spi_diag_gpio_apply(void) {
#if SNIFFER_SPI_DRDY_PURE
    const bool output_high = g_spi_diag_gpio.drdy_active;
#else
    const uint64_t now_us = time_us_64();
    const bool pulse_active = now_us < g_spi_diag_gpio.spi_pulse_until_us;
    const bool output_high = g_spi_diag_gpio.drdy_active ||
                             g_spi_diag_gpio.heartbeat_level ||
                             pulse_active;
#endif

    gpio_put(SNIFFER_SPI_DRDY_PIN, output_high);
}

static void spi_diag_gpio_init(void) {
    memset(&g_spi_diag_gpio, 0, sizeof(g_spi_diag_gpio));
    g_spi_diag_gpio.next_heartbeat_toggle_us = time_us_64() + SPI_DIAG_HEARTBEAT_HALF_PERIOD_US;
    spi_diag_gpio_apply();
}

static void spi_diag_gpio_service(void) {
#if SNIFFER_SPI_DRDY_PURE
    spi_diag_gpio_apply();
#else
    const uint64_t now_us = time_us_64();

    while (now_us >= g_spi_diag_gpio.next_heartbeat_toggle_us) {
        g_spi_diag_gpio.heartbeat_level = !g_spi_diag_gpio.heartbeat_level;
        g_spi_diag_gpio.next_heartbeat_toggle_us += SPI_DIAG_HEARTBEAT_HALF_PERIOD_US;
    }

    spi_diag_gpio_apply();
#endif
}

static void spi_diag_gpio_set_drdy(bool active) {
    g_spi_diag_gpio.drdy_active = active;
    spi_diag_gpio_apply();
}

static void spi_diag_gpio_note_spi_activity(void) {
#if SNIFFER_SPI_DRDY_PURE
    spi_diag_gpio_apply();
#else
    g_spi_diag_gpio.spi_pulse_until_us = time_us_64() + SPI_DIAG_PULSE_US;
    spi_diag_gpio_apply();
#endif
}

static void update_spi_drdy(uint16_t queued_events) {
    spi_diag_gpio_set_drdy(queued_events > 0u);
}

static void latest_snapshot_clear(latest_snapshot_state_t *snapshot) {
    memset(snapshot, 0, sizeof(*snapshot));
}

static int latest_snapshot_find_slot(const latest_snapshot_state_t *snapshot,
                                     uint8_t channel,
                                     uint8_t label,
                                     uint8_t sdi) {
    for (uint8_t index = 0; index < snapshot->valid_count; ++index) {
        const latest_slot_t *slot = &snapshot->slots[index];
        if (slot->valid &&
            slot->channel == channel &&
            slot->label == label &&
            slot->sdi == sdi) {
            return (int)index;
        }
    }

    return -1;
}

static uint8_t latest_snapshot_pick_slot(latest_snapshot_state_t *snapshot) {
    if (snapshot->valid_count < LATEST_SLOT_CAPACITY) {
        return snapshot->valid_count++;
    }

    uint8_t victim_index = 0u;
    uint32_t victim_counter = snapshot->slots[0].update_counter;

    for (uint8_t index = 1u; index < LATEST_SLOT_CAPACITY; ++index) {
        if (snapshot->slots[index].update_counter < victim_counter) {
            victim_counter = snapshot->slots[index].update_counter;
            victim_index = index;
        }
    }

    ++snapshot->slot_evictions;
    return victim_index;
}

static void latest_snapshot_update(latest_snapshot_state_t *snapshot,
                                   uint8_t channel,
                                   uint8_t label,
                                   uint8_t sdi,
                                   uint8_t ssm,
                                   bool parity_ok,
                                   uint32_t raw_value,
                                   int32_t scaled_tenths) {
    int slot_index = latest_snapshot_find_slot(snapshot, channel, label, sdi);

    if (slot_index < 0) {
        slot_index = (int)latest_snapshot_pick_slot(snapshot);
        snapshot->slots[slot_index].hit_count = 0u;
    }

    latest_slot_t *slot = &snapshot->slots[slot_index];
    slot->valid = true;
    slot->parity_ok = parity_ok;
    slot->channel = channel;
    slot->label = label;
    slot->sdi = sdi;
    slot->ssm = ssm;
    slot->raw_value = raw_value;
    slot->scaled_tenths = scaled_tenths;
    slot->update_counter = ++snapshot->last_update_counter;
    ++slot->hit_count;
    ++snapshot->snapshot_revision;
    spi_diag_gpio_set_drdy(true);
}

static bool event_queue_push(event_queue_t *queue, const spi_event_t *event, rx_stats_t *stats) {
    if (queue->queued >= SPI_EVENT_QUEUE_DEPTH) {
        ++stats->spi_drop_events;
        return false;
    }

    queue->entries[queue->write_index] = *event;
    queue->write_index = (uint16_t)((queue->write_index + 1u) % SPI_EVENT_QUEUE_DEPTH);
    ++queue->queued;
    update_spi_drdy(queue->queued);
    return true;
}

static bool event_queue_pop(event_queue_t *queue, spi_event_t *event) {
    if (queue->queued == 0u) {
        return false;
    }

    *event = queue->entries[queue->read_index];
    queue->read_index = (uint16_t)((queue->read_index + 1u) % SPI_EVENT_QUEUE_DEPTH);
    --queue->queued;
    update_spi_drdy(queue->queued);
    return true;
}

static void enqueue_from_sm(PIO pio,
                            uint sm,
                            uint8_t channel,
                            uint32_t *write_index,
                            uint32_t *queued_words,
                            uint32_t *invalid_streak,
                            absolute_time_t *last_valid_time,
                            rx_stats_t *stats,
                            const filter_state_t *filter,
                            bool *drained_any) {
    while (!pio_sm_is_rx_fifo_empty(pio, sm)) {
        *drained_any = true;
        const uint32_t word = pio_sm_get(pio, sm);
        *last_valid_time = get_absolute_time();
        if (channel == CHANNEL_FWD) {
            ++stats->fwd_raw_words;
        } else {
            ++stats->rev_raw_words;
        }
        ++stats->received_words;

        if (!parity_is_ok(word)) {
            ++stats->parity_errors;
            log_logic_rejected_word(word, channel, filter, stats);
            ++(*invalid_streak);
            if (*invalid_streak >= RESYNC_WORD_THRESHOLD) {
                restart_rx_state_machine(pio, sm);
                note_channel_resync(channel, "invalid_streak", stats);
                *invalid_streak = 0u;
                return;
            }
            continue;
        }

        if (!should_buffer_word(word, channel, filter)) {
            ++stats->filtered_words;
            log_logic_rejected_word(word, channel, filter, stats);
            ++(*invalid_streak);
            if (*invalid_streak >= RESYNC_WORD_THRESHOLD) {
                restart_rx_state_machine(pio, sm);
                note_channel_resync(channel, "invalid_streak", stats);
                *invalid_streak = 0u;
                return;
            }
            continue;
        }

        *invalid_streak = 0u;
        capture_queue_push(word, channel, write_index, queued_words, stats);
    }
}

static uint16_t u16_saturated(uint32_t value) {
    return value > 0xFFFFu ? 0xFFFFu : (uint16_t)value;
}

static uint16_t crc16_ccitt(const uint8_t *data, size_t length) {
    uint16_t crc = 0xFFFFu;

    for (size_t i = 0; i < length; ++i) {
        crc ^= (uint16_t)data[i] << 8;
        for (uint8_t bit = 0; bit < 8u; ++bit) {
            if (crc & 0x8000u) {
                crc = (uint16_t)((crc << 1u) ^ 0x1021u);
            } else {
                crc <<= 1u;
            }
        }
    }

    return crc;
}

static void build_spi_packet(sniffer_spi_packet_t *packet,
                             uint8_t command,
                             uint8_t status,
                             uint8_t flags,
                             uint16_t sequence,
                             const void *payload,
                             size_t payload_size) {
    memset(packet, 0, sizeof(*packet));
    packet->magic[0] = SNIFFER_SPI_MAGIC_0;
    packet->magic[1] = SNIFFER_SPI_MAGIC_1;
    packet->version = SNIFFER_SPI_PROTOCOL_VERSION;
    packet->command = command;
    packet->status = status;
    packet->flags = flags;
    packet->sequence = sequence;

    if (payload != NULL && payload_size > 0u) {
        if (payload_size > sizeof(packet->payload)) {
            payload_size = sizeof(packet->payload);
        }
        memcpy(packet->payload, payload, payload_size);
    }

    packet->crc16 = crc16_ccitt((const uint8_t *)packet, sizeof(*packet) - sizeof(packet->crc16));
}

static bool packet_has_valid_magic(const sniffer_spi_packet_t *packet) {
    return packet->magic[0] == SNIFFER_SPI_MAGIC_0 &&
           packet->magic[1] == SNIFFER_SPI_MAGIC_1 &&
           packet->version == SNIFFER_SPI_PROTOCOL_VERSION;
}

static bool packet_has_valid_crc(const sniffer_spi_packet_t *packet) {
    const uint16_t expected = crc16_ccitt((const uint8_t *)packet,
                                          sizeof(*packet) - sizeof(packet->crc16));
    return expected == packet->crc16;
}

static void spi_diag_dump_transaction(const spi_link_state_t *spi_link,
                                      const char *verdict,
                                      uint8_t command,
                                      bool magic_ok,
                                      bool crc_ok) {
#if ENABLE_SPI_DIAG_LOG
    if (strcmp(verdict, "request_ok") == 0 &&
        SPI_DIAG_OK_LOG_STRIDE > 0u &&
        ((spi_link->transaction_counter % SPI_DIAG_OK_LOG_STRIDE) != 0u)) {
        return;
    }

    const uint8_t b0 = spi_link->rx_count > 0u ? spi_link->rx_bytes[0] : 0u;
    const uint8_t b1 = spi_link->rx_count > 1u ? spi_link->rx_bytes[1] : 0u;
    const uint8_t b2 = spi_link->rx_count > 2u ? spi_link->rx_bytes[2] : 0u;
    const uint8_t b3 = spi_link->rx_count > 3u ? spi_link->rx_bytes[3] : 0u;
    const uint8_t b4 = spi_link->rx_count > 4u ? spi_link->rx_bytes[4] : 0u;
    const uint8_t b5 = spi_link->rx_count > 5u ? spi_link->rx_bytes[5] : 0u;
    const uint8_t b6 = spi_link->rx_count > 6u ? spi_link->rx_bytes[6] : 0u;
    const uint8_t b7 = spi_link->rx_count > 7u ? spi_link->rx_bytes[7] : 0u;

    printf("[SPI-DIAG] #%lu len=%u verdict=%s cmd=0x%02X magic=%s crc=%s first8=%02X %02X %02X %02X %02X %02X %02X %02X\r\n",
           (unsigned long)spi_link->transaction_counter,
           spi_link->rx_count,
           verdict,
           command,
           magic_ok ? "OK" : "BAD",
           crc_ok ? "OK" : "BAD",
           b0,
           b1,
           b2,
           b3,
           b4,
           b5,
           b6,
           b7);
#else
    (void)spi_link;
    (void)verdict;
    (void)command;
    (void)magic_ok;
    (void)crc_ok;
#endif
}

static void build_spi_pong_packet(sniffer_spi_packet_t *packet,
                                  uint16_t sequence,
                                  uint16_t queued_events) {
    const uint8_t flags = queued_events > 0u ? 0x01u : 0x00u;
    build_spi_packet(packet,
                     SNIFFER_SPI_RESPONSE_PONG,
                     SNIFFER_SPI_STATUS_OK,
                     flags,
                     sequence,
                     NULL,
                     0u);
}

static void build_spi_event_packet(sniffer_spi_packet_t *packet,
                                   const spi_event_t *event,
                                   uint16_t sequence,
                                   uint16_t queued_events_after_pop,
                                   uint16_t dropped_events) {
    sniffer_spi_event_payload_t payload = {
        .event_type = frame_is_ack(event->label, event->sdi) ? SNIFFER_SPI_EVENT_ACK : SNIFFER_SPI_EVENT_DATA,
        .channel = event->channel,
        .label = event->label,
        .sdi = event->sdi,
        .ssm = event->ssm,
        .parity_ok = event->parity_ok ? 1u : 0u,
        .raw_value = event->raw_value,
        .scaled_tenths = event->scaled_tenths,
        .event_counter = event->event_counter,
        .drop_counter = dropped_events,
        .queue_depth = queued_events_after_pop,
    };

    build_spi_packet(packet,
                     SNIFFER_SPI_RESPONSE_EVENT,
                     SNIFFER_SPI_STATUS_OK,
                     queued_events_after_pop > 0u ? 0x01u : 0x00u,
                     sequence,
                     &payload,
                     sizeof(payload));
}

static void build_spi_stats_packet(sniffer_spi_packet_t *packet,
                                   const rx_stats_t *stats,
                                   uint16_t sequence,
                                   uint16_t queued_events) {
    const sniffer_spi_stats_payload_t payload = {
        .received_words = stats->received_words,
        .accepted_words = stats->accepted_words,
        .filtered_words = stats->filtered_words,
        .parity_errors = stats->parity_errors,
        .overflow_events = u16_saturated(stats->overflow_events),
        .fwd_resync_events = u16_saturated(stats->fwd_resync_events),
        .rev_resync_events = u16_saturated(stats->rev_resync_events),
    };

    build_spi_packet(packet,
                     SNIFFER_SPI_RESPONSE_STATS,
                     SNIFFER_SPI_STATUS_OK,
                     queued_events > 0u ? 0x01u : 0x00u,
                     sequence,
                     &payload,
                     sizeof(payload));
}

static void build_spi_filter_packet(sniffer_spi_packet_t *packet,
                                    const filter_state_t *filter,
                                    uint16_t sequence,
                                    uint16_t queued_events) {
    sniffer_spi_filter_payload_t payload = {
        .mode = filter->pass_all ? SNIFFER_SPI_FILTER_MODE_PASS_ALL : SNIFFER_SPI_FILTER_MODE_WHITELIST,
        .count = filter->count,
    };

    for (uint8_t i = 0; i < filter->count && i < SNIFFER_SPI_FILTER_CAPACITY; ++i) {
        payload.entries[i][0] = filter->entries[i].label;
        payload.entries[i][1] = filter->entries[i].sdi;
    }

    build_spi_packet(packet,
                     SNIFFER_SPI_RESPONSE_FILTER,
                     SNIFFER_SPI_STATUS_OK,
                     queued_events > 0u ? 0x01u : 0x00u,
                     sequence,
                     &payload,
                     sizeof(payload));
}

static void build_spi_latest_meta_packet(sniffer_spi_packet_t *packet,
                                         const latest_snapshot_state_t *snapshot,
                                         const filter_state_t *filter,
                                         const rx_stats_t *stats,
                                         uint16_t sequence,
                                         uint16_t queued_events) {
    const sniffer_spi_latest_meta_payload_t payload = {
        .slot_count = snapshot->valid_count,
        .slot_capacity = LATEST_SLOT_CAPACITY,
        .flags = filter->pass_all ? 0x01u : 0x00u,
        .reserved0 = 0u,
        .snapshot_revision = snapshot->snapshot_revision,
        .slot_evictions = snapshot->slot_evictions,
        .last_update_counter = snapshot->last_update_counter,
        .fwd_startup_resync_events = u16_saturated(stats->fwd_startup_resync_events),
        .fwd_operational_resync_events = u16_saturated(stats->fwd_operational_resync_events),
        .reserved1 = 0u,
    };

    build_spi_packet(packet,
                     SNIFFER_SPI_RESPONSE_LATEST_META,
                     SNIFFER_SPI_STATUS_OK,
                     queued_events > 0u ? 0x01u : 0x00u,
                     sequence,
                     &payload,
                     sizeof(payload));
}

static void build_spi_latest_slot_packet(sniffer_spi_packet_t *packet,
                                         const latest_snapshot_state_t *snapshot,
                                         uint8_t slot_index,
                                         uint16_t sequence,
                                         uint16_t queued_events) {
    sniffer_spi_latest_slot_payload_t payload = {0};

    if (slot_index < snapshot->valid_count) {
        const latest_slot_t *slot = &snapshot->slots[slot_index];
        payload.channel = slot->channel;
        payload.label = slot->label;
        payload.sdi = slot->sdi;
        payload.ssm = slot->ssm;
        payload.flags = (slot->valid ? 0x01u : 0x00u) |
                        (slot->parity_ok ? 0x02u : 0x00u);
        payload.raw_value = slot->raw_value;
        payload.scaled_tenths = slot->scaled_tenths;
        payload.update_counter = slot->update_counter;
        payload.hit_count = slot->hit_count;
    }

    build_spi_packet(packet,
                     SNIFFER_SPI_RESPONSE_LATEST_SLOT,
                     SNIFFER_SPI_STATUS_OK,
                     queued_events > 0u ? 0x01u : 0x00u,
                     sequence,
                     &payload,
                     sizeof(payload));
}

static bool apply_filter_payload(filter_state_t *filter, const sniffer_spi_filter_payload_t *payload) {
    if (payload->count > SNIFFER_SPI_FILTER_CAPACITY) {
        return false;
    }

    if (payload->mode == SNIFFER_SPI_FILTER_MODE_PASS_ALL) {
        filter->pass_all = true;
        filter->count = payload->count;
    } else if (payload->mode == SNIFFER_SPI_FILTER_MODE_WHITELIST) {
        filter->pass_all = false;
        filter->count = payload->count;
    } else {
        return false;
    }

    memset(filter->entries, 0, sizeof(filter->entries));
    for (uint8_t i = 0; i < payload->count; ++i) {
        filter->entries[i].label = payload->entries[i][0];
        filter->entries[i].sdi = payload->entries[i][1];
    }

    return true;
}

static void reset_runtime_counters(rx_stats_t *stats,
                                   event_queue_t *event_queue,
                                   latest_snapshot_state_t *latest_snapshot) {
    memset(stats, 0, sizeof(*stats));
    event_queue_clear(event_queue);
    latest_snapshot_clear(latest_snapshot);
    update_spi_drdy(event_queue->queued);
}

static void spi_flush_rx_fifo(void) {
    while (spi_is_readable(SNIFFER_SPI_PORT)) {
        (void)spi_get_hw(SNIFFER_SPI_PORT)->dr;
    }
}

static void spi_prefill_tx_byte(uint8_t value) {
    if (spi_is_writable(SNIFFER_SPI_PORT)) {
        spi_get_hw(SNIFFER_SPI_PORT)->dr = value;
    }
}

static void __not_in_flash_func(spi_irq_tx_disable)(void) {
    spi_get_hw(SNIFFER_SPI_PORT)->imsc &= ~SPI_SSPIMSC_TXIM_BITS;
}

static void __not_in_flash_func(spi_irq_tx_enable)(void) {
    spi_get_hw(SNIFFER_SPI_PORT)->imsc |= SPI_SSPIMSC_TXIM_BITS;
}

static void __not_in_flash_func(spi_irq_prefill_response)(spi_link_state_t *spi_link) {
    spi_hw_t *hw = spi_get_hw(SNIFFER_SPI_PORT);
    const uint8_t *response_bytes = (const uint8_t *)&spi_link->tx_packet;

    while ((hw->sr & SPI_SSPSR_TNF_BITS) && spi_link->tx_index < SNIFFER_SPI_PACKET_SIZE) {
        hw->dr = response_bytes[spi_link->tx_index++];
    }

    if (spi_link->tx_index >= SNIFFER_SPI_PACKET_SIZE) {
        spi_irq_tx_disable();
    }
}

static void __not_in_flash_func(spi_irq_flush_rx_fifo)(void) {
    spi_hw_t *hw = spi_get_hw(SNIFFER_SPI_PORT);

    while (hw->sr & SPI_SSPSR_RNE_BITS) {
        (void)hw->dr;
    }
    hw->icr = SPI_SSPICR_RORIC_BITS | SPI_SSPICR_RTIC_BITS;
}

static void __not_in_flash_func(spi_clear_fifos_between_transfers)(void) {
    spi_hw_t *hw = spi_get_hw(SNIFFER_SPI_PORT);
    const uint32_t cr1 = hw->cr1;

    hw->cr1 = cr1 & ~SPI_SSPCR1_SSE_BITS;
    while (hw->sr & SPI_SSPSR_RNE_BITS) {
        (void)hw->dr;
    }
    hw->icr = SPI_SSPICR_RORIC_BITS | SPI_SSPICR_RTIC_BITS;
    hw->cr1 = cr1;
}

static void spi_dma_note_complete(spi_link_state_t *spi_link) {
#if ENABLE_SPI_TX_DMA
    if (spi_link->tx_dma_ready &&
        spi_link->tx_dma_active &&
        !dma_channel_is_busy((uint)spi_link->tx_dma_chan)) {
        spi_link->tx_dma_active = false;
        ++spi_link->tx_dma_done_count;
        spi_get_hw(SNIFFER_SPI_PORT)->dmacr &= ~SPI_SSPDMACR_TXDMAE_BITS;
    }
#else
    (void)spi_link;
#endif
}

static void spi_dma_abort_response(spi_link_state_t *spi_link) {
#if ENABLE_SPI_TX_DMA
    if (!spi_link->tx_dma_ready || !spi_link->tx_dma_active) {
        return;
    }

    spi_get_hw(SNIFFER_SPI_PORT)->dmacr &= ~SPI_SSPDMACR_TXDMAE_BITS;
    if (dma_channel_is_busy((uint)spi_link->tx_dma_chan)) {
        dma_channel_abort((uint)spi_link->tx_dma_chan);
        ++spi_link->tx_dma_abort_count;
    } else {
        ++spi_link->tx_dma_done_count;
    }
    spi_link->tx_dma_active = false;
#else
    (void)spi_link;
#endif
}

static void spi_dma_start_response(spi_link_state_t *spi_link) {
#if ENABLE_SPI_TX_DMA
    if (!spi_link->tx_dma_ready) {
        return;
    }

    spi_dma_abort_response(spi_link);

    dma_channel_config config = dma_channel_get_default_config((uint)spi_link->tx_dma_chan);
    channel_config_set_transfer_data_size(&config, DMA_SIZE_8);
    channel_config_set_read_increment(&config, true);
    channel_config_set_write_increment(&config, false);
    channel_config_set_dreq(&config, spi_get_dreq(SNIFFER_SPI_PORT, true));

    spi_get_hw(SNIFFER_SPI_PORT)->dmacr |= SPI_SSPDMACR_TXDMAE_BITS;
    spi_link->tx_dma_active = true;
    ++spi_link->tx_dma_start_count;

    dma_channel_configure((uint)spi_link->tx_dma_chan,
                          &config,
                          &spi_get_hw(SNIFFER_SPI_PORT)->dr,
                          (const uint8_t *)&spi_link->tx_packet,
                          SNIFFER_SPI_PACKET_SIZE,
                          true);
#else
    (void)spi_link;
#endif
}

#if SNIFFER_SPI_PIO_FRAME_TRANSPORT
static uint8_t spi_pio_reverse8(uint8_t value) {
    value = (uint8_t)(((value & 0xF0u) >> 4) | ((value & 0x0Fu) << 4));
    value = (uint8_t)(((value & 0xCCu) >> 2) | ((value & 0x33u) << 2));
    value = (uint8_t)(((value & 0xAAu) >> 1) | ((value & 0x55u) << 1));
    return value;
}

static uint32_t spi_pio_pack_tx_word_msb_first(const uint8_t *bytes) {
    return ((uint32_t)spi_pio_reverse8(bytes[0]) << 0) |
           ((uint32_t)spi_pio_reverse8(bytes[1]) << 8) |
           ((uint32_t)spi_pio_reverse8(bytes[2]) << 16) |
           ((uint32_t)spi_pio_reverse8(bytes[3]) << 24);
}

static void spi_pio_unpack_rx_word_msb_first(uint32_t word, uint8_t *bytes) {
    bytes[0] = spi_pio_reverse8((uint8_t)((word >> 0) & 0xFFu));
    bytes[1] = spi_pio_reverse8((uint8_t)((word >> 8) & 0xFFu));
    bytes[2] = spi_pio_reverse8((uint8_t)((word >> 16) & 0xFFu));
    bytes[3] = spi_pio_reverse8((uint8_t)((word >> 24) & 0xFFu));
}

static void spi_pio_preload_frame(spi_link_state_t *spi_link,
                                  const uint8_t *frame) {
    PIO pio = spi_link->pio_spi;
    const uint sm_tx = spi_link->pio_sm_tx;

    pio_sm_clear_fifos(pio, sm_tx);
    for (uint8_t i = 0u; i < (SNIFFER_SPI_PACKET_SIZE / 4u); ++i) {
        const uint32_t word = spi_pio_pack_tx_word_msb_first(&frame[i * 4u]);
        pio_sm_put_blocking(pio, sm_tx, word);
    }
}

static void spi_pio_preload_idle_frame(spi_link_state_t *spi_link) {
    sniffer_spi_packet_t idle_packet;

    build_spi_packet(&idle_packet,
                     SNIFFER_SPI_RESPONSE_NONE,
                     SNIFFER_SPI_STATUS_EMPTY,
                     0u,
                     0u,
                     NULL,
                     0u);
    spi_pio_preload_frame(spi_link, (const uint8_t *)&idle_packet);
}

static bool spi_pio_read_frame(spi_link_state_t *spi_link,
                               uint8_t *frame) {
    PIO pio = spi_link->pio_spi;
    const uint sm_rx = spi_link->pio_sm_rx;

    if (pio_sm_get_rx_fifo_level(pio, sm_rx) < (SNIFFER_SPI_PACKET_SIZE / 4u)) {
        return false;
    }

    for (uint8_t i = 0u; i < (SNIFFER_SPI_PACKET_SIZE / 4u); ++i) {
        const uint32_t word = pio_sm_get(pio, sm_rx);
        spi_pio_unpack_rx_word_msb_first(word, &frame[i * 4u]);
    }

    return true;
}

static void spi_pio_init_link(spi_link_state_t *spi_link) {
    PIO pio = SNIFFER_SPI_PIO;
    const uint sm_rx = SNIFFER_SPI_PIO_SM_RX;
    const uint sm_tx = SNIFFER_SPI_PIO_SM_TX;
    const uint rx_offset = pio_add_program(pio, &spi_frame_rx32_program);
    const uint tx_offset = pio_add_program(pio, &spi_frame_tx32_program);

    spi_link->pio_spi = pio;
    spi_link->pio_sm_rx = sm_rx;
    spi_link->pio_sm_tx = sm_tx;
    spi_link->pio_rx_offset = rx_offset;
    spi_link->pio_tx_offset = tx_offset;

    pio_gpio_init(pio, SNIFFER_SPI_RX_PIN);
    pio_gpio_init(pio, SNIFFER_SPI_CSN_PIN);
    pio_gpio_init(pio, SNIFFER_SPI_SCK_PIN);
    pio_gpio_init(pio, SNIFFER_SPI_TX_PIN);
    gpio_pull_up(SNIFFER_SPI_CSN_PIN);
    gpio_pull_down(SNIFFER_SPI_SCK_PIN);

    pio_sm_config rx_config = spi_frame_rx32_program_get_default_config(rx_offset);
    sm_config_set_in_pins(&rx_config, SNIFFER_SPI_RX_PIN);
    sm_config_set_in_shift(&rx_config, true, false, 32);
    sm_config_set_fifo_join(&rx_config, PIO_FIFO_JOIN_RX);
    sm_config_set_clkdiv(&rx_config, 1.0f);

    pio_sm_config tx_config = spi_frame_tx32_program_get_default_config(tx_offset);
    sm_config_set_out_pins(&tx_config, SNIFFER_SPI_TX_PIN, 1);
    sm_config_set_set_pins(&tx_config, SNIFFER_SPI_TX_PIN, 1);
    sm_config_set_out_shift(&tx_config, true, false, 32);
    sm_config_set_fifo_join(&tx_config, PIO_FIFO_JOIN_TX);
    sm_config_set_clkdiv(&tx_config, 1.0f);

    pio_sm_init(pio, sm_rx, rx_offset, &rx_config);
    pio_sm_init(pio, sm_tx, tx_offset, &tx_config);
    pio_sm_set_consecutive_pindirs(pio, sm_rx, SNIFFER_SPI_RX_PIN, 1, false);
    pio_sm_set_consecutive_pindirs(pio, sm_rx, SNIFFER_SPI_CSN_PIN, 1, false);
    pio_sm_set_consecutive_pindirs(pio, sm_rx, SNIFFER_SPI_SCK_PIN, 1, false);
    pio_sm_set_consecutive_pindirs(pio, sm_tx, SNIFFER_SPI_TX_PIN, 1, true);
    pio_sm_set_pins_with_mask(pio, sm_tx, 0u, 1u << SNIFFER_SPI_TX_PIN);
    pio_sm_clear_fifos(pio, sm_rx);
    pio_sm_clear_fifos(pio, sm_tx);
    spi_pio_preload_idle_frame(spi_link);
    pio_enable_sm_mask_in_sync(pio, (1u << sm_rx) | (1u << sm_tx));
}
#endif

static void __not_in_flash_func(spi_transport_to_idle)(spi_link_state_t *spi_link) {
#if !SNIFFER_SPI_PIO_FRAME_TRANSPORT
    spi_dma_note_complete(spi_link);
#endif
    spi_link->phase = SPI_LINK_PHASE_IDLE;
    spi_link->rx_count = 0u;
    spi_link->tx_index = 0u;
    spi_link->response_rx_count = 0u;
    spi_link->next_tx = SNIFFER_SPI_TRANSPORT_IDLE;
#if !SNIFFER_SPI_PIO_FRAME_TRANSPORT
    spi_irq_tx_disable();
#endif
}

static void __not_in_flash_func(spi_transport_begin_request)(spi_link_state_t *spi_link) {
#if !SNIFFER_SPI_PIO_FRAME_TRANSPORT
    spi_dma_abort_response(spi_link);
#endif
    spi_link->phase = SPI_LINK_PHASE_COLLECT_REQUEST;
    spi_link->rx_count = 0u;
    spi_link->tx_index = 0u;
    spi_link->response_rx_count = 0u;
    spi_link->next_tx = SNIFFER_SPI_TRANSPORT_IDLE;
    spi_link->request_ready = false;
#if !SNIFFER_SPI_PIO_FRAME_TRANSPORT
    spi_irq_tx_disable();
#endif
}

static void spi_prepare_response(spi_link_state_t *spi_link,
                                 const sniffer_spi_packet_t *response) {
#if SNIFFER_SPI_PIO_FRAME_TRANSPORT
    memcpy(&spi_link->tx_packet, response, sizeof(*response));
    spi_link->phase = SPI_LINK_PHASE_STREAM_RESPONSE;
    spi_link->tx_index = SNIFFER_SPI_PACKET_SIZE;
    spi_link->response_rx_count = 0u;
    spi_link->next_tx = ((const uint8_t *)&spi_link->tx_packet)[0];
    spi_pio_preload_frame(spi_link, (const uint8_t *)&spi_link->tx_packet);
#else
    const uint32_t irq_state = save_and_disable_interrupts();

    memcpy(&spi_link->tx_packet, response, sizeof(*response));
    spi_link->phase = SPI_LINK_PHASE_STREAM_RESPONSE;
    spi_link->tx_index = 0u;
    spi_link->response_rx_count = 0u;
    spi_link->next_tx = ((const uint8_t *)&spi_link->tx_packet)[0];
    spi_irq_tx_disable();
    if (spi_link->tx_dma_ready) {
        spi_clear_fifos_between_transfers();
        spi_irq_flush_rx_fifo();
        spi_link->tx_index = SNIFFER_SPI_PACKET_SIZE;
        spi_dma_start_response(spi_link);
    } else {
        spi_irq_flush_rx_fifo();
        spi_irq_prefill_response(spi_link);
        spi_irq_tx_enable();
    }

    restore_interrupts(irq_state);
#endif
}

static void spi_init_link(spi_link_state_t *spi_link) {
    memset(spi_link, 0, sizeof(*spi_link));

#if SNIFFER_SPI_PIO_FRAME_TRANSPORT
    gpio_init(SNIFFER_SPI_DRDY_PIN);
    gpio_set_dir(SNIFFER_SPI_DRDY_PIN, GPIO_OUT);
    spi_diag_gpio_init();
    update_spi_drdy(0u);

    spi_link->next_sequence = 1u;
    spi_transport_to_idle(spi_link);
    spi_pio_init_link(spi_link);
#else
    spi_init(SNIFFER_SPI_PORT, 1000u * 1000u);
    spi_set_slave(SNIFFER_SPI_PORT, true);
    spi_set_format(SNIFFER_SPI_PORT, 8u, SPI_CPOL_0, SPI_CPHA_0, SPI_MSB_FIRST);

    gpio_set_function(SNIFFER_SPI_RX_PIN, GPIO_FUNC_SPI);
    gpio_set_function(SNIFFER_SPI_CSN_PIN, GPIO_FUNC_SPI);
    gpio_set_function(SNIFFER_SPI_SCK_PIN, GPIO_FUNC_SPI);
    gpio_set_function(SNIFFER_SPI_TX_PIN, GPIO_FUNC_SPI);
    gpio_pull_up(SNIFFER_SPI_CSN_PIN);

    gpio_init(SNIFFER_SPI_DRDY_PIN);
    gpio_set_dir(SNIFFER_SPI_DRDY_PIN, GPIO_OUT);
    spi_diag_gpio_init();
    update_spi_drdy(0u);

    spi_get_hw(SNIFFER_SPI_PORT)->dmacr = 0u;
    spi_get_hw(SNIFFER_SPI_PORT)->icr = SPI_SSPICR_RORIC_BITS | SPI_SSPICR_RTIC_BITS;
    spi_get_hw(SNIFFER_SPI_PORT)->imsc = 0u;
#if ENABLE_SPI_TX_DMA
    spi_link->tx_dma_chan = dma_claim_unused_channel(true);
    spi_link->tx_dma_ready = spi_link->tx_dma_chan >= 0;
#else
    spi_link->tx_dma_chan = -1;
    spi_link->tx_dma_ready = false;
#endif
    spi_link->next_sequence = 1u;
    spi_transport_to_idle(spi_link);
    spi_flush_rx_fifo();
#if ENABLE_SPI_IRQ_TRANSPORT
    g_spi_irq_link = spi_link;
    irq_set_exclusive_handler(SNIFFER_SPI_IRQ, spi_irq_handler);
    irq_set_priority(SNIFFER_SPI_IRQ, 0x00u);
    irq_set_enabled(SNIFFER_SPI_IRQ, true);
    spi_get_hw(SNIFFER_SPI_PORT)->imsc =
        SPI_SSPIMSC_RXIM_BITS |
        SPI_SSPIMSC_RTIM_BITS |
        SPI_SSPIMSC_RORIM_BITS;
#else
    spi_prefill_tx_byte(spi_link->next_tx);
#endif
#endif
}

static void process_spi_request(spi_link_state_t *spi_link,
                                rx_stats_t *stats,
                                event_queue_t *event_queue,
                                filter_state_t *filter,
                                latest_snapshot_state_t *latest_snapshot) {
    const sniffer_spi_packet_t *request = (const sniffer_spi_packet_t *)spi_link->rx_bytes;
    sniffer_spi_packet_t response;
    const uint16_t response_sequence = spi_link->next_sequence++;
    const uint8_t flags = event_queue->queued > 0u ? 0x01u : 0x00u;

    if (!packet_has_valid_magic(request)) {
        spi_diag_dump_transaction(spi_link, "bad_magic", request->command, false, false);
        build_spi_packet(&response,
                         SNIFFER_SPI_RESPONSE_ACK,
                         SNIFFER_SPI_STATUS_BAD_MAGIC,
                         flags,
                         response_sequence,
                         NULL,
                         0u);
        spi_prepare_response(spi_link, &response);
        return;
    }

    if (!packet_has_valid_crc(request)) {
        spi_diag_dump_transaction(spi_link, "bad_crc", request->command, true, false);
        build_spi_packet(&response,
                         SNIFFER_SPI_RESPONSE_ACK,
                         SNIFFER_SPI_STATUS_BAD_CRC,
                         flags,
                         response_sequence,
                         NULL,
                         0u);
        spi_prepare_response(spi_link, &response);
        return;
    }

    spi_diag_dump_transaction(spi_link, "request_ok", request->command, true, true);

    switch (request->command) {
        case SNIFFER_SPI_CMD_NOP:
        case SNIFFER_SPI_CMD_PING:
            build_spi_pong_packet(&response, response_sequence, event_queue->queued);
            break;

        case SNIFFER_SPI_CMD_POP_EVENT: {
            spi_event_t event;
            if (!event_queue_pop(event_queue, &event)) {
                build_spi_packet(&response,
                                 SNIFFER_SPI_RESPONSE_EVENT,
                                 SNIFFER_SPI_STATUS_EMPTY,
                                 0u,
                                 response_sequence,
                                 NULL,
                                 0u);
            } else {
                build_spi_event_packet(&response,
                                       &event,
                                       response_sequence,
                                       event_queue->queued,
                                       u16_saturated(stats->spi_drop_events));
            }
            break;
        }

        case SNIFFER_SPI_CMD_GET_STATS:
            build_spi_stats_packet(&response, stats, response_sequence, event_queue->queued);
            break;

        case SNIFFER_SPI_CMD_GET_FILTER:
            build_spi_filter_packet(&response, filter, response_sequence, event_queue->queued);
            break;

        case SNIFFER_SPI_CMD_GET_LATEST_META:
            build_spi_latest_meta_packet(&response,
                                         latest_snapshot,
                                         filter,
                                         stats,
                                         response_sequence,
                                         event_queue->queued);
            update_spi_drdy(event_queue->queued);
            break;

        case SNIFFER_SPI_CMD_GET_LATEST_SLOT: {
            const uint8_t slot_index = request->payload[0];
            build_spi_latest_slot_packet(&response,
                                         latest_snapshot,
                                         slot_index,
                                         response_sequence,
                                         event_queue->queued);
            break;
        }

        case SNIFFER_SPI_CMD_SET_FILTER: {
            const sniffer_spi_filter_payload_t *payload = (const sniffer_spi_filter_payload_t *)request->payload;
            if (!apply_filter_payload(filter, payload)) {
                build_spi_packet(&response,
                                 SNIFFER_SPI_RESPONSE_ACK,
                                 SNIFFER_SPI_STATUS_BAD_PAYLOAD,
                                 flags,
                                 response_sequence,
                                 NULL,
                                 0u);
            } else {
                event_queue_clear(event_queue);
                update_spi_drdy(event_queue->queued);
                latest_snapshot_clear(latest_snapshot);
                build_spi_filter_packet(&response, filter, response_sequence, event_queue->queued);
            }
            break;
        }

        case SNIFFER_SPI_CMD_RESET_STATS:
            reset_runtime_counters(stats, event_queue, latest_snapshot);
            build_spi_stats_packet(&response, stats, response_sequence, event_queue->queued);
            break;

        default:
            build_spi_packet(&response,
                             SNIFFER_SPI_RESPONSE_ACK,
                             SNIFFER_SPI_STATUS_BAD_COMMAND,
                             flags,
                             response_sequence,
                             NULL,
                             0u);
            break;
    }

    spi_prepare_response(spi_link, &response);
}

static void spi_consume_transport_byte(spi_link_state_t *spi_link,
                                       rx_stats_t *stats,
                                       event_queue_t *event_queue,
                                       filter_state_t *filter,
                                       latest_snapshot_state_t *latest_snapshot,
                                       uint8_t rx_byte) {
    const uint64_t now_us = time_us_64();
    const uint8_t *response_bytes = (const uint8_t *)&spi_link->tx_packet;
    const bool stale_gap = spi_link->last_byte_us != 0u &&
                           (now_us - spi_link->last_byte_us) >= SPI_TRANSPORT_STALE_GAP_US;

    spi_link->last_byte_us = now_us;
    spi_diag_gpio_note_spi_activity();

    if (stale_gap && spi_link->phase == SPI_LINK_PHASE_COLLECT_REQUEST && spi_link->rx_count > 0u) {
        spi_diag_dump_transaction(spi_link, "stale_reset", 0u, false, false);
        spi_transport_to_idle(spi_link);
    }

    if (rx_byte == SNIFFER_SPI_TRANSPORT_RESET &&
        (spi_link->phase != SPI_LINK_PHASE_COLLECT_REQUEST || spi_link->rx_count == 0u || stale_gap)) {
        ++spi_link->reset_counter;
        spi_transport_begin_request(spi_link);
        return;
    }

    switch (spi_link->phase) {
        case SPI_LINK_PHASE_IDLE:
            spi_link->next_tx = SNIFFER_SPI_TRANSPORT_IDLE;
            break;

        case SPI_LINK_PHASE_COLLECT_REQUEST:
            if (spi_link->rx_count < SNIFFER_SPI_PACKET_SIZE) {
                spi_link->rx_bytes[spi_link->rx_count++] = rx_byte;
            }

            if (spi_link->rx_count == SNIFFER_SPI_PACKET_SIZE) {
                ++spi_link->transaction_counter;
                process_spi_request(spi_link, stats, event_queue, filter, latest_snapshot);
            } else {
                spi_link->next_tx = SNIFFER_SPI_TRANSPORT_IDLE;
            }
            break;

        case SPI_LINK_PHASE_REQUEST_READY:
            break;

        case SPI_LINK_PHASE_STREAM_RESPONSE:
            if (spi_link->tx_index < SNIFFER_SPI_PACKET_SIZE) {
                spi_link->next_tx = response_bytes[spi_link->tx_index++];
            } else {
                spi_transport_to_idle(spi_link);
            }
            break;
    }
}

static void __not_in_flash_func(spi_irq_consume_transport_byte)(spi_link_state_t *spi_link,
                                                                uint8_t rx_byte,
                                                                uint64_t now_us) {
    const bool stale_gap = spi_link->last_byte_us != 0u &&
                           (now_us - spi_link->last_byte_us) >= SPI_TRANSPORT_STALE_GAP_US;

    spi_link->last_byte_us = now_us;

    if (stale_gap && spi_link->phase == SPI_LINK_PHASE_COLLECT_REQUEST && spi_link->rx_count > 0u) {
        spi_transport_to_idle(spi_link);
    }

    if (rx_byte == SNIFFER_SPI_TRANSPORT_RESET &&
        (spi_link->phase != SPI_LINK_PHASE_COLLECT_REQUEST || spi_link->rx_count == 0u || stale_gap)) {
        ++spi_link->reset_counter;
        spi_transport_begin_request(spi_link);
        return;
    }

    switch (spi_link->phase) {
        case SPI_LINK_PHASE_IDLE:
            break;

        case SPI_LINK_PHASE_COLLECT_REQUEST:
            if (spi_link->rx_count < SNIFFER_SPI_PACKET_SIZE) {
                spi_link->rx_bytes[spi_link->rx_count++] = rx_byte;
            }

            if (spi_link->rx_count == SNIFFER_SPI_PACKET_SIZE) {
                ++spi_link->transaction_counter;
                if (spi_link->request_ready) {
                    spi_link->request_overrun = true;
                } else {
                    spi_link->request_ready = true;
                    spi_link->phase = SPI_LINK_PHASE_REQUEST_READY;
                }
            }
            break;

        case SPI_LINK_PHASE_REQUEST_READY:
            break;

        case SPI_LINK_PHASE_STREAM_RESPONSE:
            if (++spi_link->response_rx_count >= SNIFFER_SPI_PACKET_SIZE) {
                spi_transport_to_idle(spi_link);
            }
            break;
    }
}

static void __not_in_flash_func(spi_irq_handler)(void) {
    spi_hw_t *hw = spi_get_hw(SNIFFER_SPI_PORT);
    spi_link_state_t *spi_link = g_spi_irq_link;
    const uint32_t mis = hw->mis;

    if (spi_link == NULL) {
        hw->icr = SPI_SSPICR_RORIC_BITS | SPI_SSPICR_RTIC_BITS;
        return;
    }

    ++spi_link->irq_count;

    if (spi_link->phase == SPI_LINK_PHASE_STREAM_RESPONSE && !spi_link->tx_dma_ready) {
        spi_irq_prefill_response(spi_link);
    }

    if (mis & SPI_SSPMIS_RORMIS_BITS) {
        ++spi_link->ror_irq_count;
        hw->icr = SPI_SSPICR_RORIC_BITS;
    }
    if (mis & SPI_SSPMIS_RTMIS_BITS) {
        ++spi_link->rt_irq_count;
        hw->icr = SPI_SSPICR_RTIC_BITS;
    }

    if (mis & (SPI_SSPMIS_RXMIS_BITS | SPI_SSPMIS_RTMIS_BITS | SPI_SSPMIS_RORMIS_BITS)) {
        const uint64_t now_us = time_us_64();

        ++spi_link->rx_irq_count;
        while (hw->sr & SPI_SSPSR_RNE_BITS) {
            const uint8_t rx_byte = (uint8_t)hw->dr;
            spi_irq_consume_transport_byte(spi_link, rx_byte, now_us);
        }
    }

    if (mis & SPI_SSPMIS_TXMIS_BITS) {
        ++spi_link->tx_irq_count;
        if (spi_link->phase == SPI_LINK_PHASE_STREAM_RESPONSE && !spi_link->tx_dma_ready) {
            spi_irq_prefill_response(spi_link);
        } else {
            spi_irq_tx_disable();
        }
    }
}

static void service_spi_link(spi_link_state_t *spi_link,
                             rx_stats_t *stats,
                             event_queue_t *event_queue,
                             filter_state_t *filter,
                             latest_snapshot_state_t *latest_snapshot) {
#if SNIFFER_SPI_PIO_FRAME_TRANSPORT
    uint8_t frame[SNIFFER_SPI_PACKET_SIZE];

    while (spi_pio_read_frame(spi_link, frame)) {
        ++spi_link->pio_frame_count;
        spi_link->last_byte_us = time_us_64();
        spi_diag_gpio_note_spi_activity();

        if (spi_link->phase == SPI_LINK_PHASE_STREAM_RESPONSE) {
            ++spi_link->pio_response_frame_count;
            spi_link->response_rx_count = SNIFFER_SPI_PACKET_SIZE;
            spi_transport_to_idle(spi_link);
            spi_pio_preload_idle_frame(spi_link);
            continue;
        }

        ++spi_link->transaction_counter;
        ++spi_link->pio_request_frame_count;
        memcpy(spi_link->rx_bytes, frame, sizeof(frame));
        spi_link->rx_count = SNIFFER_SPI_PACKET_SIZE;
        process_spi_request(spi_link, stats, event_queue, filter, latest_snapshot);
    }
#elif ENABLE_SPI_IRQ_TRANSPORT
    bool request_ready = false;
    uint8_t request_bytes[SNIFFER_SPI_PACKET_SIZE];

    const uint32_t irq_state = save_and_disable_interrupts();
    if (spi_link->request_ready) {
        memcpy(request_bytes, spi_link->rx_bytes, sizeof(request_bytes));
        spi_link->request_ready = false;
        request_ready = true;
    }
    restore_interrupts(irq_state);

    if (request_ready) {
        memcpy(spi_link->rx_bytes, request_bytes, sizeof(request_bytes));
        process_spi_request(spi_link, stats, event_queue, filter, latest_snapshot);
    }
#else
    while (spi_is_readable(SNIFFER_SPI_PORT)) {
        const uint8_t rx_byte = (uint8_t)spi_get_hw(SNIFFER_SPI_PORT)->dr;
        spi_consume_transport_byte(spi_link, stats, event_queue, filter, latest_snapshot, rx_byte);
        spi_get_hw(SNIFFER_SPI_PORT)->icr = SPI_SSPICR_RORIC_BITS | SPI_SSPICR_RTIC_BITS;
        spi_prefill_tx_byte(spi_link->next_tx);
    }
#endif
}

static void print_decoded_record(uint32_t word,
                                 uint8_t channel,
                                 rx_stats_t *stats,
                                 const filter_state_t *filter,
                                 event_queue_t *event_queue,
                                 latest_snapshot_state_t *latest_snapshot) {
    const uint8_t label = ARINC429_LOGIC_MODE ? arinc429_word_label(word) : (uint8_t)((word >> 0) & 0xFFu);
    const uint8_t sdi = ARINC429_LOGIC_MODE ? arinc429_word_sdi(word) : (uint8_t)((word >> 8) & 0x03u);
    const uint32_t raw = ARINC429_LOGIC_MODE ? arinc429_word_data(word) : ((word >> 10) & 0x7FFFFu);
    const uint8_t ssm = ARINC429_LOGIC_MODE ? arinc429_word_ssm(word) : (uint8_t)((word >> 29) & 0x03u);
    const bool parity_ok = parity_is_ok(word);
    const bool strict_ack = frame_is_strict_ack(channel, label, sdi, ssm, parity_ok);
    const float value = decode_value(label, raw);
    const char *name = strict_ack ? "ACK_BATCH" : label_to_name(label);
    const char *unit = strict_ack ? "batch" : label_to_unit(label);
    const char *parity_text = parity_ok ? "OK" : "ERROR";

    if (!frame_passes_filter(filter, channel, label, sdi, ssm, parity_ok)) {
        ++stats->filtered_words;
        return;
    }

    if (!payload_is_plausible(label, value)) {
        ++stats->filtered_words;
        return;
    }

    if (!parity_ok) {
        return;
    }

    ++stats->accepted_words;
    if (channel == CHANNEL_FWD) {
        stats->fwd_link_armed = true;
    }
    latest_snapshot_update(latest_snapshot,
                           channel,
                           label,
                           sdi,
                           ssm,
                           parity_ok,
                           raw,
                           decode_value_tenths(label, raw));

#if ENABLE_SPI_EVENT_DEBUG_QUEUE
    spi_event_t event = {
        .event_counter = ++event_queue->next_event_counter,
        .raw_value = raw,
        .scaled_tenths = decode_value_tenths(label, raw),
        .channel = channel,
        .label = label,
        .sdi = sdi,
        .ssm = ssm,
        .parity_ok = parity_ok,
    };
    (void)event_queue_push(event_queue, &event, stats);
#else
    (void)event_queue;
#endif

#if ENABLE_MATCH_LOG
    const bool emit_match_log = strict_ack ||
                                (MATCH_LOG_STRIDE > 0u &&
                                 ((stats->accepted_words % MATCH_LOG_STRIDE) == 0u));
    if (emit_match_log) {
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
    printf("MATCH -> CH: %s | LABEL: 0x%02X (%s) | RAW: %lu | VAL: %.1f %s | SDI: %u | SSM: %u | SSM_TXT: %s | PARITY: %s\r\n",
           channel == CHANNEL_FWD ? "FWD" : "REV",
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
    }
#endif
}

int main(void) {
    stdio_init_all();
    sleep_ms(STARTUP_DELAY_MS);

    printf("ARINC_SNIFFER - captura pasiva por dos canales simplex\r\n");
    printf("Build: %s\r\n", SNIFFER_BUILD_TAG);
    printf("FWD RX: GP2=FWD_A, GP3=FWD_B\r\n");
    printf("REV RX: GP4=REV_A, GP5=REV_B\r\n");
    printf("SPI host: GP16=MOSI | GP17=CSn | GP18=SCK | GP19=MISO | GP20=DRDY\r\n");
    printf("SPI transport: RESET=0x%02X + request[%u] + response[%u] | IRQ=%s | TXDMA=%s | PIOFRAME=%s\r\n",
           SNIFFER_SPI_TRANSPORT_RESET,
           SNIFFER_SPI_PACKET_SIZE,
           SNIFFER_SPI_PACKET_SIZE,
           ENABLE_SPI_IRQ_TRANSPORT ? "ON" : "OFF",
           ENABLE_SPI_TX_DMA ? "ON" : "OFF",
           SNIFFER_SPI_PIO_FRAME_TRANSPORT ? "ON" : "OFF");
    printf("GP20/DRDY: %s\r\n",
           SNIFFER_SPI_DRDY_PURE ?
           "alto cuando hay snapshot nuevo; se limpia con GET_LATEST_META" :
           "heartbeat 1 Hz + pulso ante actividad SPI");
    printf("Bit rate objetivo: %u bps\r\n", BIT_RATE_HZ);
    printf("ACK observado: LABEL=0x%02X SDI=%u\r\n", ACK_LABEL, ACK_SDI);
    printf("Modo ARINC: %s\r\n", ARINC429_LOGIC_MODE ? "logic" : "legacy");
    printf("Filtro por whitelist: %s\r\n", ENABLE_FRAME_FILTER ? "ACTIVO" : "INACTIVO");
    printf("Cola de captura: %u palabras | snapshot slots: %u | cola SPI debug: %u eventos\r\n\r\n",
           CAPTURE_BUFFER_WORDS,
           LATEST_SLOT_CAPACITY,
           SPI_EVENT_QUEUE_DEPTH);

    PIO pio = pio0;
    const uint sm_fwd = 0;
    const uint sm_rev = 1;
#if ARINC429_LOGIC_MODE
    const uint offset = pio_add_program(pio, &arinc429_logic_rx_program);
#else
    const uint offset = pio_add_program(pio, &arinc_gpio_link_rx_program);
#endif
    rx_stats_t stats = {0};
    filter_state_t filter;
    event_queue_t event_queue = {0};
    latest_snapshot_state_t latest_snapshot;
    spi_link_state_t spi_link;
    uint32_t queue_write_index = 0u;
    uint32_t queue_read_index = 0u;
    uint32_t queued_words = 0u;
    uint32_t invalid_streak_fwd = 0u;
    uint32_t invalid_streak_rev = 0u;
    absolute_time_t last_valid_fwd_time = get_absolute_time();
    absolute_time_t last_valid_rev_time = get_absolute_time();

    filter_reset_to_defaults(&filter);
    event_queue_clear(&event_queue);
    latest_snapshot_clear(&latest_snapshot);
    spi_init_link(&spi_link);
    arinc_rx_program_init(pio, sm_fwd, offset, ARINC_FWD_PIN_BASE);
    arinc_rx_program_init(pio, sm_rev, offset, ARINC_REV_PIN_BASE);

    while (true) {
        bool drained_any = false;
        uint32_t printed_words = 0u;
        capture_entry_t entry;
        absolute_time_t now;

        spi_diag_gpio_service();
        service_spi_link(&spi_link, &stats, &event_queue, &filter, &latest_snapshot);

        enqueue_from_sm(pio,
                        sm_fwd,
                        CHANNEL_FWD,
                        &queue_write_index,
                        &queued_words,
                        &invalid_streak_fwd,
                        &last_valid_fwd_time,
                        &stats,
                        &filter,
                        &drained_any);
        enqueue_from_sm(pio,
                        sm_rev,
                        CHANNEL_REV,
                        &queue_write_index,
                        &queued_words,
                        &invalid_streak_rev,
                        &last_valid_rev_time,
                        &stats,
                        &filter,
                        &drained_any);

        now = get_absolute_time();

        if (queued_words == 0u &&
            absolute_time_diff_us(last_valid_fwd_time, now) >= (int64_t)FWD_STALL_RESYNC_US) {
            restart_rx_state_machine(pio, sm_fwd);
            invalid_streak_fwd = 0u;
            last_valid_fwd_time = now;
            note_channel_resync(CHANNEL_FWD, "timeout", &stats);
        }

        while (queued_words > 0u && printed_words < PRINT_BUDGET_PER_LOOP) {
            if (!capture_queue_pop(&entry, &queue_read_index, &queued_words)) {
                break;
            }

            print_decoded_record(entry.word,
                                 entry.channel,
                                 &stats,
                                 &filter,
                                 &event_queue,
                                 &latest_snapshot);
            ++printed_words;
            spi_diag_gpio_service();
            service_spi_link(&spi_link, &stats, &event_queue, &filter, &latest_snapshot);
        }

        if (!drained_any && printed_words == 0u) {
            spi_diag_gpio_service();
            tight_loop_contents();
        }
    }
}
