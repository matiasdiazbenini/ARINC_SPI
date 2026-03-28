#include <stdio.h>
#include "pico/stdlib.h"
#include "hardware/spi.h"
#include "hardware/gpio.h"

#define SPI_PORT spi0

#define PIN_MISO 16
#define PIN_CS   17
#define PIN_SCK  18
#define PIN_MOSI 19
#define LED_PIN  PICO_DEFAULT_LED_PIN

int main() {
    stdio_init_all();

    gpio_init(LED_PIN);
    gpio_set_dir(LED_PIN, GPIO_OUT);

    sleep_ms(3000); // da tiempo a que aparezca el USB

    printf("SLAVE - Etapa 1 Punto 1\r\n");
    printf("Esperando bytes por SPI...\r\n");

    spi_init(SPI_PORT, 10000);
    spi_set_slave(SPI_PORT, true);
    spi_set_format(SPI_PORT, 8, SPI_CPOL_0, SPI_CPHA_0, SPI_MSB_FIRST);

    gpio_set_function(PIN_MISO, GPIO_FUNC_SPI);
    gpio_set_function(PIN_CS,   GPIO_FUNC_SPI);
    gpio_set_function(PIN_SCK,  GPIO_FUNC_SPI);
    gpio_set_function(PIN_MOSI, GPIO_FUNC_SPI);

    uint8_t dato = 0;

    while (true) {
        // señal de vida aunque no llegue nada
        gpio_put(LED_PIN, 1);
        sleep_ms(50);
        gpio_put(LED_PIN, 0);
        sleep_ms(50);

        if (gpio_get(PIN_CS) == 0) {
            spi_read_blocking(SPI_PORT, 0x00, &dato, 1);

            while (gpio_get(PIN_CS) == 0) {
                tight_loop_contents();
            }

            printf("SLAVE recibio: 0x%02X", dato);
            if (dato >= 32 && dato <= 126) {
                printf(" (%c)", dato);
            }
            printf("\r\n");
        }
    }
}