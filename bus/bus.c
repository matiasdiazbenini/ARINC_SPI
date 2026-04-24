#include "bus.h"

#include "hardware/clocks.h"
#include "pico/platform.h"
#include "pico/stdlib.h"

#define BUS_BIT_RATE_HZ          20000u
#define BUS_HALFBIT_RATE_HZ      (BUS_BIT_RATE_HZ * 2u)
#define BUS_SYNC_PULSE_US        4u
#define BUS_SYNC_GAP_US          2u
#define BUS_SYNC_MIN_PULSE_US    3u
#define BUS_SYNC_MAX_PULSE_US    12u
#define BUS_SYNC_TRANSITION_US   6u

static uint32_t s_half_bit_cycles = 1u;

static inline void bus_delay_half_bit(void) {
    busy_wait_at_least_cycles(s_half_bit_cycles);
}

static inline void bus_drive_levels(bool p_high) {
    gpio_put(BUS_PIN_P, p_high);
    gpio_put(BUS_PIN_N, !p_high);
}

static inline bool bus_line_is_valid_level(bool p_high) {
    const bool p = gpio_get(BUS_PIN_P);
    const bool n = gpio_get(BUS_PIN_N);
    return (p == p_high) && (n == !p_high);
}

static bool bus_wait_level_with_timeout(bool p_high,
                                        uint64_t hard_deadline_us,
                                        uint32_t max_wait_us,
                                        uint64_t *out_start_us) {
    uint64_t now_us = to_us_since_boot(get_absolute_time());
    uint64_t wait_deadline_us = hard_deadline_us;

    if (max_wait_us > 0u) {
        const uint64_t soft_deadline_us = now_us + (uint64_t)max_wait_us;
        if (soft_deadline_us < wait_deadline_us) {
            wait_deadline_us = soft_deadline_us;
        }
    }

    while ((now_us = to_us_since_boot(get_absolute_time())) < wait_deadline_us) {
        if (bus_line_is_valid_level(p_high)) {
            if (out_start_us) {
                *out_start_us = now_us;
            }
            return true;
        }
        tight_loop_contents();
    }

    return false;
}

static bool bus_measure_pulse_us(bool level_high,
                                 uint64_t pulse_start_us,
                                 uint64_t hard_deadline_us,
                                 uint32_t min_us,
                                 uint32_t max_us,
                                 uint64_t *out_end_us) {
    uint64_t now_us = pulse_start_us;

    while ((now_us = to_us_since_boot(get_absolute_time())) < hard_deadline_us) {
        if (!bus_line_is_valid_level(level_high)) {
            const uint64_t pulse_us = now_us - pulse_start_us;
            if (pulse_us < (uint64_t)min_us || pulse_us > (uint64_t)max_us) {
                return false;
            }
            if (out_end_us) {
                *out_end_us = now_us;
            }
            return true;
        }

        if ((now_us - pulse_start_us) > (uint64_t)max_us) {
            return false;
        }
        tight_loop_contents();
    }

    return false;
}

void bus_init(void) {
    gpio_init(BUS_PIN_P);
    gpio_init(BUS_PIN_N);

    // Calculo de medio bit en ciclos de CPU para aproximar 0.5 us.
    const uint32_t sys_hz = clock_get_hz(clk_sys);
    s_half_bit_cycles = (sys_hz + (BUS_HALFBIT_RATE_HZ / 2u)) / BUS_HALFBIT_RATE_HZ;
    if (s_half_bit_cycles == 0u) {
        s_half_bit_cycles = 1u;
    }

    bus_set_rx_mode();
}

void bus_set_tx_mode(void) {
    gpio_disable_pulls(BUS_PIN_P);
    gpio_disable_pulls(BUS_PIN_N);

    gpio_set_dir(BUS_PIN_P, GPIO_OUT);
    gpio_set_dir(BUS_PIN_N, GPIO_OUT);

    // Estado inicial de linea antes de transmitir.
    bus_drive_levels(false);
}

void bus_set_rx_mode(void) {
    gpio_set_dir(BUS_PIN_P, GPIO_IN);
    gpio_set_dir(BUS_PIN_N, GPIO_IN);

    // Sin pull-up/pull-down para simular alta impedancia real.
    gpio_disable_pulls(BUS_PIN_P);
    gpio_disable_pulls(BUS_PIN_N);
}

void bus_send_bit(int bit) {
    const bool first_half_p_high = (bit != 0);

    // Primera mitad de bit.
    bus_drive_levels(first_half_p_high);
    bus_delay_half_bit();

    // Transicion de mitad de bit.
    bus_drive_levels(!first_half_p_high);
    bus_delay_half_bit();
}

int bus_receive_bit(void) {
    // Muestreo al inicio del bit.
    const bool start_p = gpio_get(BUS_PIN_P);
    const bool start_n = gpio_get(BUS_PIN_N);
    if (start_p == start_n) {
        // Error diferencial (P/N no complementarios).
        bus_delay_half_bit();
        return -1;
    }

    // Muestreo en mitad de bit (donde debe ocurrir la transicion Manchester).
    bus_delay_half_bit();
    const bool mid_p = gpio_get(BUS_PIN_P);
    const bool mid_n = gpio_get(BUS_PIN_N);
    if (mid_p == mid_n) {
        bus_delay_half_bit();
        return -1;
    }
    if (mid_p == start_p) {
        // No hubo transicion en mitad de bit.
        bus_delay_half_bit();
        return -1;
    }

    // Completa el tiempo del bit para dejar la funcion alineada al siguiente.
    bus_delay_half_bit();

    // Manchester-II:
    // 1 -> alto->bajo en BUS_P
    // 0 -> bajo->alto en BUS_P
    if (start_p && !mid_p) {
        return 1;
    }
    if (!start_p && mid_p) {
        return 0;
    }

    return -1;
}

