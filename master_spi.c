#include <stdio.h>
#include "pico/stdlib.h"
#include "hardware/spi.h"

#define SPI_PORT spi0

#define PIN_MISO 16
#define PIN_CS   17
#define PIN_SCK  18
#define PIN_MOSI 19
#define LED_PIN  PICO_DEFAULT_LED_PIN

static inline void cs_select() {
    gpio_put(PIN_CS, 0);
}

static inline void cs_deselect() {
    gpio_put(PIN_CS, 1);
}

int main() {
    stdio_init_all();

    gpio_init(LED_PIN);
    gpio_set_dir(LED_PIN, GPIO_OUT);

    sleep_ms(3000); // da tiempo a que aparezca el USB

    printf("MASTER - Etapa 1 Punto 1\r\n");
    printf("Enviando un byte por SPI cada 1 segundo\r\n");

    spi_init(SPI_PORT, 10000); // 10 kHz, bien lento para probar
    spi_set_format(SPI_PORT, 8, SPI_CPOL_0, SPI_CPHA_0, SPI_MSB_FIRST);

    gpio_set_function(PIN_MISO, GPIO_FUNC_SPI);
    gpio_set_function(PIN_SCK,  GPIO_FUNC_SPI);
    gpio_set_function(PIN_MOSI, GPIO_FUNC_SPI);

    gpio_init(PIN_CS);
    gpio_set_dir(PIN_CS, GPIO_OUT);
    cs_deselect();

    uint8_t dato = 'A';

    while (true) {
        gpio_put(LED_PIN, 1);

        cs_select();
        sleep_us(100);
        spi_write_blocking(SPI_PORT, &dato, 1);
        sleep_us(100);
        cs_deselect();

        printf("MASTER envio: 0x%02X (%c)\r\n", dato, dato);

        dato++;
        if (dato > 'Z') dato = 'A';

        sleep_ms(200);

        gpio_put(LED_PIN, 0);
        sleep_ms(800);
    }
}