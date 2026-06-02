#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>
#include <string.h>

#include "pico/stdlib.h"
#include "hardware/spi.h"

#define TEST_STARTUP_DELAY_MS 1200u

#define TEST_SPI_PORT    spi0
#define TEST_SPI_RX_PIN  16u /* <- slave GP19 / MISO */
#define TEST_SPI_CSN_PIN 17u /* -> slave GP17 / CSn  */
#define TEST_SPI_SCK_PIN 18u /* -> slave GP18 / SCK  */
#define TEST_SPI_TX_PIN  19u /* -> slave GP16 / MOSI */

#define TEST_FRAME_SIZE 32u
#define TEST_SPI_HZ     10000u
#define TEST_GAP_MS     300u

static void build_tx_frame(uint8_t *frame) {
    memset(frame, 0, TEST_FRAME_SIZE);
    frame[0] = 0xA4u;
    frame[1] = 0x29u;
    frame[2] = 0x50u; /* P */
    frame[3] = 0x49u; /* I */
    frame[4] = 0x50u; /* P */
    frame[5] = 0x4Du; /* M */
    frame[6] = 0x41u; /* A */
    frame[7] = 0x53u; /* S */
    for (uint8_t i = 8u; i < TEST_FRAME_SIZE; ++i) {
        frame[i] = (uint8_t)(0x40u + i);
    }
}

static const char *classify_response(const uint8_t *frame) {
    static char buffer[64];

    bool all_zero = true;
    for (uint8_t i = 0u; i < TEST_FRAME_SIZE; ++i) {
        if (frame[i] != 0u) {
            all_zero = false;
            break;
        }
    }
    if (all_zero) {
        return "todo_cero";
    }
    if (memcmp(frame, "\xA4\x29\x53\x50\x49\x4D\x49\x4E", 8u) == 0) {
        return "firma_SPI_MIN_OK";
    }
    snprintf(buffer, sizeof(buffer),
             "cabecera=%02X%02X%02X%02X%02X%02X%02X%02X",
             frame[0], frame[1], frame[2], frame[3],
             frame[4], frame[5], frame[6], frame[7]);
    return buffer;
}

static void print_first8(const uint8_t *frame) {
    printf("%02X %02X %02X %02X %02X %02X %02X %02X",
           frame[0], frame[1], frame[2], frame[3],
           frame[4], frame[5], frame[6], frame[7]);
}

int main(void) {
    stdio_init_all();
    sleep_ms(TEST_STARTUP_DELAY_MS);

    spi_init(TEST_SPI_PORT, TEST_SPI_HZ);
    spi_set_format(TEST_SPI_PORT, 8u, SPI_CPOL_0, SPI_CPHA_0, SPI_MSB_FIRST);

    gpio_set_function(TEST_SPI_RX_PIN, GPIO_FUNC_SPI);
    gpio_set_function(TEST_SPI_SCK_PIN, GPIO_FUNC_SPI);
    gpio_set_function(TEST_SPI_TX_PIN, GPIO_FUNC_SPI);

    gpio_init(TEST_SPI_CSN_PIN);
    gpio_set_dir(TEST_SPI_CSN_PIN, GPIO_OUT);
    gpio_put(TEST_SPI_CSN_PIN, 1u);

    uint8_t tx_frame[TEST_FRAME_SIZE];
    uint8_t rx_frame[TEST_FRAME_SIZE];
    uint32_t attempt = 0u;

    build_tx_frame(tx_frame);

    printf("ARINC_SNIFFER_SPI_MASTER_MIN - prueba minima Pico master -> Pico slave\r\n");
    printf("Pico master SPI0: GP19=TX(MOSI) | GP18=SCK | GP17=CSn manual | GP16=RX(MISO)\r\n");
    printf("Objetivo: verificar si una Pico como master puede completar la transaccion con SPI slave hardware.\r\n");
    printf("TX first8: ");
    print_first8(tx_frame);
    printf("\r\n\r\n");

    while (true) {
        ++attempt;
        memset(rx_frame, 0, sizeof(rx_frame));

        gpio_put(TEST_SPI_CSN_PIN, 0u);
        sleep_us(10u);
        spi_write_read_blocking(TEST_SPI_PORT, tx_frame, rx_frame, TEST_FRAME_SIZE);
        sleep_us(10u);
        gpio_put(TEST_SPI_CSN_PIN, 1u);

        printf("[PM%02lu] RX first8=", (unsigned long)attempt);
        print_first8(rx_frame);
        printf(" | %s\r\n", classify_response(rx_frame));

        sleep_ms(TEST_GAP_MS);
    }
}
