#include "bus.h"

#include <stdio.h>
#include <string.h>

#include "pico/stdlib.h"
#include "hardware/gpio.h"
#include "hardware/clocks.h"
#include "hardware/pio.h"

#include "manchester_rx.pio.h"

/*
 * ============================================================
 * Configuración RX
 * ============================================================
 */

#define RX_PIO pio0
#define RX_SM  1u

/*
 * Base estable actual:
 *
 * BIT_PERIOD_US = 500 us
 * SAMPLES_PER_BIT = 10
 *
 * Entonces:
 * RX_SAMPLE_PERIOD_US = 50 us
 */
#define SAMPLES_PER_BIT      10u
#define RX_SAMPLE_PERIOD_US  (BIT_PERIOD_US / SAMPLES_PER_BIT)

#define PN_IDLE    0x00u
#define PN_HIGH    0x01u
#define PN_LOW     0x02u
#define PN_INVALID 0x03u

/*
 * Para el sniffer conviene una ventana más larga que en el RT,
 * porque queremos capturar la transacción completa:
 *
 * TR=0:
 *   CMD + DATA + STATUS
 *
 * TR=1:
 *   CMD + STATUS + DATA
 *
 * Con 8192 muestras:
 *   8192 * 50 us ≈ 409 ms
 */
#define SNIFFER_CAPTURE_SAMPLES 8192u

static uint32_t sample_word = 0;
static int sample_index = 16;
static bool rx_pio_initialized = false;

/*
 * ============================================================
 * Prototipos internos
 * ============================================================
 */

static void rx_sampler_init(PIO pio, uint sm, uint pin_base);
static void rx_sampler_take_pins(PIO pio, uint sm, uint pin_base);

static uint8_t get_sample_from_word(uint32_t raw, int index);
static uint8_t read_sample(void);
static void capture_samples(uint8_t *buffer, uint32_t count);

static uint8_t majority_range(const uint8_t *buffer, int start, int count);
static bool decode_bit_at_phase(const uint8_t *buffer, int start, bool *bit);
static bool decode_byte_at_phase(const uint8_t *buffer, int start, uint8_t *byte);

static uint8_t bus_compute_odd_parity(uint16_t word);

static bool find_any_word16_parity_near(const uint8_t *samples,
                                        int sample_count,
                                        int center,
                                        int radius,
                                        uint16_t *word,
                                        int *found_offset);

static bool find_sync_byte_near(const uint8_t *samples,
                                int sample_count,
                                int center,
                                int radius,
                                uint8_t sync_value,
                                int *found_offset);

static bool find_sync_byte_forward(const uint8_t *samples,
                                   int sample_count,
                                   int start,
                                   int end,
                                   uint8_t sync_value,
                                   int *found_offset);

static bool decode_status_at_sync(const uint8_t *samples,
                                  int sample_count,
                                  int off_sync,
                                  uint16_t *status,
                                  int *off_status_word);

static bool decode_data_words_after(const uint8_t *samples,
                                    int sample_count,
                                    int start_sync_center,
                                    uint8_t wc,
                                    uint16_t data[],
                                    int *next_center_after_last_word);

static bool parse_bc_to_rt_event(const uint8_t *samples,
                                 int sample_count,
                                 int off_cmd_sync,
                                 uint16_t cmd,
                                 int off_cmd_word,
                                 sniffer_event_t *event);

static bool parse_rt_to_bc_event(const uint8_t *samples,
                                 int sample_count,
                                 int off_cmd_sync,
                                 uint16_t cmd,
                                 int off_cmd_word,
                                 sniffer_event_t *event);

/*
 * ============================================================
 * RX PIO helpers
 * ============================================================
 */

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

    if (bit == NULL) {
        return false;
    }

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

/*
 * ============================================================
 * PIO init
 * ============================================================
 */

static void rx_sampler_take_pins(PIO pio, uint sm, uint pin_base) {
    gpio_set_function(pin_base, GPIO_FUNC_PIO0);
    gpio_set_function(pin_base + 1u, GPIO_FUNC_PIO0);

    pio_sm_set_consecutive_pindirs(pio, sm, pin_base, 2, false);
    pio_sm_set_enabled(pio, sm, true);
}

