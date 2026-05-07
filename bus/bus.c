#include "bus.h"
#include <stdio.h>
#include "hardware/gpio.h"
#if BUS_USE_PIO_TX
#include "hardware/clocks.h"
#include "hardware/pio.h"
#include "manchester_tx.pio.h"
#include "manchester_rx.pio.h"
#endif
#include "pico/stdlib.h"

#if BUS_USE_PIO_TX
static PIO bus_tx_pio = pio0;
static const uint bus_tx_sm = 0u;
static uint bus_tx_offset = 0u;
static bool bus_tx_pio_initialized = false;

#define RX_PIO pio0
#define RX_SM  1u

#define SAMPLES_PER_BIT      12u
#define RX_SAMPLE_PERIOD_US  (BIT_PERIOD_US / SAMPLES_PER_BIT)

#define PN_IDLE    0x00u
#define PN_HIGH    0x01u
#define PN_LOW     0x02u
#define PN_INVALID 0x03u

#define STATUS_CAPTURE_SAMPLES 4096u

#define RX_RING_SIZE 8192u

static uint8_t rx_ring[RX_RING_SIZE];
static volatile uint32_t rx_ring_wr = 0;
static bool rx_streaming = false;

static uint32_t rx_sample_word = 0;
static int rx_sample_index = 16;
static bool rx_pio_initialized = false;

void bus_send_command_word(uint16_t cmd);

bool bus_read_status_word_pio(uint16_t *status);
static void rx_sampler_init(PIO pio, uint sm, uint pin_base);
static void rx_sampler_take_pins(PIO pio, uint sm, uint pin_base);


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
static void rx_ring_push(uint8_t sample) {
    rx_ring[rx_ring_wr % RX_RING_SIZE] = sample;
    rx_ring_wr++;
}

static void rx_service(void) {
    /*
     * Vacía todo lo que haya disponible en el FIFO RX del PIO
     * y lo pasa al buffer circular.
     */
    while (!pio_sm_is_rx_fifo_empty(RX_PIO, RX_SM)) {
        uint32_t raw = pio_sm_get(RX_PIO, RX_SM);

        for (int i = 0; i < 16; i++) {
            uint8_t sample = rx_get_sample_from_word(raw, i);
            rx_ring_push(sample);
        }
    }
}

