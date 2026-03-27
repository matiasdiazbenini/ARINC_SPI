#include <stdio.h>
#include <stdint.h>
#include "pico/stdlib.h"
#include "hardware/spi.h"

#define SPI_PORT spi0

#define PIN_MISO 16
#define PIN_CS   17
#define PIN_SCK  18
#define PIN_MOSI 19

#define SYNC_BYTE 0xA5

static inline void cs_select() {
    gpio_put(PIN_CS, 0);
}

static inline void cs_deselect() {
    gpio_put(PIN_CS, 1);
}

static int count_ones_31(uint32_t word) {
    int count = 0;
    for (int i = 0; i < 31; i++) {
        if (word & (1u << i)) count++;
    }
    return count;
}

// bits  0..7   = Label
// bits  8..9   = SDI
// bits 10..28  = Data
// bits 29..30  = SSM
// bit  31      = Parity impar
uint32_t build_arinc429_word(uint8_t label, uint8_t sdi, uint32_t data, uint8_t ssm) {
    uint32_t word = 0;

    word |= ((uint32_t)label & 0xFFu);
    word |= (((uint32_t)sdi  & 0x03u) << 8);
    word |= (((uint32_t)data & 0x7FFFFu) << 10);
    word |= (((uint32_t)ssm  & 0x03u) << 29);

    int ones = count_ones_31(word);
    if ((ones % 2) == 0) {
        word |= (1u << 31);
    }

    return word;
}

int main() {
    stdio_init_all();
    sleep_ms(2000);

    printf("MASTER SPI - Envio repetido de palabra ARINC 429\n");

    spi_init(SPI_PORT, 100 * 1000);  // más lento para asegurar estabilidad
    spi_set_format(SPI_PORT, 8, SPI_CPOL_0, SPI_CPHA_0, SPI_MSB_FIRST);

    gpio_set_function(PIN_MISO, GPIO_FUNC_SPI);
    gpio_set_function(PIN_SCK,  GPIO_FUNC_SPI);
    gpio_set_function(PIN_MOSI, GPIO_FUNC_SPI);

    gpio_init(PIN_CS);
    gpio_set_dir(PIN_CS, GPIO_OUT);
    cs_deselect();

    uint8_t label = 0xA5;
    uint8_t sdi   = 0x01;
    uint32_t data = 0x12345;
    uint8_t ssm   = 0x02;

    uint32_t arinc_word = build_arinc429_word(label, sdi, data, ssm);

    uint8_t txbuf[5];
    txbuf[0] = SYNC_BYTE;
    txbuf[1] = (arinc_word >> 24) & 0xFF;
    txbuf[2] = (arinc_word >> 16) & 0xFF;
    txbuf[3] = (arinc_word >> 8)  & 0xFF;
    txbuf[4] =  arinc_word        & 0xFF;

    printf("Palabra ARINC fija: 0x%08lX\n", arinc_word);
    printf("Trama SPI TX: %02X %02X %02X %02X %02X\n",
           txbuf[0], txbuf[1], txbuf[2], txbuf[3], txbuf[4]);

    sleep_ms(1000);

    for (int i = 0; i < 60; i++) {
        cs_select();
        sleep_us(20);  // pequeño margen antes de clockear

        spi_write_blocking(SPI_PORT, txbuf, 5);

        sleep_us(20);
        cs_deselect();

        printf("Envio %02d/60 realizado\n", i + 1);

        sleep_ms(1000);
    }

    printf("Fin del ensayo.\n");

    while (true) {
        tight_loop_contents();
    }
}