#include "bus.h"

#include <stdio.h>

#include "pico/stdlib.h"
#include "hardware/gpio.h"
#include "hardware/clocks.h"
#include "hardware/pio.h"

#include "manchester_tx.pio.h"
#include "manchester_rx.pio.h"

/*
 * ============================================================
 * Configuración PIO
 * ============================================================
 */

static PIO bus_tx_pio = pio0;
static const uint bus_tx_sm = 0u;
static uint bus_tx_offset = 0u;
static bool bus_tx_pio_initialized = false;

#define RX_PIO pio0
#define RX_SM  1u

#define SAMPLES_PER_BIT      10u
#define RX_SAMPLE_PERIOD_US  (BIT_PERIOD_US / SAMPLES_PER_BIT)

#define PN_IDLE    0x00u
#define PN_HIGH    0x01u
#define PN_LOW     0x02u
#define PN_INVALID 0x03u

/*
 * Ventana larga para STATUS + DATA en TR=1.
 */
#define STATUS_CAPTURE_SAMPLES 4096u

/*
 * Ventana corta para STATUS solo en TR=0.
 */
#define BC_STATUS_CAPTURE_SAMPLES 1024u

static uint32_t rx_sample_word = 0;
static int rx_sample_index = 16;
static bool rx_pio_initialized = false;

/*
 * ============================================================
 * Prototipos internos
 * ============================================================
 */

static void bus_tx_pio_init(void);
static void bus_pio_take_tx_pins(void);
static void bus_release_pins_to_sio(void);

static void rx_sampler_init(PIO pio, uint sm, uint pin_base);
static void rx_sampler_take_pins(PIO pio, uint sm, uint pin_base);

static uint8_t rx_get_sample_from_word(uint32_t raw, int index);
static uint8_t rx_read_sample(void);
static void rx_capture_samples(uint8_t *buffer, uint32_t count);

static uint8_t rx_majority_range(const uint8_t *buffer, int start, int count);
static bool rx_decode_bit_at_phase(const uint8_t *buffer, int start, bool *bit);
static bool rx_decode_byte_at_phase(const uint8_t *buffer, int start, uint8_t *byte);

static bool rx_find_any_word16_parity_near_limited(const uint8_t *samples,
                                                   int sample_count,
                                                   int center,
                                                   int radius,
                                                   uint16_t *word,
                                                   int *found_offset);

/*
 * ============================================================
 * RX PIO helpers
 * ============================================================
 */

static uint8_t rx_get_sample_from_word(uint32_t raw, int index) {
    const int shift = 30 - (index * 2);
    return (uint8_t)((raw >> shift) & 0x03u);
}

static uint8_t rx_read_sample(void) {
    if (rx_sample_index >= 16) {
        rx_sample_word = pio_sm_get_blocking(RX_PIO, RX_SM);
        rx_sample_index = 0;
    }

    uint8_t sample = rx_get_sample_from_word(rx_sample_word, rx_sample_index);
    rx_sample_index++;

    return sample;
}

static void rx_capture_samples(uint8_t *buffer, uint32_t count) {
    for (uint32_t i = 0; i < count; i++) {
        buffer[i] = rx_read_sample();
    }
}

