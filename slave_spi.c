#include <stdio.h>
#include "pico/stdlib.h"
#include "hardware/spi.h"
#include "hardware/gpio.h"

#define SPI_PORT spi0

// Pines SPI0 en Pico 2 W
#define PIN_MISO 16
#define PIN_CS   17
#define PIN_SCK  18
#define PIN_MOSI 19

int main() {
    // Inicializa USB serial
    stdio_init_all();

    // Espera unos segundos para que el puerto serial aparezca en la PC
    sleep_ms(3000);

    printf("SLAVE - SPI bidireccional con logica de respuesta\n");
    printf("Regla: A->1, B->2, C->3\n\n");

    // Inicializa SPI en modo slave
    spi_init(SPI_PORT, 100 * 1000);
    spi_set_slave(SPI_PORT, true);
    spi_set_format(SPI_PORT, 8, SPI_CPOL_0, SPI_CPHA_0, SPI_MSB_FIRST);

    // Configura funciones SPI en los pines
    gpio_set_function(PIN_MISO, GPIO_FUNC_SPI);
    gpio_set_function(PIN_CS,   GPIO_FUNC_SPI);
    gpio_set_function(PIN_SCK,  GPIO_FUNC_SPI);
    gpio_set_function(PIN_MOSI, GPIO_FUNC_SPI);

    uint8_t rx = 0;   // byte recibido del master
    uint8_t tx = 0;   // byte a responder

    while (true) {

        // Espera a que el master active CS
        while (gpio_get(PIN_CS) == 1) {
            tight_loop_contents();
        }

        // El slave prepara una respuesta por defecto.
        // Durante la transferencia, recibe el byte del master y
        // simultaneamente devuelve este byte 'tx'.
        //
        // Como queremos que la respuesta corresponda al mensaje actual,
        // usamos una estrategia simple:
        // - dejamos un valor inicial
        // - leemos el mensaje
        // - en la proxima iteracion responderemos segun lo recibido
        //
        // Para este punto del proyecto, esto igual es valido porque
        // permite verificar la logica de interpretacion.
        spi_write_read_blocking(SPI_PORT, &tx, &rx, 1);

        // Espera a que termine la transaccion
        while (gpio_get(PIN_CS) == 0) {
            tight_loop_contents();
        }

        // Decide la respuesta para la proxima transferencia
        if (rx == 'A') {
            tx = '1';
        } else if (rx == 'B') {
            tx = '2';
        } else if (rx == 'C') {
            tx = '3';
        } else {
            tx = '?';
        }

        // Muestra lo recibido y lo que respondera en la siguiente transaccion
        printf("RX: %c | Proxima respuesta: %c\n", rx, tx);
    }
}