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

static uint8_t calc_checksum(uint8_t sync, uint8_t type, uint8_t data) {
    return sync ^ type ^ data;
}

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

static const char* type_to_name(uint8_t type) {
    switch (type) {
        case 0x01: return "TEMPERATURA";
        case 0x02: return "VELOCIDAD";
        case 0x03: return "PRESION";
        default:   return "DESCONOCIDO";
    }
}

int main() {
    stdio_init_all();
    sleep_ms(4000);

    printf("SNIFFER LOGICO - MODO AUTOMATICO\r\n");
    printf("Imprimiendo todas las tramas validas\r\n\r\n");

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

        int valid = 1;

        if (sync != SYNC_BYTE) valid = 0;
        if (checksum != expected) valid = 0;

        if (!valid) {
            printf("TRAMA INVALIDA -> %02X %02X %02X %02X\r\n",
                   rx[0], rx[1], rx[2], rx[3]);
            continue;
        }

        printf("MATCH -> TYPE: 0x%02X (%s) | DATA: 0x%02X | FRAME: %02X %02X %02X %02X\r\n",
               type, type_to_name(type), data, rx[0], rx[1], rx[2], rx[3]);
    }
}