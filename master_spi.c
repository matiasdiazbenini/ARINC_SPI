#include <stdio.h>
#include <stdlib.h>     // rand(), srand()
#include "pico/stdlib.h"
#include "hardware/spi.h"

#define SPI_PORT spi0

// Pines SPI0 en Pico 2 W
#define PIN_MISO 16
#define PIN_CS   17
#define PIN_SCK  18
#define PIN_MOSI 19

// Funciones auxiliares para controlar CS manualmente
static inline void cs_select() {
    gpio_put(PIN_CS, 0);
}

static inline void cs_deselect() {
    gpio_put(PIN_CS, 1);
}

int main() {
    // Inicializa USB serial
    stdio_init_all();

    // Espera unos segundos para que el puerto serial aparezca en la PC
    sleep_ms(3000);

    printf("MASTER - SPI bidireccional con logica de mensajes\n");
    printf("Mensajes posibles: A, B o C\n");
    printf("Respuestas esperadas: 1, 2 o 3\n\n");

    // Inicializa SPI en modo master
    spi_init(SPI_PORT, 100 * 1000); // 100 kHz
    spi_set_format(SPI_PORT, 8, SPI_CPOL_0, SPI_CPHA_0, SPI_MSB_FIRST);

    // Configura funciones SPI en los pines
    gpio_set_function(PIN_MISO, GPIO_FUNC_SPI);
    gpio_set_function(PIN_SCK,  GPIO_FUNC_SPI);
    gpio_set_function(PIN_MOSI, GPIO_FUNC_SPI);

    // CS lo controlamos manualmente como GPIO
    gpio_init(PIN_CS);
    gpio_set_dir(PIN_CS, GPIO_OUT);
    cs_deselect();

    // Seed simple para que la secuencia no sea siempre igual
    srand((unsigned int)to_us_since_boot(get_absolute_time()));

    uint8_t tx = 0;   // byte a enviar
    uint8_t rx = 0;   // byte recibido del slave

    // Ensayo de 60 intercambios, uno por segundo
    for (int i = 0; i < 60; i++) {

        // Elegimos aleatoriamente uno de los tres mensajes
        int r = rand() % 3;
        if (r == 0) {
            tx = 'A';
        } else if (r == 1) {
            tx = 'B';
        } else {
            tx = 'C';
        }

        // Inicia la transaccion SPI
        cs_select();
        sleep_us(50);

        // Envia 1 byte y recibe 1 byte simultaneamente
        spi_write_read_blocking(SPI_PORT, &tx, &rx, 1);

        sleep_us(50);
        cs_deselect();

        // Verifica si la respuesta fue la esperada
        int ok = 0;
        if ((tx == 'A' && rx == '1') ||
            (tx == 'B' && rx == '2') ||
            (tx == 'C' && rx == '3')) {
            ok = 1;
        }

        // Muestra el resultado en terminal
        printf("Iteracion %02d/60 | TX: %c | RX: %c | %s\n",
               i + 1,
               tx,
               rx,
               ok ? "OK" : "ERROR");

        sleep_ms(1000);
    }

    printf("\nFin del ensayo.\n");

    while (true) {
        tight_loop_contents();
    }
}