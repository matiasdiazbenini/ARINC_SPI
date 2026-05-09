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
 * Configuración RX
 * ============================================================
 */

#define RX_PIO pio0
#define RX_SM  1u

#define SAMPLES_PER_BIT      12u
#define RX_SAMPLE_PERIOD_US  (BIT_PERIOD_US / SAMPLES_PER_BIT)

#define PN_IDLE    0x00u
#define PN_HIGH    0x01u
#define PN_LOW     0x02u
#define PN_INVALID 0x03u

/*
 * Ventana grande para que el RT pueda capturar:
 *
 * TR=0:
 *   CMD + DATA
 *
 * TR=1:
 *   CMD
 */
#define CAPTURE_SAMPLES 8192u

/*
 * Subdirecciones válidas del RT.
 */
#define RT_SUB_DATA    2u
#define RT_SUB_STATUS  3u
#define RT_SUB_DIAG    4u

#define RT_TX_WORDS    3u

static uint32_t sample_word = 0;
static int sample_index = 16;
static bool rx_pio_initialized = false;

/*
 * ============================================================
 * Configuración TX
 * ============================================================
 */

static PIO bus_tx_pio = pio0;
static const uint bus_tx_sm = 0u;
static uint bus_tx_offset = 0u;
static bool bus_tx_pio_initialized = false;

/*
 * ============================================================
 * Prototipos internos
 * ============================================================
 */

static void rx_sampler_init(PIO pio, uint sm, uint pin_base);

static uint8_t get_sample_from_word(uint32_t raw, int index);
static uint8_t read_sample(void);
static void capture_samples(uint8_t *buffer, uint32_t count);

static uint8_t majority_range(const uint8_t *buffer, int start, int count);
static bool decode_bit_at_phase(const uint8_t *buffer, int start, bool *bit);
static bool decode_byte_at_phase(const uint8_t *buffer, int start, uint8_t *byte);

static bool find_any_word16_parity_near(const uint8_t *samples,
                                        int center,
                                        int radius,
                                        uint16_t *word,
                                        int *found_offset);

static void bus_tx_pio_init(void);
static void bus_pio_take_tx_pins(void);
static void bus_release_pins_to_sio(void);

static bool rt_is_valid_subaddress(uint8_t sub);
static bool rt_is_valid_rx_wc(uint8_t wc);
static bool rt_is_valid_tx_wc(uint8_t wc);

/*
 * ============================================================
 * RX helpers
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

static bool find_any_word16_parity_near(const uint8_t *samples,
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

            if (pos + word_samples >= CAPTURE_SAMPLES) {
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

static void rx_sampler_init(PIO pio, uint sm, uint pin_base) {
    if (rx_pio_initialized) {
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
    rx_sampler_init(RX_PIO, RX_SM, BUS_PIN_P);

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

void bus_send_bit(bool bit) {
    bus_tx_pio_init();

    if (gpio_get_function(BUS_PIN_P) != GPIO_FUNC_PIO0 ||
        gpio_get_function(BUS_PIN_N) != GPIO_FUNC_PIO0) {
        bus_pio_take_tx_pins();
    }

    pio_sm_set_enabled(bus_tx_pio, bus_tx_sm, true);

    const uint32_t tx_word = bit ? 0x80000000u : 0x00000000u;
    pio_sm_put_blocking(bus_tx_pio, bus_tx_sm, tx_word);
}

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

    /*
     * Mantenemos el comportamiento actual estable:
     * la paridad se transmite como un bit usando el PIO TX.
     */
    bus_send_bit(bus_compute_odd_parity(word) != 0u);
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

void bus_send_1553_status(uint8_t rt_addr, bool msg_error) {
    uint16_t status = BUS_1553_STATUS_MAKE(rt_addr, msg_error);

    bus_send_1553_word(BUS_1553_SYNC_CMD_STATUS, status);

    /*
     * Postámbulo neutro temporal.
     */
    bus_send_byte(0x00);
    bus_send_byte(0x00);
}

void bus_send_1553_data_word(uint16_t data) {
    bus_send_1553_word(BUS_1553_SYNC_DATA, data);
}

void bus_send_status_word_parity(uint8_t rt_addr,
                                 bool msg_error) {
    bus_send_1553_status(rt_addr, msg_error);
}

