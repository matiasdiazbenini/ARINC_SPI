#include <stdio.h>
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

int main() {
    stdio_init_all();
    sleep_ms(2000);

    printf("MASTER - Etapa 1 Punto 1\n");
    printf("Enviando un byte por SPI cada 1 segundo\n");

    spi_init(SPI_PORT, 100 * 1000); // 100 kHz
    spi_set_format(SPI_PORT, 8, SPI_CPOL_0, SPI_CPHA_0, SPI_MSB_FIRST);

    gpio_set_function(PIN_MISO, GPIO_FUNC_SPI);
    gpio_set_function(PIN_SCK,  GPIO_FUNC_SPI);
    gpio_set_function(PIN_MOSI, GPIO_FUNC_SPI);

    gpio_init(PIN_CS);
    gpio_set_dir(PIN_CS, GPIO_OUT);
    cs_deselect();

    uint8_t dato = 0x41; // arranca en 'A'

    while (true) {
        cs_select();
        sleep_us(50);
        spi_write_blocking(SPI_PORT, &dato, 1);
        sleep_us(50);
        cs_deselect();

        printf("Enviado: 0x%02X (%c)\n", dato, dato);

        dato++;
        if (dato > 0x5A) { // después de 'Z' vuelve a 'A'
            dato = 0x41;
        }

        sleep_ms(1000);
    }
}