#include <stdio.h>
#include <stdint.h>
#include "pico/stdlib.h"
#include "hardware/spi.h"
#include "hardware/gpio.h"

#define SPI_PORT spi0

#define PIN_MISO 16
#define PIN_CS   17
#define PIN_SCK  18
#define PIN_MOSI 19

#define SYNC_BYTE 0xAA
#define TYPE_BYTE 0x01

static uint8_t calc_checksum(uint8_t sync, uint8_t type, uint8_t data) {
    return sync ^ type ^ data;
}

// Recibe un byte en una transaccion SPI separada
static uint8_t receive_one_byte(void) {
    uint8_t dato = 0;

    while (gpio_get(PIN_CS) == 1) {
        tight_loop_contents();
    }

    spi_read_blocking(SPI_PORT, 0x00, &dato, 1);

    while (gpio_get(PIN_CS) == 0) {
        tight_loop_contents();
    }

    sleep_ms(1);
    return dato;
}

int main() {
    stdio_init_all();
    sleep_ms(3000);

    printf("SLAVE - Mini trama SPI unidireccional byte a byte\n");
    printf("Esperando tramas...\n\n");

    spi_init(SPI_PORT, 100 * 1000);
    spi_set_slave(SPI_PORT, true);
    spi_set_format(SPI_PORT, 8, SPI_CPOL_0, SPI_CPHA_0, SPI_MSB_FIRST);

    gpio_set_function(PIN_MISO, GPIO_FUNC_SPI);
    gpio_set_function(PIN_CS,   GPIO_FUNC_SPI);
    gpio_set_function(PIN_SCK,  GPIO_FUNC_SPI);
    gpio_set_function(PIN_MOSI, GPIO_FUNC_SPI);

    uint8_t rx[4];

    while (true) {
        rx[0] = receive_one_byte();
        rx[1] = receive_one_byte();
        rx[2] = receive_one_byte();
        rx[3] = receive_one_byte();

        uint8_t sync = rx[0];
        uint8_t type = rx[1];
        uint8_t data = rx[2];
        uint8_t checksum = rx[3];

        uint8_t expected = calc_checksum(sync, type, data);

        printf("RX -> %02X %02X %02X %02X | ",
               sync, type, data, checksum);

        if (sync != SYNC_BYTE) {
            printf("ERROR: SYNC invalido\n");
        } else if (type != TYPE_BYTE) {
            printf("ERROR: TYPE invalido\n");
        } else if (checksum != expected) {
            printf("ERROR: CHECKSUM invalido (esperado %02X)\n", expected);
        } else {
            printf("OK | DATA = 0x%02X\n", data);
        }
    }
}