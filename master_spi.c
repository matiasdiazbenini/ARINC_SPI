#include <stdio.h>
#include <stdint.h>
#include "pico/stdlib.h"
#include "hardware/spi.h"

#define SPI_PORT spi0

#define PIN_MISO 16
#define PIN_CS   17
#define PIN_SCK  18
#define PIN_MOSI 19

#define SYNC_BYTE 0xAX

static inline void cs_select() {
    gpio_put(PIN_CS, 0);
}

static inline void cs_deselect() {
    gpio_put(PIN_CS, 1);
}

static uint8_t calc_checksum(uint8_t sync, uint8_t type, uint8_t data) {
    return sync ^ type ^ data;
}

static void send_one_byte(uint8_t b) {
    cs_select();
    sleep_us(100);
    spi_write_blocking(SPI_PORT, &b, 1);
    sleep_us(100);
    cs_deselect();
    sleep_ms(2);
}

int main() {
    stdio_init_all();
    sleep_ms(3000);

    printf("MASTER - Emisor de mini tramas\n");
    printf("Formato: [SYNC][TYPE][DATA][CHECKSUM]\n\n");

    spi_init(SPI_PORT, 100 * 1000);
    spi_set_format(SPI_PORT, 8, SPI_CPOL_0, SPI_CPHA_0, SPI_MSB_FIRST);

    gpio_set_function(PIN_MISO, GPIO_FUNC_SPI);
    gpio_set_function(PIN_SCK,  GPIO_FUNC_SPI);
    gpio_set_function(PIN_MOSI, GPIO_FUNC_SPI);

    gpio_init(PIN_CS);
    gpio_set_dir(PIN_CS, GPIO_OUT);
    cs_deselect();

    uint8_t frame[4];
    uint8_t data = 0x10;
    uint8_t types[3] = {0x01, 0x02, 0x03};
    int idx = 0;

    for (int i = 0; i < 60; i++) {
        frame[0] = SYNC_BYTE;
        frame[1] = types[idx];
        frame[2] = data;
        frame[3] = calc_checksum(frame[0], frame[1], frame[2]);

        send_one_byte(frame[0]);
        send_one_byte(frame[1]);
        send_one_byte(frame[2]);
        send_one_byte(frame[3]);

        printf("TX %02d/60 -> %02X %02X %02X %02X\n",
               i + 1, frame[0], frame[1], frame[2], frame[3]);

        data++;
        idx = (idx + 1) % 3;
        sleep_ms(1000);
    }

    printf("\nFin del envio.\n");

    while (true) {
        tight_loop_contents();
    }
}