void bus_send_sync(void) {
    // Sync simple de laboratorio:
    // pulso alto -> pulso bajo -> pulso alto -> gap bajo.
    bus_drive_levels(true);
    sleep_us(BUS_SYNC_PULSE_US);

    bus_drive_levels(false);
    sleep_us(BUS_SYNC_PULSE_US);

    bus_drive_levels(true);
    sleep_us(BUS_SYNC_PULSE_US);

    bus_drive_levels(false);
    sleep_us(BUS_SYNC_GAP_US);
}

bool bus_wait_sync(uint32_t timeout_us) {
    const uint64_t t_start = to_us_since_boot(get_absolute_time());
    const uint64_t t_deadline = t_start + (uint64_t)timeout_us;

    while (to_us_since_boot(get_absolute_time()) < t_deadline) {
        uint64_t t_high1_start = 0;
        uint64_t t_low1_start = 0;
        uint64_t t_high2_start = 0;
        uint64_t t_gap_start = 0;
        uint64_t pulse_end = 0;

        // 1) Pulso alto inicial.
        if (!bus_wait_level_with_timeout(true, t_deadline, 0u, &t_high1_start)) {
            // No aparecio un nuevo candidato antes del timeout global.
            break;
        }
        if (!bus_measure_pulse_us(true,
                                  t_high1_start,
                                  t_deadline,
                                  BUS_SYNC_MIN_PULSE_US,
                                  BUS_SYNC_MAX_PULSE_US,
                                  &pulse_end)) {
            continue;
        }

        // 2) Pulso bajo inmediatamente despues del alto.
        if (!bus_wait_level_with_timeout(false,
                                         t_deadline,
                                         BUS_SYNC_TRANSITION_US,
                                         &t_low1_start)) {
            continue;
        }
        if (!bus_measure_pulse_us(false,
                                  t_low1_start,
                                  t_deadline,
                                  BUS_SYNC_MIN_PULSE_US,
                                  BUS_SYNC_MAX_PULSE_US,
                                  &pulse_end)) {
            continue;
        }

        // 3) Segundo pulso alto inmediatamente despues del bajo.
        if (!bus_wait_level_with_timeout(true,
                                         t_deadline,
                                         BUS_SYNC_TRANSITION_US,
                                         &t_high2_start)) {
            continue;
        }
        if (!bus_measure_pulse_us(true,
                                  t_high2_start,
                                  t_deadline,
                                  BUS_SYNC_MIN_PULSE_US,
                                  BUS_SYNC_MAX_PULSE_US,
                                  &pulse_end)) {
            continue;
        }

        // 4) Gap en bajo, tambien inmediato tras el ultimo pulso.
        if (!bus_wait_level_with_timeout(false,
                                         t_deadline,
                                         BUS_SYNC_TRANSITION_US,
                                         &t_gap_start)) {
            continue;
        }

        const uint64_t t_gap_min_end = t_gap_start + (uint64_t)BUS_SYNC_GAP_US;
        if (t_gap_min_end > t_deadline) {
            // No hay tiempo suficiente para validar el gap completo.
            break;
        }

        // El gap debe permanecer estable al menos BUS_SYNC_GAP_US.
        bool gap_ok = true;
        while (to_us_since_boot(get_absolute_time()) < t_gap_min_end) {
            if (!bus_line_is_valid_level(false)) {
                gap_ok = false;
                break;
            }
            tight_loop_contents();
        }

        if (!gap_ok) {
            continue;
        }

        return true;
    }

    return false;
}

uint8_t bus_calc_odd_parity16(uint16_t word) {
    uint8_t parity = 1u;

    for (int bit = 0; bit < 16; bit++) {
        parity ^= (uint8_t)((word >> bit) & 0x01u);
    }

    return (uint8_t)(parity & 0x01u);
}

void bus_send_full_word(uint16_t word) {
    bus_send_sync();

    for (int bit = 15; bit >= 0; bit--) {
        bus_send_bit((word >> bit) & 0x01);
    }

    bus_send_bit(bus_calc_odd_parity16(word));
}

bool bus_receive_full_word(uint16_t *out_word,
                           bool *out_parity_ok,
                           uint32_t sync_timeout_us) {
    if (!out_word || !out_parity_ok) {
        return false;
    }

    if (!bus_wait_sync(sync_timeout_us)) {
        return false;
    }

    uint16_t word = 0;
    for (int i = 0; i < 16; i++) {
        int bit = bus_receive_bit();
        if (bit < 0) {
            return false;
        }
        word = (uint16_t)((word << 1) | (uint16_t)(bit & 0x01));
    }

    int parity_bit = bus_receive_bit();
    if (parity_bit < 0) {
        return false;
    }

    *out_word = word;
    *out_parity_ok = ((uint8_t)parity_bit == bus_calc_odd_parity16(word));
    return true;
}
