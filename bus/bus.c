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

#define RX_PIO pio0
#define RX_SM  1u

#define SAMPLES_PER_BIT      12u
#define RX_SAMPLE_PERIOD_US  (BIT_PERIOD_US / SAMPLES_PER_BIT)

#define PN_IDLE    0x00u
#define PN_HIGH    0x01u
#define PN_LOW     0x02u
#define PN_INVALID 0x03u

#define CAPTURE_SAMPLES 8192u

static uint32_t sample_word = 0;
static int sample_index = 16;

static bool rx_pio_initialized = false;
static void rx_sampler_init(PIO pio, uint sm, uint pin_base);
bool bus_read_command_word_pio(uint16_t *cmd);
static bool find_any_word16_near(const uint8_t *samples,
                                 int center,
                                 int radius,
                                 uint16_t *word,
                                 int *found_offset);
static bool find_word16_near(const uint8_t *samples,
                             int center,
                             int radius,
                             uint16_t target,
                             uint16_t *found_word,
                             int *found_offset);

void bus_send_command_word(uint16_t cmd);

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
static bool find_any_byte_near(const uint8_t *samples,
                               int center,
                               int radius,
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
            if (found_byte != NULL) {
                *found_byte = b;
            }

            if (found_offset != NULL) {
                *found_offset = pos;
            }

            return true;
        }
    }

    return false;
}
static bool bus_pio_read_frame_any(uint16_t *cmd, uint16_t data[], uint8_t wc) {
    uint8_t samples[CAPTURE_SAMPLES];

    capture_samples(samples, CAPTURE_SAMPLES);

    const int byte_samples = 8 * SAMPLES_PER_BIT;
    const int search_radius = 20;

    const int total_bytes = 1 + 2 + (wc * 2); // SYNC + CMD + DATA

    for (int offset = 0;
         offset + (total_bytes * byte_samples) < CAPTURE_SAMPLES;
         offset++) {

        uint8_t sync = 0;

        if (!decode_byte_at_phase(samples, offset, &sync)) {
            continue;
        }

        if (sync != 0xF0u) {
            continue;
        }

        uint8_t cmd_hi = 0;
        uint8_t cmd_lo = 0;

        int off_cmd_hi = -1;
        int off_cmd_lo = -1;

        bool ok_cmd_hi = find_any_byte_near(samples,
                                            offset + 1 * byte_samples,
                                            search_radius,
                                            &cmd_hi,
                                            &off_cmd_hi);

        bool ok_cmd_lo = find_any_byte_near(samples,
                                            offset + 2 * byte_samples,
                                            search_radius,
                                            &cmd_lo,
                                            &off_cmd_lo);

        if (!ok_cmd_hi || !ok_cmd_lo) {
            continue;
        }

        if (cmd != NULL) {
            *cmd = ((uint16_t)cmd_hi << 8) | cmd_lo;
        }

        bool all_data_ok = true;

        for (uint8_t i = 0; i < wc; i++) {
            uint8_t hi = 0;
            uint8_t lo = 0;

            int off_hi = -1;
            int off_lo = -1;

            int hi_center = offset + (3 + i * 2) * byte_samples;
            int lo_center = offset + (4 + i * 2) * byte_samples;

            bool ok_hi = find_any_byte_near(samples,
                                            hi_center,
                                            search_radius,
                                            &hi,
                                            &off_hi);

            bool ok_lo = find_any_byte_near(samples,
                                            lo_center,
                                            search_radius,
                                            &lo,
                                            &off_lo);

            if (!ok_hi || !ok_lo) {
                all_data_ok = false;
                break;
            }

            if (data != NULL) {
                data[i] = ((uint16_t)hi << 8) | lo;
            }
        }

        if (all_data_ok) {
            return true;
        }
    }

    return false;
}
static bool bus_pio_read_frame(uint16_t *cmd, uint16_t data[], uint8_t wc) {
    uint8_t samples[CAPTURE_SAMPLES];

    capture_samples(samples, CAPTURE_SAMPLES);

    const int byte_samples = 8 * SAMPLES_PER_BIT;
    const int search_radius = 20;

    for (int offset = 0;
         offset + ((3 + wc * 2) * byte_samples) < CAPTURE_SAMPLES;
         offset++) {

        uint8_t sync = 0;

        if (!decode_byte_at_phase(samples, offset, &sync)) {
            continue;
        }

        if (sync != 0xF0u) {
            continue;
        }

        uint8_t cmd_hi = 0;
        uint8_t cmd_lo = 0;
        int off_dummy = -1;

        bool ok_cmd_hi = find_byte_near(samples,
                                        offset + 1 * byte_samples,
                                        search_radius,
                                        0x18u,
                                        &cmd_hi,
                                        &off_dummy);

        bool ok_cmd_lo = find_byte_near(samples,
                                        offset + 2 * byte_samples,
                                        search_radius,
                                        0x23u,
                                        &cmd_lo,
                                        &off_dummy);

        if (!ok_cmd_hi || !ok_cmd_lo) {
            continue;
        }

        uint16_t rx_cmd = ((uint16_t)cmd_hi << 8) | cmd_lo;

        if (cmd != NULL) {
            *cmd = rx_cmd;
        }

        bool all_data_ok = true;

        for (uint8_t i = 0; i < wc; i++) {
            uint8_t hi = 0;
            uint8_t lo = 0;

            uint8_t expected_hi = 0xA0u;
            uint8_t expected_lo = i;

            bool ok_hi = find_byte_near(samples,
                                        offset + (3 + i * 2) * byte_samples,
                                        search_radius,
                                        expected_hi,
                                        &hi,
                                        &off_dummy);

            bool ok_lo = find_byte_near(samples,
                                        offset + (4 + i * 2) * byte_samples,
                                        search_radius,
                                        expected_lo,
                                        &lo,
                                        &off_dummy);

            if (!ok_hi || !ok_lo) {
                all_data_ok = false;
                break;
            }

            if (data != NULL) {
                data[i] = ((uint16_t)hi << 8) | lo;
            }
        }

        if (all_data_ok) {
            return true;
        }
    }

    return false;
}
bool bus_read_test_frame_pio(uint16_t *cmd, uint16_t data[], uint8_t wc) {
    rx_sampler_init(RX_PIO, RX_SM, BUS_PIN_P);
    return bus_pio_read_frame(cmd, data, wc);
}
bool bus_read_test_packet_pio(uint16_t *cmd, uint16_t data[], uint8_t wc) {
    rx_sampler_init(RX_PIO, RX_SM, BUS_PIN_P);
    uint8_t samples[CAPTURE_SAMPLES];

    capture_samples(samples, CAPTURE_SAMPLES);

    const int byte_samples = 8 * SAMPLES_PER_BIT;
    const int search_radius = 20;

    /*
     * Paquete esperado:
     *
     * 0: 0xF0  -> SYNC CMD/STATUS
     * 1: 0x18
     * 2: 0x23
     * 3: 0x0F  -> SYNC DATA
     * 4: 0xA0
     * 5: 0x00
     * 6: 0xA0
     * 7: 0x01
     * 8: 0xA0
     * 9: 0x02
     *
     * Total = 4 + wc*2 bytes
     */
    const int total_bytes = 4 + (wc * 2);

    for (int offset = 0;
         offset + (total_bytes * byte_samples) < CAPTURE_SAMPLES;
         offset++) {

        uint8_t sync_cmd = 0;

        if (!decode_byte_at_phase(samples, offset, &sync_cmd)) {
            continue;
        }

        if (sync_cmd != 0xF0u) {
            continue;
        }

        uint8_t cmd_hi = 0;
        uint8_t cmd_lo = 0;
        uint8_t sync_data = 0;

        int off_dummy = -1;

        bool ok_cmd_hi = find_byte_near(samples,
                                        offset + 1 * byte_samples,
                                        search_radius,
                                        0x18u,
                                        &cmd_hi,
                                        &off_dummy);

        bool ok_cmd_lo = find_byte_near(samples,
                                        offset + 2 * byte_samples,
                                        search_radius,
                                        0x23u,
                                        &cmd_lo,
                                        &off_dummy);

        bool ok_sync_data = find_byte_near(samples,
                                           offset + 3 * byte_samples,
                                           search_radius,
                                           0x0Fu,
                                           &sync_data,
                                           &off_dummy);

        if (!ok_cmd_hi || !ok_cmd_lo || !ok_sync_data) {
            continue;
        }

        uint16_t rx_cmd = ((uint16_t)cmd_hi << 8) | cmd_lo;

        if (cmd != NULL) {
            *cmd = rx_cmd;
        }

        bool all_data_ok = true;

        for (uint8_t i = 0; i < wc; i++) {
            uint8_t hi = 0;
            uint8_t lo = 0;

            uint8_t expected_hi = 0xA0u;
            uint8_t expected_lo = i;

            /*
             * Los datos ahora empiezan después de:
             * F0 18 23 0F
             *
             * DATA0_H está en índice 4
             * DATA0_L está en índice 5
             */
            int hi_center = offset + (4 + i * 2) * byte_samples;
            int lo_center = offset + (5 + i * 2) * byte_samples;

            bool ok_hi = find_byte_near(samples,
                                        hi_center,
                                        search_radius,
                                        expected_hi,
                                        &hi,
                                        &off_dummy);

            bool ok_lo = find_byte_near(samples,
                                        lo_center,
                                        search_radius,
                                        expected_lo,
                                        &lo,
                                        &off_dummy);

            if (!ok_hi || !ok_lo) {
                all_data_ok = false;
                break;
            }

            if (data != NULL) {
                data[i] = ((uint16_t)hi << 8) | lo;
            }
        }

        if (all_data_ok) {
            return true;
        }
    }

    return false;
}
bool bus_read_packet_checked_pio(uint16_t *cmd, uint16_t data[], uint8_t wc) {
    rx_sampler_init(RX_PIO, RX_SM, BUS_PIN_P);

    uint8_t samples[CAPTURE_SAMPLES];
    capture_samples(samples, CAPTURE_SAMPLES);

    const int byte_samples = 8 * SAMPLES_PER_BIT;
    const int word_samples = 16 * SAMPLES_PER_BIT;
    const int search_radius = 12;

    /*
     * F0 + CMD(2B) + 0F + DATA(wc*2B) + CHK(2B)
     */
    const int total_bytes = 1 + 2 + 1 + (wc * 2) + 2;

    int dbg_sync_f0 = 0;
    int dbg_cmd_ok = 0;
    int dbg_sync_data_ok = 0;
    int dbg_data_ok = 0;

    for (int offset = 0;
         offset + (total_bytes * byte_samples) < CAPTURE_SAMPLES;
         offset++) {

        uint8_t sync_cmd = 0;

        if (!decode_byte_at_phase(samples, offset, &sync_cmd)) {
            continue;
        }

        if (sync_cmd != 0xF0u) {
            continue;
        }
        dbg_sync_f0++;
        uint8_t cmd_hi = 0;
        uint8_t cmd_lo = 0;

        int off_cmd_hi = -1;
        int off_cmd_lo = -1;

        bool ok_cmd_hi = find_byte_near(samples,
                                        offset + 1 * byte_samples,
                                        search_radius,
                                        0x18u,
                                        &cmd_hi,
                                        &off_cmd_hi);

        bool ok_cmd_lo = find_byte_near(samples,
                                        offset + 2 * byte_samples,
                                        search_radius,
                                        0x03u,
                                        &cmd_lo,
                                        &off_cmd_lo);

        if (!ok_cmd_hi || !ok_cmd_lo) {
            continue;
        }

        uint16_t rx_cmd = ((uint16_t)cmd_hi << 8) | cmd_lo;

        dbg_cmd_ok++;

        uint8_t sync_data = 0;
        int off_sync_data = -1;

        /*
        * Buscar el 0x0F después del byte bajo real del CMD.
        * off_cmd_lo es la posición real donde encontró 0x23.
        */
        if (!find_byte_near(samples,
                            off_cmd_lo + byte_samples,
                            search_radius,
                            0x0Fu,
                            &sync_data,
                            &off_sync_data)) {
            continue;
        }

        dbg_sync_data_ok++;
        uint16_t temp_data[16] = {0};

        if (wc > 16) {
            return false;
        }

        bool data_ok = true;

        /*
        * El primer dato debería empezar después del SYNC_DATA.
        * Pero después de encontrar cada word, usamos su offset real
        * para buscar el siguiente. Esto evita acumulación de desfase.
        */
        int next_word_center = off_sync_data + byte_samples;

        for (uint8_t i = 0; i < wc; i++) {
            int off_word = -1;

            if (!find_any_word16_near(samples,
                                    next_word_center,
                                    search_radius,
                                    &temp_data[i],
                                    &off_word)) {
                data_ok = false;
                break;
            }

            /*
            * Próxima palabra: 16 bits después del offset real encontrado.
            */
            next_word_center = off_word + word_samples;
        }

        if (!data_ok) {
            continue;
        }

        dbg_data_ok++;

        uint16_t rx_chk = 0;
        int off_chk = -1;

        /*
        * El checksum va después del último data word,
        * usando el offset real encadenado.
        */
        if (!find_any_word16_near(samples,
                                next_word_center,
                                search_radius,
                                &rx_chk,
                                &off_chk)) {
            continue;
        }

        uint16_t calc = rx_cmd;

        for (uint8_t i = 0; i < wc; i++) {
            calc ^= temp_data[i];
        }

        static int dbg_count = 0;

        if (dbg_count < 20) {
            dbg_count++;

            printf("CHK_TEST|CMD=0x%04X|D0=0x%04X|D1=0x%04X|D2=0x%04X|RX_CHK=0x%04X|CALC=0x%04X\n",
                rx_cmd,
                temp_data[0],
                temp_data[1],
                temp_data[2],
                rx_chk,
                calc);
        }

        if (calc != rx_chk) {
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

        return true;
    }
    return false;
}
bool bus_read_packet_checked_auto_pio(uint16_t *cmd,
                                      uint16_t data[],
                                      uint8_t max_wc,
                                      uint8_t *rx_wc) {
    rx_sampler_init(RX_PIO, RX_SM, BUS_PIN_P);

    uint8_t samples[CAPTURE_SAMPLES];
    capture_samples(samples, CAPTURE_SAMPLES);

    const int byte_samples = 8 * SAMPLES_PER_BIT;
    const int word_samples = 16 * SAMPLES_PER_BIT;
    const int search_radius = 12;

    /*
     * Máximo esperado:
     * F0 + CMD(2B) + 0F + DATA(max_wc*2B) + CHK(2B)
     */
    const int max_total_bytes = 1 + 2 + 1 + (max_wc * 2) + 2;

    int dbg_f0 = 0;
    int dbg_cmd_hi = 0;
    int dbg_cmd_lo = 0;
    int dbg_sync_data = 0;
    int dbg_data = 0;
    int dbg_chk = 0;

    /*
     * Para esta versión:
     * WC = 1..max_wc.
     * En 1553 real WC usa 5 bits: 1..31. Dejamos WC=0 reservado.
     */
    if (max_wc == 0 || max_wc > BUS_1553_MAX_DATA_WORDS) {
        return false;
    }

    for (int offset = 0;
         offset + (max_total_bytes * byte_samples) < CAPTURE_SAMPLES;
         offset++) {

        uint8_t sync_cmd = 0;

        if (!decode_byte_at_phase(samples, offset, &sync_cmd)) {
            continue;
        }

        if (sync_cmd != 0xF0u) {
            continue;
        }

        dbg_f0++;

        /*
         * Buscar:
         *
         * F0 CMD_H CMD_L 0F
         *
         * No aceptamos CMD_H/CMD_L si después no aparece SYNC_DATA 0x0F.
         * Esto evita agarrar bytes falsos o corridos.
         */
        uint8_t cmd_hi = 0;
        uint8_t cmd_lo = 0;
        uint8_t sync_data = 0;

        int off_cmd_hi = -1;
        int off_cmd_lo = -1;
        int off_sync_data = -1;

        bool ok_cmd_and_sync = false;

        for (int delta_hi = -search_radius; delta_hi <= search_radius; delta_hi++) {
            int candidate_off_cmd_hi = offset + byte_samples + delta_hi;

            if (candidate_off_cmd_hi < 0) {
                continue;
            }

            if (candidate_off_cmd_hi + byte_samples >= CAPTURE_SAMPLES) {
                continue;
            }

            uint8_t candidate_cmd_hi = 0;

            if (!decode_byte_at_phase(samples, candidate_off_cmd_hi, &candidate_cmd_hi)) {
                continue;
            }

            dbg_cmd_hi++;

            /*
             * CMD_L debería estar un byte después de CMD_H.
             * Lo buscamos cerca de esa posición.
             */
            for (int delta_lo = -search_radius; delta_lo <= search_radius; delta_lo++) {
                int candidate_off_cmd_lo =
                    candidate_off_cmd_hi + byte_samples + delta_lo;

                if (candidate_off_cmd_lo < 0) {
                    continue;
                }

                if (candidate_off_cmd_lo + byte_samples >= CAPTURE_SAMPLES) {
                    continue;
                }

                uint8_t candidate_cmd_lo = 0;

                if (!decode_byte_at_phase(samples,
                                          candidate_off_cmd_lo,
                                          &candidate_cmd_lo)) {
                    continue;
                }

                uint16_t candidate_cmd =
                    ((uint16_t)candidate_cmd_hi << 8) | candidate_cmd_lo;

                uint8_t candidate_rt  = BUS_1553_CMD_RT(candidate_cmd);
                uint8_t candidate_tr  = BUS_1553_CMD_TR(candidate_cmd);
                uint8_t candidate_sub = BUS_1553_CMD_SUB(candidate_cmd);
                uint8_t candidate_wc  = BUS_1553_CMD_WC(candidate_cmd);

                /*
                 * Validaciones básicas.
                 * Para esta prueba esperamos:
                 * RT = 3
                 * TR = 0, BC -> RT
                 * SUB = 2
                 * WC = 1..max_wc
                 */
                if (candidate_wc == 0 || candidate_wc > max_wc) {
                    continue;
                }

                if (candidate_tr != BUS_1553_TR_BC_TO_RT) {
                    continue;
                }

                /*
                 * Filtro de prueba.
                 * Si después querés aceptar cualquier RT/SUB, comentá este if.
                 */
                if (candidate_rt != 3u || candidate_sub != 2u) {
                    continue;
                }

                /*
                 * Solo aceptamos este CMD si luego aparece SYNC_DATA 0x0F.
                 */
                uint8_t candidate_sync_data = 0;
                int candidate_off_sync_data = -1;

                if (!find_byte_near(samples,
                                    candidate_off_cmd_lo + byte_samples,
                                    search_radius,
                                    0x0Fu,
                                    &candidate_sync_data,
                                    &candidate_off_sync_data)) {
                    continue;
                }

                cmd_hi = candidate_cmd_hi;
                cmd_lo = candidate_cmd_lo;
                sync_data = candidate_sync_data;

                off_cmd_hi = candidate_off_cmd_hi;
                off_cmd_lo = candidate_off_cmd_lo;
                off_sync_data = candidate_off_sync_data;

                ok_cmd_and_sync = true;
                break;
            }

            if (ok_cmd_and_sync) {
                break;
            }
        }

        if (!ok_cmd_and_sync) {
            continue;
        }

        dbg_cmd_lo++;
        dbg_sync_data++;

        uint16_t rx_cmd = ((uint16_t)cmd_hi << 8) | cmd_lo;
        uint8_t wc = BUS_1553_CMD_WC(rx_cmd);

        uint16_t temp_data[BUS_1553_MAX_DATA_WORDS] = {0};
        bool data_ok = true;

        /*
         * DATA empieza después de SYNC_DATA.
         * Usamos offsets encadenados para evitar desfase acumulado.
         */
        int next_word_center = off_sync_data + byte_samples;

        for (uint8_t i = 0; i < wc; i++) {
            int off_word = -1;

            if (!find_any_word16_near(samples,
                                      next_word_center,
                                      search_radius,
                                      &temp_data[i],
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
         * Checksum después del último dato.
         */
        /*
        * Calcular checksum esperado.
        */
        uint16_t calc = rx_cmd;

        for (uint8_t i = 0; i < wc; i++) {
            calc ^= temp_data[i];
        }

       /*
        * Debug temporal del checksum:
        * primero leemos cualquier word16 cercano para ver qué está llegando,
        * y comparamos contra calc.
        */
        uint16_t rx_chk = 0;
        int off_chk = -1;

        if (!find_any_word16_near(samples,
                                next_word_center,
                                24,
                                &rx_chk,
                                &off_chk)) {
            continue;
        }

        dbg_chk++;

        static int chk_dbg = 0;

        if (chk_dbg < 30) {
            chk_dbg++;

            /*printf("CHK_DBG|CMD=0x%04X|WC=%u|D0=0x%04X|D1=0x%04X|D2=0x%04X|RX_CHK=0x%04X|CALC=0x%04X|OFF_CHK=%d|CENTER=%d\n",
                rx_cmd,
                wc,
                temp_data[0],
                temp_data[1],
                temp_data[2],
                rx_chk,
                calc,
                off_chk,
                next_word_center);*/
        }

        if (calc != rx_chk) {
            continue;
        }

        if (cmd != NULL) {
            *cmd = rx_cmd;
        }

        if (rx_wc != NULL) {
            *rx_wc = wc;
        }

        if (data != NULL) {
            for (uint8_t i = 0; i < wc; i++) {
                data[i] = temp_data[i];
            }
        }

        return true;
    }

    /*
     * Debug temporal. Cuando funcione estable, podés comentarlo.
     */
    /*printf("AUTO_DBG|F0=%d|CMD_H=%d|CMD_L=%d|SYNC_DATA=%d|DATA=%d|CHK=%d\n",
           dbg_f0,
           dbg_cmd_hi,
           dbg_cmd_lo,
           dbg_sync_data,
           dbg_data,
           dbg_chk);*/

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

#if BUS_USE_PIO_TX
static PIO bus_tx_pio = pio0;
static const uint bus_tx_sm = 0u;
static uint bus_tx_offset = 0u;
static bool bus_tx_pio_initialized = false;

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
    uint8_t hi = (uint8_t)((word >> 8) & 0xFFu);
    uint8_t lo = (uint8_t)(word & 0xFFu);

    bus_send_byte(hi);
    bus_send_byte(lo);
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
     * Postámbulo neutro para proteger el final del status.
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
    * Separación entre STATUS y DATA SYNC.
    * No pertenece al paquete; solo estabiliza la búsqueda del 0x0F.
    */
    bus_send_byte(0x00);

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
void bus_send_word16_parity(uint16_t word) {
    bus_send_word16(word);
    bus_send_bit(bus_compute_odd_parity(word) != 0u);
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
static bool find_any_word16_near(const uint8_t *samples,
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

            if (pos + (16 * SAMPLES_PER_BIT) >= CAPTURE_SAMPLES) {
                continue;
            }

            uint8_t hi = 0;
            uint8_t lo = 0;

            if (decode_byte_at_phase(samples, pos, &hi) &&
                decode_byte_at_phase(samples, pos + 8 * SAMPLES_PER_BIT, &lo)) {

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
static bool find_word16_near(const uint8_t *samples,
                             int center,
                             int radius,
                             uint16_t target,
                             uint16_t *found_word,
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

            if (pos + (16 * SAMPLES_PER_BIT) >= CAPTURE_SAMPLES) {
                continue;
            }

            uint8_t hi = 0;
            uint8_t lo = 0;

            if (decode_byte_at_phase(samples, pos, &hi) &&
                decode_byte_at_phase(samples, pos + 8 * SAMPLES_PER_BIT, &lo)) {

                uint16_t w = ((uint16_t)hi << 8) | lo;

                if (w == target) {
                    if (found_word != NULL) {
                        *found_word = w;
                    }

                    if (found_offset != NULL) {
                        *found_offset = pos;
                    }

                    return true;
                }
            }
        }
    }

    return false;
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
bool bus_read_command_word_pio(uint16_t *cmd) {
    rx_sampler_init(RX_PIO, RX_SM, BUS_PIN_P);

    uint8_t samples[CAPTURE_SAMPLES];
    capture_samples(samples, CAPTURE_SAMPLES);

    const int byte_samples = 8 * SAMPLES_PER_BIT;
    const int search_radius = 12;

    /*
     * Comando esperado:
     * F0 + CMD(2 bytes)
     */
    const int total_bytes = 1 + 2;

    for (int offset = 0;
         offset + (total_bytes * byte_samples) < CAPTURE_SAMPLES;
         offset++) {

        uint8_t sync = 0;

        if (!decode_byte_at_phase(samples, offset, &sync)) {
            continue;
        }

        if (sync != 0xF0u) {
            continue;
        }

        uint16_t rx_cmd = 0;
        int off_cmd = -1;

        if (!find_any_word16_near(samples,
                                  offset + byte_samples,
                                  search_radius,
                                  &rx_cmd,
                                  &off_cmd)) {
            continue;
        }

        uint8_t rt  = BUS_1553_CMD_RT(rx_cmd);
        uint8_t tr  = BUS_1553_CMD_TR(rx_cmd);
        uint8_t wc  = BUS_1553_CMD_WC(rx_cmd);

        /*
         * Filtro básico de comando 1553-like.
         */
        if (rt == 0 || rt > 31) {
            continue;
        }

        if (wc == 0 || wc > BUS_1553_MAX_DATA_WORDS) {
            continue;
        }

        /*
         * Para esta prueba queremos TR=1.
         */
        if (tr != BUS_1553_TR_RT_TO_BC) {
            continue;
        }

        if (cmd != NULL) {
            *cmd = rx_cmd;
        }

        return true;
    }

    return false;
}