static void rx_sampler_init(PIO pio, uint sm, uint pin_base) {
    if (rx_pio_initialized) {
        rx_sampler_take_pins(pio, sm, pin_base);
        return;
    }

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

    rx_pio_initialized = true;

    rx_sampler_take_pins(pio, sm, pin_base);
}

/*
 * ============================================================
 * Inicialización pública
 * ============================================================
 */

void bus_init(void) {
    gpio_init(BUS_PIN_P);
    gpio_init(BUS_PIN_N);

    rx_sampler_init(RX_PIO, RX_SM, BUS_PIN_P);
    rx_sampler_take_pins(RX_PIO, RX_SM, BUS_PIN_P);
}

/*
 * ============================================================
 * Decodificación de palabras con paridad
 * ============================================================
 */

static uint8_t bus_compute_odd_parity(uint16_t word) {
    uint8_t parity = 0u;

    for (int i = 0; i < 16; i++) {
        parity ^= (uint8_t)((word >> i) & 1u);
    }

    return (uint8_t)(parity ^ 1u);
}

static bool find_any_word16_parity_near(const uint8_t *samples,
                                        int sample_count,
                                        int center,
                                        int radius,
                                        uint16_t *word,
                                        int *found_offset) {
    /*
     * Palabra esperada:
     *
     * 16 bits útiles + paridad.
     *
     * En la práctica venimos usando una ranura equivalente de 24 bits
     * para sincronizar el siguiente elemento, tomando la paridad real en
     * el primer bit después de los 16 bits.
     */
    const int word_bits = 24;
    const int word_samples = word_bits * SAMPLES_PER_BIT;

    for (int abs_delta = 0; abs_delta <= radius; abs_delta++) {
        for (int side = 0; side < 2; side++) {
            int delta;

            if (abs_delta == 0) {
                if (side == 1) {
                    continue;
                }

                delta = 0;
            } else {
                delta = (side == 0) ? -abs_delta : abs_delta;
            }

            int pos = center + delta;

            if (pos < 0) {
                continue;
            }

            if (pos + word_samples >= sample_count) {
                continue;
            }

            uint8_t hi = 0;
            uint8_t lo = 0;
            bool parity_bit = false;

            if (!decode_byte_at_phase(samples, pos, &hi)) {
                continue;
            }

            if (!decode_byte_at_phase(samples,
                                      pos + 8 * SAMPLES_PER_BIT,
                                      &lo)) {
                continue;
            }

            if (!decode_bit_at_phase(samples,
                                     pos + 16 * SAMPLES_PER_BIT,
                                     &parity_bit)) {
                continue;
            }

            uint16_t w = ((uint16_t)hi << 8) | lo;

            uint8_t expected_parity = bus_compute_odd_parity(w);
            uint8_t rx_parity = parity_bit ? 1u : 0u;

            if (rx_parity != expected_parity) {
                continue;
            }

            if (word != NULL) {
                *word = w;
            }

            if (found_offset != NULL) {
                *found_offset = pos;
            }

            return true;
        }
    }

    return false;
}

static bool find_sync_byte_near(const uint8_t *samples,
                                int sample_count,
                                int center,
                                int radius,
                                uint8_t sync_value,
                                int *found_offset) {
    const int byte_samples = 8 * SAMPLES_PER_BIT;

    for (int abs_delta = 0; abs_delta <= radius; abs_delta++) {
        for (int side = 0; side < 2; side++) {
            int delta;

            if (abs_delta == 0) {
                if (side == 1) {
                    continue;
                }

                delta = 0;
            } else {
                delta = (side == 0) ? -abs_delta : abs_delta;
            }

            int pos = center + delta;

            if (pos < 0) {
                continue;
            }

            if (pos + byte_samples >= sample_count) {
                continue;
            }

            uint8_t b = 0;

            if (!decode_byte_at_phase(samples, pos, &b)) {
                continue;
            }

            if (b == sync_value) {
                if (found_offset != NULL) {
                    *found_offset = pos;
                }

                return true;
            }
        }
    }

    return false;
}

