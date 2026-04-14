#include "bus.h"

#include "hardware/clocks.h"
#include "pico/platform.h"
#include "pico/stdlib.h"

#define BUS_BIT_RATE_HZ          1000000u
#define BUS_HALFBIT_RATE_HZ      (BUS_BIT_RATE_HZ * 2u)

static uint32_t s_half_bit_cycles = 1u;

static inline void bus_delay_half_bit(void) {
    busy_wait_at_least_cycles(s_half_bit_cycles);
}

static inline void bus_drive_levels(bool p_high) {
    gpio_put(BUS_PIN_P, p_high);
    gpio_put(BUS_PIN_N, !p_high);
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
