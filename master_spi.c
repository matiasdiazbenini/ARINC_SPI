#include <stdio.h>
#include <stdint.h>
#include "pico/stdlib.h"
#include "hardware/clocks.h"
#include "hardware/pio.h"
#include "arinc_gpio_tx.pio.h"

#define SYNC_PIN          4
#define SYNC_PREAMBLE_US  3000
#define ARINC_TX_PIN_BASE 2   // GP2 = A, GP3 = B
#define BIT_RATE_HZ       1000
#define WORD_GAP_US       15000
#define HALF_CYCLES       5

static uint8_t calc_odd_parity_31bits(uint32_t word_without_parity) {
    int ones = 0;
    for (int i = 0; i < 31; i++) {
        if ((word_without_parity >> i) & 1u) {
            ones++;
        }
    }
    return (ones % 2 == 0) ? 1 : 0;
}

static uint32_t build_arinc_word(uint8_t label, uint8_t sdi, uint32_t data, uint8_t ssm) {
    uint32_t word = 0;
    word |= ((uint32_t)(label & 0xFF))    << 0;
    word |= ((uint32_t)(sdi   & 0x03))    << 8;
    word |= ((uint32_t)(data  & 0x7FFFF)) << 10;
    word |= ((uint32_t)(ssm   & 0x03))    << 29;

    uint8_t parity = calc_odd_parity_31bits(word);
    word |= ((uint32_t)parity) << 31;
    return word;
}

static uint32_t encode_temperature(float temp_c) {
    float raw = (temp_c + 50.0f) / 0.25f;
    if (raw < 0) raw = 0;
    if (raw > 0x7FFFF) raw = 0x7FFFF;
    return (uint32_t)(raw + 0.5f);
}

static uint32_t encode_speed(float speed_kt) {
    if (speed_kt < 0) speed_kt = 0;
    if (speed_kt > 0x7FFFF) speed_kt = 0x7FFFF;
    return (uint32_t)(speed_kt + 0.5f);
}

static uint32_t encode_altitude(float altitude_ft) {
    float raw = altitude_ft / 10.0f;
    if (raw < 0) raw = 0;
    if (raw > 0x7FFFF) raw = 0x7FFFF;
    return (uint32_t)(raw + 0.5f);
}

static const char* ssm_to_text(uint8_t ssm) {
    switch (ssm) {
        case 3: return "NORMAL";
        case 1: return "NCD";
        case 2: return "FUNCTIONAL_TEST";
        case 0: return "FAILURE";
        default: return "DESCONOCIDO";
    }
}

static void arinc_tx_program_init(PIO pio, uint sm, uint offset, uint pin_base, float bit_rate_hz) {
    pio_gpio_init(pio, pin_base + 0);
    pio_gpio_init(pio, pin_base + 1);

    pio_sm_set_consecutive_pindirs(pio, sm, pin_base, 2, true);

    pio_sm_config c = arinc_gpio_tx_program_get_default_config(offset);

    sm_config_set_set_pins(&c, pin_base, 2);
    sm_config_set_out_shift(&c, true, true, 32); // LSB first, autopull 32
    sm_config_set_fifo_join(&c, PIO_FIFO_JOIN_TX);

    const float cycles_per_bit = 2.0f * (float)HALF_CYCLES;
    float clkdiv = (float)clock_get_hz(clk_sys) / (bit_rate_hz * cycles_per_bit);
    sm_config_set_clkdiv(&c, clkdiv);

    pio_sm_init(pio, sm, offset, &c);

    pio_sm_set_pins_with_mask(pio, sm, 0u, (1u << pin_base) | (1u << (pin_base + 1)));
    pio_sm_set_enabled(pio, sm, true);
}

int main() {
    stdio_init_all();
    sleep_ms(3000);

    printf("MASTER - ARINC-like por GPIO con PIO\r\n");
    printf("TX pins: GP2=ARINC_A, GP3=ARINC_B\r\n");
    printf("Bit rate: %d bps\r\n\r\n", BIT_RATE_HZ);

    gpio_init(SYNC_PIN);
    gpio_set_dir(SYNC_PIN, GPIO_OUT);
    gpio_put(SYNC_PIN, 0);

    PIO pio = pio0;
    uint sm = 0;
    uint offset = pio_add_program(pio, &arinc_gpio_tx_program);

    arinc_tx_program_init(pio, sm, offset, ARINC_TX_PIN_BASE, BIT_RATE_HZ);

    float temp_c = 20.0f;
    float speed_kt = 120.0f;
    float altitude_ft = 1000.0f;

    for (int i = 0; i < 60; i++) {
        int idx = i % 3;

        uint8_t label;
        uint8_t sdi;
        uint8_t ssm;
        uint32_t raw;
        const char *name;

        int ssm_cycle = (i / 3) % 4;
        if (ssm_cycle == 0) ssm = 3;
        else if (ssm_cycle == 1) ssm = 1;
        else if (ssm_cycle == 2) ssm = 2;
        else ssm = 0;

        if (idx == 0) {
            label = 0xA5;
            sdi = 0;
            raw = encode_temperature(temp_c);
            name = "TEMPERATURA";
        } else if (idx == 1) {
            label = 0xB1;
            sdi = 1;
            raw = encode_speed(speed_kt);
            name = "VELOCIDAD";
        } else {
            label = 0xC2;
            sdi = 2;
            raw = encode_altitude(altitude_ft);
            name = "ALTITUD";
        }

        uint32_t word = build_arinc_word(label, sdi, raw, ssm);

        uint32_t word_time_us = (32u * 1000000u) / BIT_RATE_HZ;

        gpio_put(SYNC_PIN, 1);
        sleep_us(SYNC_PREAMBLE_US);

        pio_sm_put_blocking(pio, sm, word);

        /* Esperar a que la palabra termine de salir */
        sleep_us(word_time_us + 2000);

        gpio_put(SYNC_PIN, 0);

        printf("TX %02d/60 -> WORD: 0x%08lX | LABEL: 0x%02X (%s) | RAW: %lu | SDI: %u | SSM: %u (%s)\r\n",
            i + 1,
            (unsigned long)word,
            label,
            name,
            (unsigned long)raw,
            sdi,
            ssm,
            ssm_to_text(ssm));

        sleep_us(WORD_GAP_US);
        sleep_ms(300);
    }

    printf("\r\nFin del envio.\r\n");

    while (true) {
        tight_loop_contents();
    }
}