static uint8_t rx_majority_range(const uint8_t *buffer, int start, int count) {
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

static bool rx_decode_bit_at_phase(const uint8_t *buffer, int start, bool *bit) {
    const int half = SAMPLES_PER_BIT / 2;

    if (bit == NULL) {
        return false;
    }

    uint8_t first = rx_majority_range(buffer, start, half);
    uint8_t second = rx_majority_range(buffer, start + half, half);

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

static bool rx_decode_byte_at_phase(const uint8_t *buffer, int start, uint8_t *byte) {
    uint8_t value = 0;

    if (byte == NULL) {
        return false;
    }

    for (int b = 0; b < 8; b++) {
        bool bit = false;
        int bit_start = start + b * SAMPLES_PER_BIT;

        if (!rx_decode_bit_at_phase(buffer, bit_start, &bit)) {
            return false;
        }

        value = (uint8_t)((value << 1) | (bit ? 1u : 0u));
    }

    *byte = value;
    return true;
}

static bool rx_find_any_word16_parity_near_limited(const uint8_t *samples,
                                                   int sample_count,
                                                   int center,
                                                   int radius,
                                                   uint16_t *word,
                                                   int *found_offset) {
    /*
     * En esta versión, una palabra transmitida ocupa:
     *
     * 16 bits útiles + ranura de paridad equivalente a 8 bits = 24 bits.
     *
     * La paridad real se toma en el primer bit después de los 16 bits.
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

            if (!rx_decode_byte_at_phase(samples, pos, &hi)) {
                continue;
            }

            if (!rx_decode_byte_at_phase(samples,
                                         pos + 8 * SAMPLES_PER_BIT,
                                         &lo)) {
                continue;
            }

            if (!rx_decode_bit_at_phase(samples,
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
 * TX PIO helpers
 * ============================================================
 */

static void bus_pio_take_tx_pins(void) {
    gpio_set_function(BUS_PIN_P, GPIO_FUNC_PIO0);
    gpio_set_function(BUS_PIN_N, GPIO_FUNC_PIO0);
    pio_sm_set_consecutive_pindirs(bus_tx_pio, bus_tx_sm, BUS_PIN_P, 2u, true);
}

static void bus_release_pins_to_sio(void) {
    gpio_set_function(BUS_PIN_P, GPIO_FUNC_SIO);
    gpio_set_function(BUS_PIN_N, GPIO_FUNC_SIO);
}

static void bus_tx_pio_init(void) {
    if (bus_tx_pio_initialized) {
        return;
    }

    bus_tx_offset = pio_add_program(bus_tx_pio, &manchester_tx_program);

    pio_sm_config c = manchester_tx_program_get_default_config(bus_tx_offset);

    sm_config_set_sideset_pins(&c, BUS_PIN_P);

    /*
     * MSB first.
     */
    sm_config_set_out_shift(&c, false, false, 32);

    const float pio_cycles_per_bit = 16.0f;
    const float clkdiv =
        ((float)clock_get_hz(clk_sys) * ((float)BIT_PERIOD_US / 1000000.0f)) /
        pio_cycles_per_bit;

    sm_config_set_clkdiv(&c, clkdiv);

    pio_gpio_init(bus_tx_pio, BUS_PIN_P);
    pio_gpio_init(bus_tx_pio, BUS_PIN_N);

    bus_pio_take_tx_pins();

    pio_sm_init(bus_tx_pio, bus_tx_sm, bus_tx_offset, &c);
    pio_sm_set_enabled(bus_tx_pio, bus_tx_sm, false);

    bus_tx_pio_initialized = true;
}

/*
 * ============================================================
 * Inicialización y modos
 * ============================================================
 */

void bus_init(void) {
    gpio_init(BUS_PIN_P);
    gpio_init(BUS_PIN_N);

    bus_tx_pio_init();

    bus_set_rx_mode();
}

void bus_set_tx_mode(void) {
    bus_tx_pio_init();
    bus_pio_take_tx_pins();
    pio_sm_set_enabled(bus_tx_pio, bus_tx_sm, true);
}

void bus_set_rx_mode(void) {
    if (bus_tx_pio_initialized) {
        pio_sm_drain_tx_fifo(bus_tx_pio, bus_tx_sm);

        /*
         * Margen estable para asegurar que terminó el último byte.
         */
        sleep_us(10 * BIT_PERIOD_US);

        pio_sm_set_enabled(bus_tx_pio, bus_tx_sm, false);
    }

    bus_release_pins_to_sio();

    gpio_set_dir(BUS_PIN_P, GPIO_IN);
    gpio_set_dir(BUS_PIN_N, GPIO_IN);
}

void bus_idle(void) {
    if (bus_tx_pio_initialized) {
        pio_sm_drain_tx_fifo(bus_tx_pio, bus_tx_sm);

        /*
         * Margen estable para no cortar el último byte.
         */
        sleep_us(10 * BIT_PERIOD_US);

        pio_sm_set_enabled(bus_tx_pio, bus_tx_sm, false);
    }

    bus_release_pins_to_sio();

    gpio_set_dir(BUS_PIN_P, GPIO_OUT);
    gpio_set_dir(BUS_PIN_N, GPIO_OUT);

    gpio_put(BUS_PIN_P, 0);
    gpio_put(BUS_PIN_N, 0);
}

/*
 * ============================================================
 * TX básico
 * ============================================================
 */

void bus_send_byte(uint8_t byte) {
    bus_tx_pio_init();
    bus_pio_take_tx_pins();
    pio_sm_set_enabled(bus_tx_pio, bus_tx_sm, true);

    uint32_t v = ((uint32_t)byte) << 24u;
    pio_sm_put_blocking(bus_tx_pio, bus_tx_sm, v);
}

void bus_send_word16(uint16_t word) {
    bus_send_byte((uint8_t)((word >> 8) & 0xFFu));
    bus_send_byte((uint8_t)(word & 0xFFu));
}

uint8_t bus_compute_odd_parity(uint16_t word) {
    uint8_t parity = 0u;

    for (int i = 0; i < 16; i++) {
        parity ^= (uint8_t)((word >> i) & 1u);
    }

    return (uint8_t)(parity ^ 1u);
}

void bus_send_word16_parity(uint16_t word) {
    bus_send_word16(word);
    bus_send_byte(bus_compute_odd_parity(word) ? 0xFFu : 0x00u);
}

/*
 * ============================================================
 * TX 1553-like con paridad
 * ============================================================
 */

void bus_send_1553_word(bus_1553_sync_t sync_type, uint16_t word) {
    if (sync_type == BUS_1553_SYNC_CMD_STATUS) {
        bus_send_byte(SYNC_CMD_STATUS);
    } else {
        bus_send_byte(SYNC_DATA);
    }

    bus_send_word16_parity(word);
}

void bus_send_1553_command(uint16_t cmd) {
    bus_send_1553_word(BUS_1553_SYNC_CMD_STATUS, cmd);

    /*
     * Postámbulo neutro temporal.
     */
    bus_send_byte(0x00);
    bus_send_byte(0x00);
}

void bus_send_1553_status(uint8_t rt_addr, bool msg_error) {
    uint16_t status = BUS_1553_STATUS_MAKE(rt_addr, msg_error);

    bus_send_1553_word(BUS_1553_SYNC_CMD_STATUS, status);

    bus_send_byte(0x00);
    bus_send_byte(0x00);
}

void bus_send_1553_data_word(uint16_t data) {
    bus_send_1553_word(BUS_1553_SYNC_DATA, data);
}

void bus_send_command_word_parity(uint16_t cmd) {
    bus_send_1553_command(cmd);
}

void bus_send_packet_parity(uint16_t cmd,
                            const uint16_t data[],
                            uint8_t wc) {
    /*
     * Command Word.
     */
    bus_send_1553_word(BUS_1553_SYNC_CMD_STATUS, cmd);

    /*
     * Data Words.
     */
    for (uint8_t i = 0; i < wc; i++) {
        bus_send_1553_word(BUS_1553_SYNC_DATA, data[i]);
    }

    /*
     * Postámbulo neutro temporal.
     */
    bus_send_byte(0x00);
    bus_send_byte(0x00);
}

/*
 * ============================================================
 * RX: STATUS solamente
 * ============================================================
 */

bool bus_read_status_word_parity_pio(uint16_t *status) {
    rx_sampler_init(RX_PIO, RX_SM, BUS_PIN_P);
    rx_sampler_take_pins(RX_PIO, RX_SM, BUS_PIN_P);

    pio_sm_clear_fifos(RX_PIO, RX_SM);
    pio_sm_restart(RX_PIO, RX_SM);
    rx_sample_index = 16;

    uint8_t samples[BC_STATUS_CAPTURE_SAMPLES];
    rx_capture_samples(samples, BC_STATUS_CAPTURE_SAMPLES);

    const int byte_samples = 8 * SAMPLES_PER_BIT;
    const int search_radius = 24;

    /*
     * Formato:
     *
     * F0
     * STATUS + PARITY
     */
    const int total_bytes = 1 + 3;

    for (int offset = 0;
         offset + (total_bytes * byte_samples) < BC_STATUS_CAPTURE_SAMPLES;
         offset++) {

        uint8_t sync = 0;

        if (!rx_decode_byte_at_phase(samples, offset, &sync)) {
            continue;
        }

        if (sync != SYNC_CMD_STATUS) {
            continue;
        }

        uint16_t rx_status = 0;
        int off_status = -1;

        if (!rx_find_any_word16_parity_near_limited(samples,
                                                    BC_STATUS_CAPTURE_SAMPLES,
                                                    offset + byte_samples,
                                                    search_radius,
                                                    &rx_status,
                                                    &off_status)) {
            continue;
        }

        uint8_t rt = BUS_1553_STATUS_RT(rx_status);

        if (rt == 0u || rt > 31u) {
            continue;
        }

        if (status != NULL) {
            *status = rx_status;
        }

        return true;
    }

    return false;
}

/*
 * ============================================================
 * RX: STATUS + DATA
 * ============================================================
 */

bool bus_read_status_data_parity_pio(uint16_t *status,
                                     uint16_t data[],
                                     uint8_t expected_wc) {
    rx_sampler_init(RX_PIO, RX_SM, BUS_PIN_P);
    rx_sampler_take_pins(RX_PIO, RX_SM, BUS_PIN_P);

    pio_sm_clear_fifos(RX_PIO, RX_SM);
    pio_sm_restart(RX_PIO, RX_SM);
    rx_sample_index = 16;

    uint8_t samples[STATUS_CAPTURE_SAMPLES];
    rx_capture_samples(samples, STATUS_CAPTURE_SAMPLES);

    const int byte_samples = 8 * SAMPLES_PER_BIT;
    const int word_parity_samples = 24 * SAMPLES_PER_BIT;
    const int search_radius = 24;

    if (expected_wc == 0u || expected_wc > BUS_1553_MAX_DATA_WORDS) {
        return false;
    }

    /*
     * Formato:
     *
     * F0
     * STATUS + PARITY
     *
     * 0F
     * DATA0 + PARITY
     *
     * 0F
     * DATA1 + PARITY
     *
     * ...
     */
    const int total_bytes = 1 + 3 + (expected_wc * (1 + 3));

    for (int offset = 0;
         offset + (total_bytes * byte_samples) < STATUS_CAPTURE_SAMPLES;
         offset++) {

        uint8_t sync_status = 0;

        if (!rx_decode_byte_at_phase(samples, offset, &sync_status)) {
            continue;
        }

        if (sync_status != SYNC_CMD_STATUS) {
            continue;
        }

        uint16_t rx_status = 0;
        int off_status = -1;

        if (!rx_find_any_word16_parity_near_limited(samples,
                                                    STATUS_CAPTURE_SAMPLES,
                                                    offset + byte_samples,
                                                    search_radius,
                                                    &rx_status,
                                                    &off_status)) {
            continue;
        }

        uint8_t rt = BUS_1553_STATUS_RT(rx_status);

        if (rt == 0u || rt > 31u) {
            continue;
        }

        /*
         * Si el RT responde MSG_ERROR=1, puede venir solo STATUS.
         */
        if (BUS_1553_STATUS_MSG_ERROR(rx_status)) {
            if (status != NULL) {
                *status = rx_status;
            }

            if (data != NULL) {
                for (uint8_t i = 0; i < expected_wc; i++) {
                    data[i] = 0;
                }
            }

            return true;
        }

        uint16_t temp_data[BUS_1553_MAX_DATA_WORDS] = {0};
        bool data_ok = true;

        int next_sync_center = off_status + word_parity_samples;

        for (uint8_t i = 0; i < expected_wc; i++) {
            bool found_sync_data = false;
            int off_sync_data = -1;

            /*
             * Buscar SYNC_DATA centrado.
             */
            for (int abs_delta = 0; abs_delta <= 96; abs_delta++) {
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

                    int pos = next_sync_center + delta;

                    if (pos < 0) {
                        continue;
                    }

                    if (pos + byte_samples >= STATUS_CAPTURE_SAMPLES) {
                        continue;
                    }

                    uint8_t sync_data = 0;

                    if (!rx_decode_byte_at_phase(samples, pos, &sync_data)) {
                        continue;
                    }

                    if (sync_data == SYNC_DATA) {
                        found_sync_data = true;
                        off_sync_data = pos;
                        break;
                    }
                }

                if (found_sync_data) {
                    break;
                }
            }

            if (!found_sync_data) {
                data_ok = false;
                break;
            }

            int off_word = -1;

            if (!rx_find_any_word16_parity_near_limited(samples,
                                                        STATUS_CAPTURE_SAMPLES,
                                                        off_sync_data + byte_samples,
                                                        search_radius,
                                                        &temp_data[i],
                                                        &off_word)) {
                data_ok = false;
                break;
            }

            next_sync_center = off_word + word_parity_samples;
        }

        if (!data_ok) {
            continue;
        }

        if (status != NULL) {
            *status = rx_status;
        }

        if (data != NULL) {
            for (uint8_t i = 0; i < expected_wc; i++) {
                data[i] = temp_data[i];
            }
        }

        return true;
    }

    return false;
}

/*
 * ============================================================
 * API de alto nivel del Bus Controller
 * ============================================================
 */

bool bc_send_to_rt(uint8_t rt_addr,
                   uint8_t subaddr,
                   const uint16_t data[],
                   uint8_t wc,
                   uint16_t *out_status) {
    if (data == NULL) {
        return false;
    }

    if (wc == 0u || wc > BUS_1553_MAX_DATA_WORDS) {
        return false;
    }

    uint16_t cmd = BUS_1553_CMD_MAKE(
        rt_addr,
        BUS_1553_TR_BC_TO_RT,
        subaddr,
        wc
    );

    uint32_t t0 = time_us_32();

    /*
     * Como el RT todavía escucha por ventanas bloqueantes,
     * puede perder una transmisión. Por eso reintentamos
     * transmitiendo de nuevo el paquete completo.
     */
    for (int tx_attempt = 0; tx_attempt < 8; tx_attempt++) {
        bus_set_tx_mode();

        bus_send_packet_parity(cmd, data, wc);

        /*
         * Margen actual estable para no cortar físicamente el final
         * del paquete.
         */
        sleep_us(50 * BIT_PERIOD_US);

        /*
         * No usamos bus_idle() acá.
         */
        bus_set_rx_mode();

        uint16_t status = 0;

        /*
         * Pocos intentos de lectura por cada transmisión.
         * Si no llega STATUS, retransmitimos el paquete completo.
         */
        for (int rx_attempt = 0; rx_attempt < 3; rx_attempt++) {
            if (bus_read_status_word_parity_pio(&status)) {
                uint32_t t1 = time_us_32();

                if (out_status != NULL) {
                    *out_status = status;
                }

                printf("BC_SEND_TO_RT_TIME_US=%lu|TX_ATTEMPT=%d|RX_ATTEMPT=%d\n",
                       (unsigned long)(t1 - t0),
                       tx_attempt,
                       rx_attempt);

                return true;
            }

            sleep_ms(5);
        }

        /*
        * Pausa variable para no quedar sincronizado siempre
        * con la ventana bloqueante del RT.
        */
        sleep_ms(7 + (tx_attempt * 13));
    }

    uint32_t t1 = time_us_32();

    printf("BC_SEND_TO_RT_TIMEOUT_US=%lu\n",
           (unsigned long)(t1 - t0));

    return false;
}

bool bc_request_from_rt(uint8_t rt_addr,
                        uint8_t subaddr,
                        uint16_t data[],
                        uint8_t wc,
                        uint16_t *out_status) {
    if (data == NULL) {
        return false;
    }

    if (wc == 0u || wc > BUS_1553_MAX_DATA_WORDS) {
        return false;
    }

    uint16_t cmd = BUS_1553_CMD_MAKE(
        rt_addr,
        BUS_1553_TR_RT_TO_BC,
        subaddr,
        wc
    );

    uint32_t t0 = time_us_32();

    bus_set_tx_mode();

    bus_send_command_word_parity(cmd);

    /*
     * Versión estable actual.
     */
    sleep_us(50 * BIT_PERIOD_US);

    bus_idle();
    bus_set_rx_mode();

    uint16_t status = 0;

    /*
     * Caso normal:
     * RT responde STATUS + DATA.
     */
    for (int attempt = 0; attempt < 20; attempt++) {
        if (bus_read_status_data_parity_pio(&status, data, wc)) {
            uint32_t t1 = time_us_32();

            if (out_status != NULL) {
                *out_status = status;
            }

            printf("BC_REQUEST_FROM_RT_TIME_US=%lu|MODE=STATUS_DATA|ATTEMPT=%d\n",
                   (unsigned long)(t1 - t0),
                   attempt);

            return true;
        }

        sleep_us(500);
    }

    /*
     * Caso de error:
     * RT puede responder solo STATUS con MSG_ERROR=1.
     */
    for (int attempt = 0; attempt < 10; attempt++) {
        if (bus_read_status_word_parity_pio(&status)) {
            uint32_t t1 = time_us_32();

            if (out_status != NULL) {
                *out_status = status;
            }

            printf("BC_REQUEST_FROM_RT_TIME_US=%lu|MODE=STATUS_ONLY|ATTEMPT=%d\n",
                   (unsigned long)(t1 - t0),
                   attempt);

            return true;
        }

        sleep_ms(10);
    }

    uint32_t t1 = time_us_32();

    printf("BC_REQUEST_FROM_RT_TIMEOUT_US=%lu\n",
           (unsigned long)(t1 - t0));

    return false;
}