static bool find_sync_byte_forward(const uint8_t *samples,
                                   int sample_count,
                                   int start,
                                   int end,
                                   uint8_t sync_value,
                                   int *found_offset) {
    const int byte_samples = 8 * SAMPLES_PER_BIT;

    if (start < 0) {
        start = 0;
    }

    if (end > sample_count - byte_samples) {
        end = sample_count - byte_samples;
    }

    for (int pos = start; pos < end; pos++) {
        uint8_t b = 0;

        if (!decode_byte_at_phase(samples, pos, &b)) {
            continue;
        }

        if (b == sync_value) {
            if (found_offset != NULL) {
                *found_offset = pos;
            }

            return true;
        }
    }

    return false;
}

static bool decode_status_at_sync(const uint8_t *samples,
                                  int sample_count,
                                  int off_sync,
                                  uint16_t *status,
                                  int *off_status_word) {
    const int byte_samples = 8 * SAMPLES_PER_BIT;
    const int search_radius = 24;

    return find_any_word16_parity_near(samples,
                                       sample_count,
                                       off_sync + byte_samples,
                                       search_radius,
                                       status,
                                       off_status_word);
}

static bool decode_data_words_after(const uint8_t *samples,
                                    int sample_count,
                                    int start_sync_center,
                                    uint8_t wc,
                                    uint16_t data[],
                                    int *next_center_after_last_word) {
    const int byte_samples = 8 * SAMPLES_PER_BIT;
    const int word_parity_samples = 24 * SAMPLES_PER_BIT;

    const int search_radius_sync = 64;
    const int search_radius_word = 24;

    int next_sync_center = start_sync_center;

    for (uint8_t i = 0; i < wc; i++) {
        int off_sync_data = -1;

        if (!find_sync_byte_near(samples,
                                 sample_count,
                                 next_sync_center,
                                 search_radius_sync,
                                 SYNC_DATA,
                                 &off_sync_data)) {
            return false;
        }

        int off_word = -1;

        if (!find_any_word16_parity_near(samples,
                                         sample_count,
                                         off_sync_data + byte_samples,
                                         search_radius_word,
                                         &data[i],
                                         &off_word)) {
            return false;
        }

        next_sync_center = off_word + word_parity_samples;
    }

    if (next_center_after_last_word != NULL) {
        *next_center_after_last_word = next_sync_center;
    }

    return true;
}

/*
 * ============================================================
 * Parseo de eventos
 * ============================================================
 */

static bool parse_bc_to_rt_event(const uint8_t *samples,
                                 int sample_count,
                                 int off_cmd_sync,
                                 uint16_t cmd,
                                 int off_cmd_word,
                                 sniffer_event_t *event) {
    const int byte_samples = 8 * SAMPLES_PER_BIT;
    const int word_parity_samples = 24 * SAMPLES_PER_BIT;

    uint8_t rt  = BUS_1553_CMD_RT(cmd);
    uint8_t tr  = BUS_1553_CMD_TR(cmd);
    uint8_t sub = BUS_1553_CMD_SUB(cmd);
    uint8_t wc  = BUS_1553_CMD_WC(cmd);

    if (event == NULL) {
        return false;
    }

    if (tr != BUS_1553_TR_BC_TO_RT) {
        return false;
    }

    if (wc == 0u || wc > BUS_1553_MAX_DATA_WORDS) {
        return false;
    }

    uint16_t temp_data[BUS_1553_MAX_DATA_WORDS] = {0};
    int next_center = off_cmd_word + word_parity_samples;

    if (!decode_data_words_after(samples,
                                 sample_count,
                                 next_center,
                                 wc,
                                 temp_data,
                                 &next_center)) {
        return false;
    }

    /*
     * Después de los DATA, buscamos el STATUS del RT.
     * Hay una guarda temporal, así que no lo buscamos solo cerca:
     * buscamos hacia adelante.
     */
    int off_status_sync = -1;

    if (!find_sync_byte_forward(samples,
                                sample_count,
                                next_center,
                                sample_count,
                                SYNC_CMD_STATUS,
                                &off_status_sync)) {
        return false;
    }

    uint16_t status = 0;
    int off_status_word = -1;

    if (!decode_status_at_sync(samples,
                               sample_count,
                               off_status_sync,
                               &status,
                               &off_status_word)) {
        return false;
    }

    memset(event, 0, sizeof(*event));

    event->type = SNIFFER_EVENT_BC_TO_RT;
    event->timestamp_us = time_us_32();

    event->cmd = cmd;
    event->status = status;

    event->rt = rt;
    event->tr = tr;
    event->sub = sub;
    event->wc = wc;

    event->data_count = wc;
    event->msg_error = BUS_1553_STATUS_MSG_ERROR(status);

    for (uint8_t i = 0; i < wc; i++) {
        event->data[i] = temp_data[i];
    }

    (void)off_cmd_sync;
    (void)byte_samples;
    (void)off_status_word;

    return true;
}