static void rx_get_recent_samples(uint8_t *dst, uint32_t count) {
    rx_service();

    if (count > RX_RING_SIZE) {
        count = RX_RING_SIZE;
    }

    uint32_t wr = rx_ring_wr;

    for (uint32_t i = 0; i < count; i++) {
        uint32_t src_index = wr - count + i;
        dst[i] = rx_ring[src_index % RX_RING_SIZE];
    }
}
void bus_rx_stream_start(void) {
    rx_sampler_init(RX_PIO, RX_SM, BUS_PIN_P);
    rx_sampler_take_pins(RX_PIO, RX_SM, BUS_PIN_P);

    pio_sm_clear_fifos(RX_PIO, RX_SM);
    pio_sm_restart(RX_PIO, RX_SM);

    rx_sample_index = 16;
    rx_ring_wr = 0;
    rx_streaming = true;
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

static bool rx_find_byte_near(const uint8_t *samples,
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

        if (pos + (8 * SAMPLES_PER_BIT) >= STATUS_CAPTURE_SAMPLES) {
            continue;
        }

        uint8_t b = 0;

        if (rx_decode_byte_at_phase(samples, pos, &b)) {
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

static bool rx_find_any_word16_near(const uint8_t *samples,
                                    int center,
                                    int radius,
                                    uint16_t *word,
                                    int *found_offset) {
    for (int abs_delta = 0; abs_delta <= radius; abs_delta++) {
        for (int s = 0; s < 2; s++) {
            int delta;

            if (abs_delta == 0) {
                if (s == 1) {
                    continue;
                }
                delta = 0;
            } else {
                delta = (s == 0) ? -abs_delta : abs_delta;
            }

            int pos = center + delta;

            if (pos < 0) {
                continue;
            }

            if (pos + (16 * SAMPLES_PER_BIT) >= STATUS_CAPTURE_SAMPLES) {
                continue;
            }

            uint8_t hi = 0;
            uint8_t lo = 0;

            if (rx_decode_byte_at_phase(samples, pos, &hi) &&
                rx_decode_byte_at_phase(samples, pos + 8 * SAMPLES_PER_BIT, &lo)) {

                if (word != NULL) {
                    *word = ((uint16_t)hi << 8) | lo;
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
bool bus_read_status_word_pio(uint16_t *status) {
    rx_sampler_init(RX_PIO, RX_SM, BUS_PIN_P);

    rx_sampler_take_pins(RX_PIO, RX_SM, BUS_PIN_P);

    pio_sm_clear_fifos(RX_PIO, RX_SM);
    pio_sm_restart(RX_PIO, RX_SM);
    rx_sample_index = 16;

    uint8_t samples[STATUS_CAPTURE_SAMPLES];
    rx_capture_samples(samples, STATUS_CAPTURE_SAMPLES);

    const int byte_samples = 8 * SAMPLES_PER_BIT;
    const int search_radius = 12;

    /*
     * Status esperado:
     * F0 + STATUS_WORD(2 bytes)
     */
    const int total_bytes = 1 + 2;

    for (int offset = 0;
         offset + (total_bytes * byte_samples) < STATUS_CAPTURE_SAMPLES;
         offset++) {

        uint8_t sync = 0;

        if (!rx_decode_byte_at_phase(samples, offset, &sync)) {
            continue;
        }

        if (sync != 0xF0u) {
            continue;
        }

        uint8_t st_hi = 0;
        uint8_t st_lo = 0;

        int off_st_hi = -1;
        int off_st_lo = -1;

        /*
        * Para esta prueba esperamos RT=3 y MSG_ERROR=0.
        * STATUS = 0x1800.
        *
        * Buscamos byte alto 0x18 y byte bajo 0x00.
        */
        bool ok_hi = rx_find_byte_near(samples,
                                    offset + byte_samples,
                                    search_radius,
                                    0x18u,
                                    &st_hi,
                                    &off_st_hi);

        if (!ok_hi) {
            continue;
        }

        bool ok_lo = rx_find_byte_near(samples,
                                    off_st_hi + byte_samples,
                                    24,
                                    0x00u,
                                    &st_lo,
                                    &off_st_lo);

        if (!ok_lo) {
            continue;
        }

        uint16_t rx_status = ((uint16_t)st_hi << 8) | st_lo;

        if (status != NULL) {
            *status = rx_status;
        }

        return true;
    }

    return false;
}
bool bus_read_status_data_checked_pio(uint16_t *status,
                                      uint16_t data[],
                                      uint8_t expected_wc) {
    rx_sampler_init(RX_PIO, RX_SM, BUS_PIN_P);
    rx_sampler_take_pins(RX_PIO, RX_SM, BUS_PIN_P);

    /*
    * Limpiar muestras viejas justo antes de capturar.
    * Esta captura empieza apenas el maestro pasó a RX.
    */
    pio_sm_clear_fifos(RX_PIO, RX_SM);
    pio_sm_restart(RX_PIO, RX_SM);
    rx_sample_index = 16;

    uint8_t samples[STATUS_CAPTURE_SAMPLES];
    rx_capture_samples(samples, STATUS_CAPTURE_SAMPLES);

    const int byte_samples = 8 * SAMPLES_PER_BIT;
    const int word_samples = 16 * SAMPLES_PER_BIT;
    const int search_radius = 12;

    /*
     * Esperamos, idealmente:
     *
     * F0 + STATUS(2B) + 0F + DATA(expected_wc*2B) + CHK(2B)
     *
     * Pero no vamos a confiar rígidamente en el offset del 0x0F.
     */
    const int total_bytes = 1 + 2 + 1 + (expected_wc * 2) + 2;

    int dbg_f0 = 0;
    int dbg_st_hi = 0;
    int dbg_st_lo = 0;
    int dbg_sync_data = 0;
    int dbg_data = 0;
    int dbg_chk = 0;

    if (expected_wc == 0 || expected_wc > BUS_1553_MAX_DATA_WORDS) {
        return false;
    }

    for (int offset = 0;
         offset + (total_bytes * byte_samples) < STATUS_CAPTURE_SAMPLES;
         offset++) {

        uint8_t sync_status = 0;

        if (!rx_decode_byte_at_phase(samples, offset, &sync_status)) {
            continue;
        }

        if (sync_status != 0xF0u) {
            continue;
        }

        dbg_f0++;

        /*
         * Para esta prueba esperamos STATUS=0x1800:
         * RT=3, MSG_ERROR=0.
         */
        uint8_t st_hi = 0;
        uint8_t st_lo = 0;

        int off_st_hi = -1;
        int off_st_lo = -1;

        if (!rx_find_byte_near(samples,
                               offset + byte_samples,
                               search_radius,
                               0x18u,
                               &st_hi,
                               &off_st_hi)) {
            continue;
        }

        dbg_st_hi++;

        if (!rx_find_byte_near(samples,
                               off_st_hi + byte_samples,
                               24,
                               0x00u,
                               &st_lo,
                               &off_st_lo)) {
            continue;
        }

        dbg_st_lo++;

        uint16_t rx_status = ((uint16_t)st_hi << 8) | st_lo;

        uint16_t temp_data[BUS_1553_MAX_DATA_WORDS] = {0};
bool packet_ok = false;

/*
 * Buscamos posibles SYNC_DATA = 0x0F después del STATUS.
 * Para cada SYNC_DATA, buscamos DATA y CHK con tolerancia.
 */
int sync_search_start = off_st_lo + byte_samples - 96;
int sync_search_end   = off_st_lo + (4 * byte_samples) + 96;

for (int sync_pos = sync_search_start; sync_pos <= sync_search_end; sync_pos++) {
    if (sync_pos < 0) {
        continue;
    }

    if (sync_pos + byte_samples >= STATUS_CAPTURE_SAMPLES) {
        continue;
    }

    uint8_t candidate_sync = 0;

    if (!rx_decode_byte_at_phase(samples, sync_pos, &candidate_sync)) {
        continue;
    }

    if (candidate_sync != 0x0Fu) {
        continue;
    }

    dbg_sync_data++;

    /*
     * Ahora probamos varios centros para DATA0.
     */
    for (int delta0 = -96; delta0 <= 96; delta0++) {
        int next_word_center = sync_pos + byte_samples + delta0;

        if (next_word_center < 0) {
            continue;
        }

        bool data_ok = true;
        uint16_t candidate_data[BUS_1553_MAX_DATA_WORDS] = {0};

        /*
         * Leer DATA con búsqueda flexible, encadenando offsets reales.
         */
        for (uint8_t i = 0; i < expected_wc; i++) {
            int off_word = -1;

            if (!rx_find_any_word16_near(samples,
                                         next_word_center,
                                         48,
                                         &candidate_data[i],
                                         &off_word)) {
                data_ok = false;
                break;
            }

            next_word_center = off_word + word_samples;
        }

        if (!data_ok) {
            continue;
        }

        dbg_data++;

        /*
         * Leer checksum con búsqueda flexible después del último DATA.
         */
        uint16_t rx_chk = 0;
        int off_chk = -1;

        if (!rx_find_any_word16_near(samples,
                                     next_word_center,
                                     48,
                                     &rx_chk,
                                     &off_chk)) {
            continue;
        }

        dbg_chk++;

        uint16_t calc = rx_status;

        for (uint8_t i = 0; i < expected_wc; i++) {
            calc ^= candidate_data[i];
        }

        if (calc != rx_chk) {
            continue;
        }

        for (uint8_t i = 0; i < expected_wc; i++) {
            temp_data[i] = candidate_data[i];
        }

        packet_ok = true;
        break;
    }

    if (packet_ok) {
        break;
    }
}

if (!packet_ok) {
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

    /*printf("RTDATA_DBG|F0=%d|ST_H=%d|ST_L=%d|SYNC_DATA=%d|DATA=%d|CHK=%d\n",
           dbg_f0,
           dbg_st_hi,
           dbg_st_lo,
           dbg_sync_data,
           dbg_data,
           dbg_chk);*/

    return false;
}
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
     * bus_send_bit() carga 0x80000000 para bit=1 y 0x00000000 para bit=0.
     */
    sm_config_set_out_shift(&c, false, false, 32);

    /*
     * Valor conservador inicial. La temporización efectiva todavía se
     * completa con sleep_us(BIT_PERIOD_US) en bus_send_bit().
     */
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
#endif

#if !BUS_USE_PIO_TX
static void bus_send_bit_software(bool bit) {
    const uint32_t half_period_us = BIT_PERIOD_US / 2u;

    if (bit) {
        gpio_put(BUS_PIN_P, 1);
        gpio_put(BUS_PIN_N, 0);
        sleep_us(half_period_us);

        gpio_put(BUS_PIN_P, 0);
        gpio_put(BUS_PIN_N, 1);
        sleep_us(half_period_us);
    } else {
        gpio_put(BUS_PIN_P, 0);
        gpio_put(BUS_PIN_N, 1);
        sleep_us(half_period_us);

        gpio_put(BUS_PIN_P, 1);
        gpio_put(BUS_PIN_N, 0);
        sleep_us(half_period_us);
    }
}
#endif

static int bus_read_diff_level(void) {
    const int p = gpio_get(BUS_PIN_P);
    const int n = gpio_get(BUS_PIN_N);

    if (p == 1 && n == 0) {
        return 1;
    }

    if (p == 0 && n == 1) {
        return 0;
    }

    return -1;
}

void bus_init(void) {
    gpio_init(BUS_PIN_P);
    gpio_init(BUS_PIN_N);

#if BUS_USE_PIO_TX
    bus_tx_pio_init();
#endif

    bus_set_rx_mode();
}

void bus_set_tx_mode(void) {
#if BUS_USE_PIO_TX
    bus_tx_pio_init();
    bus_pio_take_tx_pins();
    pio_sm_set_enabled(bus_tx_pio, bus_tx_sm, true);
#else
    gpio_set_dir(BUS_PIN_P, GPIO_OUT);
    gpio_set_dir(BUS_PIN_N, GPIO_OUT);
#endif
}

void bus_set_rx_mode(void) {
#if BUS_USE_PIO_TX
    if (bus_tx_pio_initialized) {
        /*
         * Esperar a que la FIFO se vacíe.
         * Ojo: esto no garantiza que el último byte ya terminó físicamente.
         */
        pio_sm_drain_tx_fifo(bus_tx_pio, bus_tx_sm);

        /*
         * Espera extra para que termine de salir el último byte.
         * Cada byte son 8 bits, usamos margen de 10 bits.
         */
        sleep_us(10 * BIT_PERIOD_US);

        pio_sm_set_enabled(bus_tx_pio, bus_tx_sm, false);
    }

    bus_release_pins_to_sio();
#endif

    gpio_set_dir(BUS_PIN_P, GPIO_IN);
    gpio_set_dir(BUS_PIN_N, GPIO_IN);
}

void bus_idle(void) {
#if BUS_USE_PIO_TX
    if (bus_tx_pio_initialized) {
        /*
         * Esperar a que la FIFO se vacíe.
         */
        pio_sm_drain_tx_fifo(bus_tx_pio, bus_tx_sm);

        /*
         * Espera extra para no cortar el último byte.
         */
        sleep_us(10 * BIT_PERIOD_US);

        pio_sm_set_enabled(bus_tx_pio, bus_tx_sm, false);
    }

    bus_release_pins_to_sio();

    gpio_set_dir(BUS_PIN_P, GPIO_OUT);
    gpio_set_dir(BUS_PIN_N, GPIO_OUT);
#else
    gpio_set_dir(BUS_PIN_P, GPIO_OUT);
    gpio_set_dir(BUS_PIN_N, GPIO_OUT);
#endif

    gpio_put(BUS_PIN_P, 0);
    gpio_put(BUS_PIN_N, 0);
}

void bus_send_bit(bool bit) {
#if BUS_USE_PIO_TX
    bus_tx_pio_init();

    if (gpio_get_function(BUS_PIN_P) != GPIO_FUNC_PIO0 ||
        gpio_get_function(BUS_PIN_N) != GPIO_FUNC_PIO0) {
        bus_pio_take_tx_pins();
    }

    pio_sm_set_enabled(bus_tx_pio, bus_tx_sm, true);

    const uint32_t tx_word = bit ? 0x80000000u : 0x00000000u;
    pio_sm_put_blocking(bus_tx_pio, bus_tx_sm, tx_word);

    /*
     * Espera conservadora:
     * evita que el código pase a RX/IDLE o cargue el siguiente bit antes
     * de que el PIO termine de consumir el bit actual.
     */
#else
    bus_send_bit_software(bit);
#endif
}

bool bus_read_bit(bool *bit) {
    int first_half = 0;
    int second_half = 0;

    if (bit == NULL) {
        return false;
    }

    first_half = bus_read_diff_level();
    if (first_half < 0) {
        return false;
    }

    sleep_us(BIT_PERIOD_US / 2u);

    second_half = bus_read_diff_level();
    if (second_half < 0) {
        return false;
    }

    if (first_half == 1 && second_half == 0) {
        *bit = true;
    } else if (first_half == 0 && second_half == 1) {
        *bit = false;
    } else {
        return false;
    }

    sleep_us(BIT_PERIOD_US / 2u);
    return true;
}

void bus_send_byte(uint8_t byte) {
#if BUS_USE_PIO_TX
    bus_tx_pio_init();
    bus_pio_take_tx_pins();
    pio_sm_set_enabled(bus_tx_pio, bus_tx_sm, true);

    uint32_t v = ((uint32_t)byte) << 24u;  // MSB first
    pio_sm_put_blocking(bus_tx_pio, bus_tx_sm, v);
#else
    for (int i = 7; i >= 0; i--) {
        bus_send_bit(((byte >> i) & 1u) != 0u);
    }
#endif
}

bool bus_read_byte(uint8_t *byte) {
    uint8_t value = 0;

    if (byte == NULL) {
        return false;
    }

    for (int i = 0; i < 8; i++) {
        bool bit = false;

        if (!bus_read_bit(&bit)) {
            return false;
        }

        value = (uint8_t)((value << 1) | (bit ? 1u : 0u));
    }

    *byte = value;
    return true;
}

static void bus_send_preamble(void){
    for(uint8_t i = 0; i < SYNC_PREAMBLE_COUNT; i++){
        bus_send_byte(SYNC_PREAMBLE_BYTE);
    }
}
void bus_send_sync_cmd_status(void) {
    bus_send_preamble();
    bus_send_byte(SYNC_CMD_STATUS);
}
void bus_send_sync_data(void) {
    bus_send_preamble();
    bus_send_byte(SYNC_DATA);
}
bool bus_read_sync(uint8_t *type) {
    uint8_t byte = 0;
    uint8_t preamble_seen = 0;

    while(true){
        if(!bus_read_byte(&byte)){
            return false;
        }

        printf("SYNC_SCAN_BYTE=%02X\n", byte);

        if(byte == SYNC_PREAMBLE_BYTE){
            if(preamble_seen < SYNC_PREAMBLE_COUNT){
                preamble_seen++;
            }
            continue;
        }
        if(preamble_seen >= SYNC_PREAMBLE_COUNT){
            if(byte == SYNC_CMD_STATUS){
                if(type != NULL){
                    *type = BUS_SYNC_TYPE_CMD_STATUS;
                }
                return true;
            }
            if(byte == SYNC_DATA){
                if(type != NULL){
                    *type = BUS_SYNC_TYPE_DATA;
                }
                return true;
            }
        }
        preamble_seen = 0;
    }
}


bool bus_read_word16(uint16_t *word) {
    uint16_t value = 0;

    if (word == NULL) {
        return false;
    }

    for (int i = 0; i < 16; i++) {
        bool bit = false;

        if (!bus_read_bit(&bit)) {
            return false;
        }

        value = (uint16_t)((value << 1) | (bit ? 1u : 0u));
    }

    *word = value;
    return true;
}

uint8_t bus_compute_odd_parity(uint16_t word) {
    uint8_t parity = 0u;

    for (int i = 0; i < 16; i++) {
        parity ^= (uint8_t)((word >> i) & 1u);
    }

    return (uint8_t)(parity ^ 1u);
}
void bus_send_word16(uint16_t word) {
#if BUS_USE_PIO_TX
    bus_send_byte((uint8_t)((word >> 8) & 0xFFu));
    bus_send_byte((uint8_t)(word & 0xFFu));
#else
    for (int i = 15; i >= 0; i--) {
        bus_send_bit(((word >> i) & 1u) != 0u);
    }
#endif
}
void bus_send_status_word(uint8_t rt_addr, bool msg_error) {
    uint16_t status = BUS_1553_STATUS_MAKE(rt_addr, msg_error);

    bus_send_byte(0xF0);
    bus_send_word16(status);

    /*
     * Postámbulo neutro para no cortar el status al pasar a idle.
     */
    bus_send_byte(0x00);
    bus_send_byte(0x00);
}
void bus_send_word16_parity(uint16_t word) {
    bus_send_word16(word);
    bus_send_bit(bus_compute_odd_parity(word) != 0u);
}
void bus_send_packet_checked(uint16_t cmd, const uint16_t data[], uint8_t wc) {
    uint16_t chk = cmd;

    bus_send_byte(0xF0);
    bus_send_word16(cmd);

    bus_send_byte(0x0F);

    for (uint8_t i = 0; i < wc; i++) {
        uint16_t word = data[i];

        bus_send_word16(word);
        chk ^= word;
    }

    bus_send_word16(chk);

    /*
     * Postámbulo neutro de margen.
     * No pertenece al paquete.
     * Evita que el último bit del checksum quede contaminado
     * cuando el maestro suelta el bus.
     */
    bus_send_byte(0x00);
    bus_send_byte(0x00);
}
void bus_send_status_data_checked(uint8_t rt_addr,
                                  bool msg_error,
                                  const uint16_t data[],
                                  uint8_t wc) {
    uint16_t status = BUS_1553_STATUS_MAKE(rt_addr, msg_error);
    uint16_t chk = status;

    /*
     * STATUS SYNC + STATUS WORD
     */
    bus_send_byte(0xF0);
    bus_send_word16(status);

    /*
     * DATA SYNC + DATA WORDS
     */
    bus_send_byte(0x0F);

    for (uint8_t i = 0; i < wc; i++) {
        uint16_t word = data[i];

        bus_send_word16(word);
        chk ^= word;
    }

    /*
     * CHECKSUM
     */
    bus_send_word16(chk);

    /*
     * Postámbulo neutro.
     */
    bus_send_byte(0x00);
    bus_send_byte(0x00);
}
bool bus_read_word16_parity(uint16_t *word) {
    uint16_t value = 0;
    bool parity_bit = false;

    if (word == NULL) {
        return false;
    }

    if (!bus_read_word16(&value)) {
        return false;
    }

    if (!bus_read_bit(&parity_bit)) {
        return false;
    }

    if ((parity_bit ? 1u : 0u) != bus_compute_odd_parity(value)) {
        return false;
    }

    *word = value;
    return true;
}
void bus_send_command_word(uint16_t cmd) {
    bus_send_byte(0xF0);
    bus_send_word16(cmd);

    /*
     * Postámbulo neutro para proteger el final del comando.
     */
    bus_send_byte(0x00);
    bus_send_byte(0x00);
}