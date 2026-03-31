#include <stdio.h>
#include "pico/stdlib.h"
#include "hardware/spi.h"
#include "hardware/gpio.h"

#define SPI_PORT spi0

#define PIN_MISO 16
#define PIN_CS   17
#define PIN_SCK  18
#define PIN_MOSI 19

int main() {
    stdio_init_all();
    sleep_ms(3000);

    printf("SLAVE - SPI Bidireccional\n");

    spi_init(SPI_PORT, 100 * 1000);
    spi_set_slave(SPI_PORT, true);
    spi_set_format(SPI_PORT, 8, SPI_CPOL_0, SPI_CPHA_0, SPI_MSB_FIRST);

    gpio_set_function(PIN_MISO, GPIO_FUNC_SPI);
    gpio_set_function(PIN_CS,   GPIO_FUNC_SPI);
    gpio_set_function(PIN_SCK,  GPIO_FUNC_SPI);
    gpio_set_function(PIN_MOSI, GPIO_FUNC_SPI);

    uint8_t rx = 0;
    uint8_t tx = 0x5A; // respuesta fija

    while (true) {

        // Espera CS activo
        while (gpio_get(PIN_CS) == 1) {
            tight_loop_contents();
        }

        // Transferencia controlada (1 byte)
        spi_write_read_blocking(SPI_PORT, &tx, &rx, 1);

        // Espera fin de frame
        while (gpio_get(PIN_CS) == 0) {
            tight_loop_contents();
        }

        printf("RX: 0x%02X | TX: 0x%02X\n", rx, tx);
    }
}