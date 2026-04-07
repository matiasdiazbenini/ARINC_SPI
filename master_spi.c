#include <stdio.h>
#include <stdint.h>
#include "pico/stdlib.h"
#include "hardware/spi.h"

#define SPI_PORT spi0

#define PIN_MISO 16
#define PIN_CS   17
#define PIN_SCK  18
#define PIN_MOSI 19

static inline void cs_select() {
    gpio_put(PIN_CS, 0);
}

static inline void cs_deselect() {
    gpio_put(PIN_CS, 1);
}

static void send_one_byte(uint8_t b) {
    cs_select();
    sleep_us(100);
    spi_write_blocking(SPI_PORT, &b, 1);
    sleep_us(100);
    cs_deselect();
    sleep_ms(2);
}

static uint8_t calc_odd_parity_31bits(uint32_t word_without_parity) {
    int ones = 0;
    for (int i = 0; i < 31; i++) {
        if ((word_without_parity >> i) & 1u) ones++;
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

typedef struct {
    uint8_t label;
    uint8_t sdi;
    const char *name;
} arinc_profile_t;

int main() {
    stdio_init_all();
    sleep_ms(3000);

    printf("MASTER - ARINC 429 logico V4\r\n");
    printf("Con SSM semantico\r\n\r\n");

    spi_init(SPI_PORT, 100 * 1000);
    spi_set_format(SPI_PORT, 8, SPI_CPOL_0, SPI_CPHA_0, SPI_MSB_FIRST);

    gpio_set_function(PIN_MISO, GPIO_FUNC_SPI);
    gpio_set_function(PIN_SCK,  GPIO_FUNC_SPI);
    gpio_set_function(PIN_MOSI, GPIO_FUNC_SPI);

    gpio_init(PIN_CS);
    gpio_set_dir(PIN_CS, GPIO_OUT);
    cs_deselect();

    arinc_profile_t profiles[] = {
        {0xA5, 0x0, "TEMPERATURA"},
        {0xB1, 0x1, "VELOCIDAD"},
        {0xC2, 0x2, "ALTITUD"}
    };

    float temp_c = 20.0f;
    float speed_kt = 120.0f;
    float altitude_ft = 1000.0f;

    for (int i = 0; i < 60; i++) {
        int idx = i % 3;

        uint8_t label = profiles[idx].label;
        uint8_t sdi   = profiles[idx].sdi;
        uint8_t ssm;
        uint32_t data_raw = 0;

        /* Ciclo de estados:
           0 -> NORMAL
           1 -> NCD
           2 -> FUNCTIONAL TEST
           3 -> FAILURE
        */
        int ssm_cycle = (i / 3) % 4;
        if (ssm_cycle == 0) ssm = 3;
        else if (ssm_cycle == 1) ssm = 1;
        else if (ssm_cycle == 2) ssm = 2;
        else ssm = 0;

        if (idx == 0) {
            data_raw = encode_temperature(temp_c);
        } else if (idx == 1) {
            data_raw = encode_speed(speed_kt);
        } else {
            data_raw = encode_altitude(altitude_ft);
        }

        uint32_t word = build_arinc_word(label, sdi, data_raw, ssm);

        uint8_t b0 = (word >>  0) & 0xFF;
        uint8_t b1 = (word >>  8) & 0xFF;
        uint8_t b2 = (word >> 16) & 0xFF;
        uint8_t b3 = (word >> 24) & 0xFF;

        send_one_byte(b0);
        send_one_byte(b1);
        send_one_byte(b2);
        send_one_byte(b3);

        printf("TX %02d/60 -> LABEL: 0x%02X (%s) | RAW: %lu | SDI: %u | SSM: %u (%s) | WORD: 0x%08lX\r\n",
               i + 1,
               label,
               profiles[idx].name,
               (unsigned long)data_raw,
               sdi,
               ssm,
               ssm_to_text(ssm),
               (unsigned long)word);

        if (idx == 0) temp_c += 1.5f;
        else if (idx == 1) speed_kt += 7.0f;
        else altitude_ft += 250.0f;

        sleep_ms(1000);
    }

    printf("\r\nFin del envio.\r\n");

    while (true) {
        tight_loop_contents();
    }
}