static bool parse_rt_to_bc_event(const uint8_t *samples,
                                 int sample_count,
                                 int off_cmd_sync,
                                 uint16_t cmd,
                                 int off_cmd_word,
                                 sniffer_event_t *event) {
    const int word_parity_samples = 24 * SAMPLES_PER_BIT;

    uint8_t rt  = BUS_1553_CMD_RT(cmd);
    uint8_t tr  = BUS_1553_CMD_TR(cmd);
    uint8_t sub = BUS_1553_CMD_SUB(cmd);
    uint8_t wc  = BUS_1553_CMD_WC(cmd);

    if (event == NULL) {
        return false;
    }

    if (tr != BUS_1553_TR_RT_TO_BC) {
        return false;
    }

    if (wc == 0u || wc > BUS_1553_MAX_DATA_WORDS) {
        return false;
    }

    /*
     * Después del CMD buscamos STATUS hacia adelante,
     * porque hay guarda entre BC y RT.
     */
    int off_status_sync = -1;
    int min_status_search = off_cmd_word + word_parity_samples;

    if (!find_sync_byte_forward(samples,
                                sample_count,
                                min_status_search,
                                sample_count,
                                SYNC_CMD_STATUS,
                                &off_status_sync)) {
        return false;
    }

    uint16_t status = 0;
    int off_status_word = -1;

    if (!decode_status_at_sync(samples,
                               sample_count,
                               off_status_sync,
                               &status,
                               &off_status_word)) {
        return false;
    }

    memset(event, 0, sizeof(*event));

    event->type = SNIFFER_EVENT_RT_TO_BC;
    event->timestamp_us = time_us_32();

    event->cmd = cmd;
    event->status = status;

    event->rt = rt;
    event->tr = tr;
    event->sub = sub;
    event->wc = wc;

    event->msg_error = BUS_1553_STATUS_MSG_ERROR(status);

    /*
     * Si MSG_ERROR=1, el RT responde solo STATUS.
     */
    if (event->msg_error) {
        event->data_count = 0u;
        (void)off_cmd_sync;
        return true;
    }

    uint16_t temp_data[BUS_1553_MAX_DATA_WORDS] = {0};
    int next_center = off_status_word + word_parity_samples;

    if (!decode_data_words_after(samples,
                                 sample_count,
                                 next_center,
                                 wc,
                                 temp_data,
                                 &next_center)) {
        return false;
    }

    event->data_count = wc;

    for (uint8_t i = 0; i < wc; i++) {
        event->data[i] = temp_data[i];
    }

    (void)off_cmd_sync;

    return true;
}

/*
 * ============================================================
 * API pública del sniffer
 * ============================================================
 */

