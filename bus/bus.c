#include "bus.h"

#include "hardware/clocks.h"
#include "pico/platform.h"
#include "pico/stdlib.h"

#define BUS_BIT_RATE_HZ          1000000u
#define BUS_HALFBIT_RATE_HZ      (BUS_BIT_RATE_HZ * 2u)
#define BUS_SYNC_PULSE_US        4u
#define BUS_SYNC_GAP_US          2u
#define BUS_SYNC_MIN_PULSE_US    3u

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
        uint64_t t_now = 0;
        uint64_t t_pulse_start = 0;

        // 1) Busca primer pulso en alto.
        while ((t_now = to_us_since_boot(get_absolute_time())) < t_deadline &&
               !bus_line_is_valid_level(true)) {
            tight_loop_contents();
        }
        if (t_now >= t_deadline) {
            return false;
        }

        t_pulse_start = t_now;
        while ((t_now = to_us_since_boot(get_absolute_time())) < t_deadline &&
               bus_line_is_valid_level(true)) {
            tight_loop_contents();
        }
        if ((t_now - t_pulse_start) < BUS_SYNC_MIN_PULSE_US) {
            continue;
        }

        // 2) Busca pulso en bajo.
        while ((t_now = to_us_since_boot(get_absolute_time())) < t_deadline &&
               !bus_line_is_valid_level(false)) {
            tight_loop_contents();
        }
        if (t_now >= t_deadline) {
            return false;
        }

        t_pulse_start = t_now;
        while ((t_now = to_us_since_boot(get_absolute_time())) < t_deadline &&
               bus_line_is_valid_level(false)) {
            tight_loop_contents();
        }
        if ((t_now - t_pulse_start) < BUS_SYNC_MIN_PULSE_US) {
            continue;
        }

        // 3) Busca segundo pulso en alto.
        while ((t_now = to_us_since_boot(get_absolute_time())) < t_deadline &&
               !bus_line_is_valid_level(true)) {
            tight_loop_contents();
        }
        if (t_now >= t_deadline) {
            return false;
        }

        t_pulse_start = t_now;
        while ((t_now = to_us_since_boot(get_absolute_time())) < t_deadline &&
               bus_line_is_valid_level(true)) {
            tight_loop_contents();
        }
        if ((t_now - t_pulse_start) < BUS_SYNC_MIN_PULSE_US) {
            continue;
        }

        // 4) Detecta inicio de gap bajo y espera su duracion completa.
        while ((t_now = to_us_since_boot(get_absolute_time())) < t_deadline &&
               !bus_line_is_valid_level(false)) {
            tight_loop_contents();
        }
        if (t_now >= t_deadline) {
            return false;
        }

        sleep_us(BUS_SYNC_GAP_US);
        return true;
    }

    return false;
}
