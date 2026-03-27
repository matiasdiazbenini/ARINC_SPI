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

#define SYNC_BYTE 0xA5

static int count_ones_32(uint32_t word) {
    int count = 0;
    for (int i = 0; i < 32; i++) {
        if (word & (1u << i)) count++;
    }
    return count;
}

static int check_odd_parity(uint32_t word) {
    return (count_ones_32(word) % 2) == 1;
}

int main() {
    stdio_init_all();
    sleep_ms(2000);

    printf("SLAVE SPI - Recepcion repetida de palabra ARINC 429\n");

    spi_init(SPI_PORT, 100 * 1000);
    spi_set_slave(SPI_PORT, true);
    spi_set_format(SPI_PORT, 8, SPI_CPOL_0, SPI_CPHA_0, SPI_MSB_FIRST);

    gpio_set_function(PIN_MISO, GPIO_FUNC_SPI);
    gpio_set_function(PIN_CS,   GPIO_FUNC_SPI);
    gpio_set_function(PIN_SCK,  GPIO_FUNC_SPI);
    gpio_set_function(PIN_MOSI, GPIO_FUNC_SPI);

    uint8_t frame[5];
    int contador = 0;

    while (contador < 60) {
        // Espera inicio de frame
        while (gpio_get(PIN_CS) == 1) {
            tight_loop_contents();
        }

        // Lee exactamente 5 bytes mientras CS esté activo
        for (int i = 0; i < 5; i++) {
            while (!spi_is_readable(SPI_PORT)) {
                tight_loop_contents();
            }
            frame[i] = (uint8_t)spi_get_hw(SPI_PORT)->dr;
        }

        // Espera fin de frame
        while (gpio_get(PIN_CS) == 0) {
            tight_loop_contents();
        }

        // Validación de sync
        if (frame[0] != SYNC_BYTE) {
            printf("\nFrame invalido: sync incorrecto (%02X)\n", frame[0]);
            continue;
        }

        uint32_t word = ((uint32_t)frame[1] << 24) |
                        ((uint32_t)frame[2] << 16) |
                        ((uint32_t)frame[3] << 8)  |
                        ((uint32_t)frame[4]);

        uint8_t label  =  word        & 0xFF;
        uint8_t sdi    = (word >> 8)  & 0x03;
        uint32_t data  = (word >> 10) & 0x7FFFF;
        uint8_t ssm    = (word >> 29) & 0x03;
        uint8_t parity = (word >> 31) & 0x01;

        contador++;

        printf("\n=== Recepcion %02d/60 ===\n", contador);
        printf("Frame RX : %02X %02X %02X %02X %02X\n",
               frame[0], frame[1], frame[2], frame[3], frame[4]);
        printf("Palabra ARINC: 0x%08lX\n", word);
        printf("Label : 0x%02X\n", label);
        printf("SDI   : %u\n", sdi);
        printf("Data  : 0x%05lX\n", data);
        printf("SSM   : %u\n", ssm);
        printf("Parity bit : %u\n", parity);

        if (check_odd_parity(word)) {
            printf("Paridad: OK\n");
        } else {
            printf("Paridad: ERROR\n");
        }
    }

    printf("\nFin de recepcion: 60 tramas capturadas.\n");

    while (true) {
        tight_loop_contents();
    }
}