bool sniffer_capture_event_data(sniffer_event_t *event) {
    if (event == NULL) {
        return false;
    }

    rx_sampler_init(RX_PIO, RX_SM, BUS_PIN_P);
    rx_sampler_take_pins(RX_PIO, RX_SM, BUS_PIN_P);

    /*
     * Captura limpia.
     */
    pio_sm_clear_fifos(RX_PIO, RX_SM);
    pio_sm_restart(RX_PIO, RX_SM);
    sample_index = 16;

    uint8_t samples[SNIFFER_CAPTURE_SAMPLES];
    capture_samples(samples, SNIFFER_CAPTURE_SAMPLES);

    const int byte_samples = 8 * SAMPLES_PER_BIT;
    const int search_radius_cmd = 24;

    /*
     * Buscamos un CMD:
     *
     * F0
     * CMD + PARITY
     *
     * Luego, según TR, parseamos el resto.
     */
    const int min_total_samples = (1 + 3) * byte_samples;

    for (int offset = 0;
         offset + min_total_samples < SNIFFER_CAPTURE_SAMPLES;
         offset++) {

        uint8_t sync = 0;

        if (!decode_byte_at_phase(samples, offset, &sync)) {
            continue;
        }

        if (sync != SYNC_CMD_STATUS) {
            continue;
        }

        uint16_t cmd = 0;
        int off_cmd_word = -1;

        if (!find_any_word16_parity_near(samples,
                                         SNIFFER_CAPTURE_SAMPLES,
                                         offset + byte_samples,
                                         search_radius_cmd,
                                         &cmd,
                                         &off_cmd_word)) {
            continue;
        }

        uint8_t rt = BUS_1553_CMD_RT(cmd);
        uint8_t tr = BUS_1553_CMD_TR(cmd);
        uint8_t wc = BUS_1553_CMD_WC(cmd);

        if (rt == 0u || rt > 31u) {
            continue;
        }

        if (wc == 0u || wc > BUS_1553_MAX_DATA_WORDS) {
            continue;
        }

        if (tr == BUS_1553_TR_BC_TO_RT) {
            if (parse_bc_to_rt_event(samples,
                                     SNIFFER_CAPTURE_SAMPLES,
                                     offset,
                                     cmd,
                                     off_cmd_word,
                                     event)) {
                return true;
            }
        } else {
            if (parse_rt_to_bc_event(samples,
                                     SNIFFER_CAPTURE_SAMPLES,
                                     offset,
                                     cmd,
                                     off_cmd_word,
                                     event)) {
                return true;
            }
        }
    }

    return false;
}

void sniffer_print_event(const sniffer_event_t *event) {
    if (event == NULL) {
        return;
    }

    if (event->type == SNIFFER_EVENT_BC_TO_RT) {
        printf("SNIFF|TYPE=BC_TO_RT|T=%lu|CMD=0x%04X|RT=%u|TR=%u|SUB=%u|WC=%u",
               (unsigned long)event->timestamp_us,
               event->cmd,
               event->rt,
               event->tr,
               event->sub,
               event->wc);

        for (uint8_t i = 0; i < event->data_count; i++) {
            printf("|D%u=0x%04X", i, event->data[i]);
        }

        printf("|STATUS=0x%04X|MSG_ERROR=%u\n",
               event->status,
               event->msg_error);

        return;
    }

    if (event->type == SNIFFER_EVENT_RT_TO_BC) {
        printf("SNIFF|TYPE=RT_TO_BC|T=%lu|CMD=0x%04X|RT=%u|TR=%u|SUB=%u|WC=%u|STATUS=0x%04X|MSG_ERROR=%u",
               (unsigned long)event->timestamp_us,
               event->cmd,
               event->rt,
               event->tr,
               event->sub,
               event->wc,
               event->status,
               event->msg_error);

        for (uint8_t i = 0; i < event->data_count; i++) {
            printf("|D%u=0x%04X", i, event->data[i]);
        }

        printf("\n");

        return;
    }
}
void sniffer_resync(void) {
    rx_sampler_init(RX_PIO, RX_SM, BUS_PIN_P);
    rx_sampler_take_pins(RX_PIO, RX_SM, BUS_PIN_P);

    pio_sm_clear_fifos(RX_PIO, RX_SM);
    pio_sm_restart(RX_PIO, RX_SM);
    sample_index = 16;
}