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

/*
 * Empaquetado lógico usado en este proyecto:
 *
 * bits  0..7   -> LABEL   (8 bits)
 * bits  8..9   -> SDI     (2 bits)
 * bits 10..28  -> DATA    (19 bits)
 * bits 29..30  -> SSM     (2 bits)
 * bit   31     -> PARITY  (1 bit, impar)
 *
 * Nota:
 * Esto es una representacion logica de la palabra ARINC.
 * No estamos implementando aun la capa fisica real del bus.
 */

static uint8_t calc_odd_parity_31bits(uint32_t word_without_parity) {
    int ones = 0;

    for (int i = 0; i < 31; i++) {
        if ((word_without_parity >> i) & 1u) {
            ones++;
        }
    }

    /* Si la cantidad de unos es par, la paridad debe valer 1
       para que el total final sea impar */
    return (ones % 2 == 0) ? 1 : 0;
}

static uint32_t build_arinc_word(uint8_t label, uint8_t sdi, uint32_t data, uint8_t ssm) {
    uint32_t word = 0;

    word |= ((uint32_t)(label & 0xFF))       << 0;
    word |= ((uint32_t)(sdi   & 0x03))       << 8;
    word |= ((uint32_t)(data  & 0x7FFFF))    << 10;  // 19 bits
    word |= ((uint32_t)(ssm   & 0x03))       << 29;

    uint8_t parity = calc_odd_parity_31bits(word);
    word |= ((uint32_t)parity) << 31;

    return word;
}

int main() {
    stdio_init_all();
    sleep_ms(3000);

    printf("MASTER - ARINC 429 logico sobre SPI\r\n");
    printf("Enviando palabra de 32 bits como 4 bytes\r\n\r\n");

    spi_init(SPI_PORT, 100 * 1000);
    spi_set_format(SPI_PORT, 8, SPI_CPOL_0, SPI_CPHA_0, SPI_MSB_FIRST);

    gpio_set_function(PIN_MISO, GPIO_FUNC_SPI);
    gpio_set_function(PIN_SCK,  GPIO_FUNC_SPI);
    gpio_set_function(PIN_MOSI, GPIO_FUNC_SPI);

    gpio_init(PIN_CS);
    gpio_set_dir(PIN_CS, GPIO_OUT);
    cs_deselect();

    uint8_t label = 0xA5;
    uint8_t sdi   = 0x01;
    uint8_t ssm   = 0x03;
    uint32_t data = 0;

    for (int i = 0; i < 60; i++) {
        uint32_t word = build_arinc_word(label, sdi, data, ssm);

        uint8_t b0 = (word >>  0) & 0xFF;
        uint8_t b1 = (word >>  8) & 0xFF;
        uint8_t b2 = (word >> 16) & 0xFF;
        uint8_t b3 = (word >> 24) & 0xFF;

        send_one_byte(b0);
        send_one_byte(b1);
        send_one_byte(b2);
        send_one_byte(b3);

        printf("TX %02d/60 -> WORD: 0x%08lX | LABEL: 0x%02X | SDI: %u | DATA: %lu | SSM: %u\r\n",
               i + 1,
               (unsigned long)word,
               label,
               sdi,
               (unsigned long)data,
               ssm);

        data++;
        sleep_ms(1000);
    }

    printf("\r\nFin del envio.\r\n");

    while (true) {
        tight_loop_contents();
    }
}