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
    sleep_ms(2000);

    printf("SLAVE - Etapa 1 Punto 1\n");
    printf("Esperando bytes por SPI...\n");

    spi_init(SPI_PORT, 100 * 1000);
    spi_set_slave(SPI_PORT, true);
    spi_set_format(SPI_PORT, 8, SPI_CPOL_0, SPI_CPHA_0, SPI_MSB_FIRST);

    gpio_set_function(PIN_MISO, GPIO_FUNC_SPI);
    gpio_set_function(PIN_CS,   GPIO_FUNC_SPI);
    gpio_set_function(PIN_SCK,  GPIO_FUNC_SPI);
    gpio_set_function(PIN_MOSI, GPIO_FUNC_SPI);

    uint8_t dato;

    while (true) {
        while (gpio_get(PIN_CS) == 1) {
            tight_loop_contents();
        }

        spi_read_blocking(SPI_PORT, 0x00, &dato, 1);

        while (gpio_get(PIN_CS) == 0) {
            tight_loop_contents();
        }

        printf("Recibido: 0x%02X", dato);

        if (dato >= 32 && dato <= 126) {
            printf(" (%c)", dato);
        }

        printf("\n");
    }
}