void bus_send_status_data_parity(uint8_t rt_addr,
                                 bool msg_error,
                                 const uint16_t data[],
                                 uint8_t wc) {
    uint16_t status = BUS_1553_STATUS_MAKE(rt_addr, msg_error);

    /*
     * Status Word.
     */
    bus_send_1553_word(BUS_1553_SYNC_CMD_STATUS, status);

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
 * RX: paquete BC -> RT, TR=0
 * ============================================================
 */

bool bus_read_packet_parity_pio(uint16_t *cmd,
                                uint16_t data[],
                                uint8_t max_wc,
                                uint8_t *out_wc) {
    rx_sampler_init(RX_PIO, RX_SM, BUS_PIN_P);

    uint8_t samples[CAPTURE_SAMPLES];
    capture_samples(samples, CAPTURE_SAMPLES);

    const int byte_samples = 8 * SAMPLES_PER_BIT;
    const int word_parity_samples = 24 * SAMPLES_PER_BIT;
    const int search_radius_cmd = 24;
    const int search_radius_sync = 48;
    const int search_radius_word = 12;

    for (int offset = 0;
         offset + (3 * byte_samples) < CAPTURE_SAMPLES;
         offset++) {

        uint8_t sync_cmd = 0;

        if (!decode_byte_at_phase(samples, offset, &sync_cmd)) {
            continue;
        }

        if (sync_cmd != SYNC_CMD_STATUS) {
            continue;
        }

        uint16_t rx_cmd = 0;
        int off_cmd = -1;

        if (!find_any_word16_parity_near(samples,
                                         offset + byte_samples,
                                         search_radius_cmd,
                                         &rx_cmd,
                                         &off_cmd)) {
            continue;
        }

        uint8_t rt = BUS_1553_CMD_RT(rx_cmd);
        uint8_t tr = BUS_1553_CMD_TR(rx_cmd);
        uint8_t wc = BUS_1553_CMD_WC(rx_cmd);

        if (rt == 0u || rt > 31u) {
            continue;
        }

        if (wc == 0u || wc > max_wc) {
            continue;
        }

        /*
         * Esta función es solo para TR=0: BC -> RT.
         */
        if (tr != BUS_1553_TR_BC_TO_RT) {
            continue;
        }

        uint16_t temp_data[BUS_1553_MAX_DATA_WORDS] = {0};
        bool data_ok = true;

        /*
         * Después del CMD + PARITY esperamos:
         *
         * 0F DATA0+P
         * 0F DATA1+P
         * ...
         *
         * Importante:
         * priorizamos el 0x0F más cercano al centro esperado,
         * no el primero que aparezca.
         */
        int next_sync_center = off_cmd + word_parity_samples;

        for (uint8_t i = 0; i < wc; i++) {
            bool found_sync_data = false;
            int off_sync_data = -1;

            for (int abs_delta = 0; abs_delta <= search_radius_sync; abs_delta++) {
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

                    if (pos + byte_samples >= CAPTURE_SAMPLES) {
                        continue;
                    }

                    uint8_t sync_data = 0;

                    if (!decode_byte_at_phase(samples, pos, &sync_data)) {
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

            if (!find_any_word16_parity_near(samples,
                                             off_sync_data + byte_samples,
                                             search_radius_word,
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

        if (cmd != NULL) {
            *cmd = rx_cmd;
        }

        if (data != NULL) {
            for (uint8_t i = 0; i < wc; i++) {
                data[i] = temp_data[i];
            }
        }

        if (out_wc != NULL) {
            *out_wc = wc;
        }

        return true;
    }

    return false;
}

/*
 * ============================================================
 * RX: command word, TR=1
 * ============================================================
 */

bool bus_read_command_word_parity_pio(uint16_t *cmd) {
    rx_sampler_init(RX_PIO, RX_SM, BUS_PIN_P);

    uint8_t samples[CAPTURE_SAMPLES];
    capture_samples(samples, CAPTURE_SAMPLES);

    const int byte_samples = 8 * SAMPLES_PER_BIT;
    const int search_radius = 24;

    /*
     * Formato:
     *
     * F0
     * CMD + PARITY
     */
    const int total_bytes = 1 + 3;

    for (int offset = 0;
         offset + (total_bytes * byte_samples) < CAPTURE_SAMPLES;
         offset++) {

        uint8_t sync = 0;

        if (!decode_byte_at_phase(samples, offset, &sync)) {
            continue;
        }

        if (sync != SYNC_CMD_STATUS) {
            continue;
        }

        uint16_t rx_cmd = 0;
        int off_cmd = -1;

        if (!find_any_word16_parity_near(samples,
                                         offset + byte_samples,
                                         search_radius,
                                         &rx_cmd,
                                         &off_cmd)) {
            continue;
        }

        uint8_t rt = BUS_1553_CMD_RT(rx_cmd);
        uint8_t wc = BUS_1553_CMD_WC(rx_cmd);

        if (rt == 0u || rt > 31u) {
            continue;
        }

        if (wc == 0u || wc > BUS_1553_MAX_DATA_WORDS) {
            continue;
        }

        if (cmd != NULL) {
            *cmd = rx_cmd;
        }

        return true;
    }

    return false;
}

/*
 * ============================================================
 * Validaciones RT
 * ============================================================
 */

static bool rt_is_valid_subaddress(uint8_t sub) {
    switch (sub) {
        case RT_SUB_DATA:
        case RT_SUB_STATUS:
        case RT_SUB_DIAG:
            return true;

        default:
            return false;
    }
}

static bool rt_is_valid_rx_wc(uint8_t wc) {
    if (wc == 0u) {
        return false;
    }

    if (wc > BUS_1553_MAX_DATA_WORDS) {
        return false;
    }

    return true;
}

static bool rt_is_valid_tx_wc(uint8_t wc) {
    if (wc == 0u) {
        return false;
    }

    /*
     * Por ahora el RT tiene solo 3 palabras disponibles para transmitir.
     */
    if (wc > RT_TX_WORDS) {
        return false;
    }

    return true;
}

/*
 * ============================================================
 * API de alto nivel del Remote Terminal
 * ============================================================
 */

bool rt_process_once(uint8_t my_rt_addr) {
    /*
     * Datos que el RT entrega cuando el BC pide datos con TR=1.
     * Más adelante esto puede reemplazarse por sensores, registros,
     * memoria de subdirecciones, etc.
     */
    static const uint16_t rt_tx_data[RT_TX_WORDS] = {
        0x1111,
        0x2222,
        0x3333
    };

    /*
     * ============================================================
     * CASO 1:
     * TR=0 → BC transmite datos al RT.
     * ============================================================
     */
    uint16_t cmd = 0;
    uint16_t rx_data[BUS_1553_MAX_DATA_WORDS] = {0};
    uint8_t wc = 0;

    if (bus_read_packet_parity_pio(&cmd,
                                   rx_data,
                                   BUS_1553_MAX_DATA_WORDS,
                                   &wc)) {
        uint8_t rt  = BUS_1553_CMD_RT(cmd);
        uint8_t tr  = BUS_1553_CMD_TR(cmd);
        uint8_t sub = BUS_1553_CMD_SUB(cmd);

        if (rt == my_rt_addr && tr == BUS_1553_TR_BC_TO_RT) {
            bool msg_error = false;

            if (!rt_is_valid_subaddress(sub)) {
                msg_error = true;
            }

            if (!rt_is_valid_rx_wc(wc)) {
                msg_error = true;
            }

            /*
             * Validación temporal para prueba TR=0.
             * Mientras estemos probando con estos datos fijos, si llegan corruptos
             * marcamos MSG_ERROR=1.
             */
            if (sub == RT_SUB_DATA && wc == 3u) {
                if (rx_data[0] != 0x1234u ||
                    rx_data[1] != 0xABCDu ||
                    rx_data[2] != 0x55AAu) {
                    msg_error = true;
                }
            }

            printf("RT_RX_BC_TO_RT|CMD=0x%04X|RT=%u|TR=%u|SUB=%u|WC=%u",
                   cmd,
                   rt,
                   tr,
                   sub,
                   wc);

            for (uint8_t i = 0; i < wc; i++) {
                printf("|D%u=0x%04X", i, rx_data[i]);
            }

            printf("|MSG_ERROR=%u\n", msg_error ? 1u : 0u);

            /*
             * Guarda actual estable para que el BC pase a RX.
             */
            sleep_us(3000);

            bus_set_tx_mode();

            bus_send_status_word_parity(my_rt_addr, msg_error);

            sleep_us(30 * BIT_PERIOD_US);

            bus_idle();
            bus_set_rx_mode();

            printf("RT_TX_STATUS|RT=%u|MSG_ERROR=%u\n",
                   my_rt_addr,
                   msg_error ? 1u : 0u);

            return true;
        }
    }

    /*
     * ============================================================
     * CASO 2:
     * TR=1 → BC solicita datos al RT.
     * ============================================================
     */
    cmd = 0;

    if (bus_read_command_word_parity_pio(&cmd)) {
        uint8_t rt  = BUS_1553_CMD_RT(cmd);
        uint8_t tr  = BUS_1553_CMD_TR(cmd);
        uint8_t sub = BUS_1553_CMD_SUB(cmd);
        wc          = BUS_1553_CMD_WC(cmd);

        if (rt == my_rt_addr && tr == BUS_1553_TR_RT_TO_BC) {
            bool msg_error = false;

            if (!rt_is_valid_subaddress(sub)) {
                msg_error = true;
            }

            if (!rt_is_valid_tx_wc(wc)) {
                msg_error = true;
            }

            printf("RT_RX_RT_TO_BC_REQ|CMD=0x%04X|RT=%u|TR=%u|SUB=%u|WC=%u|MSG_ERROR=%u\n",
                   cmd,
                   rt,
                   tr,
                   sub,
                   wc,
                   msg_error ? 1u : 0u);

            /*
             * Guarda actual estable para que el BC pase a RX.
             */
            sleep_us(3000);

            bus_set_tx_mode();

            if (msg_error) {
                bus_send_status_word_parity(my_rt_addr, true);

                sleep_us(30 * BIT_PERIOD_US);

                bus_idle();
                bus_set_rx_mode();

                printf("RT_TX_STATUS|RT=%u|MSG_ERROR=1\n", my_rt_addr);
            } else {
                bus_send_status_data_parity(my_rt_addr,
                                            false,
                                            rt_tx_data,
                                            wc);

                sleep_us(30 * BIT_PERIOD_US);

                bus_idle();
                bus_set_rx_mode();

                printf("RT_TX_STATUS_DATA|RT=%u|WC=%u",
                       my_rt_addr,
                       wc);

                for (uint8_t i = 0; i < wc; i++) {
                    printf("|D%u=0x%04X", i, rt_tx_data[i]);
                }

                printf("\n");
            }

            return true;
        }
    }

    return false;
}