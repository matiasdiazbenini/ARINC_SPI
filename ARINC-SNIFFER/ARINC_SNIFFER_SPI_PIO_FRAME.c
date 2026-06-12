#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include <stdio.h>

#include "pico/stdlib.h"
#include "hardware/pio.h"

#include "spi_frame_slave.pio.h"

#define TEST_STARTUP_DELAY_MS 1200u

#define TEST_SPI_MOSI_PIN 16u
#define TEST_SPI_CSN_PIN  17u
#define TEST_SPI_SCK_PIN  18u
#define TEST_SPI_MISO_PIN 19u
#define TEST_DIAG_PIN     20u

#define TEST_FRAME_SIZE 32u
#define TEST_FRAME_WORDS (TEST_FRAME_SIZE / 4u)

static uint8_t reverse8(uint8_t value) {
    value = (uint8_t)(((value & 0xF0u) >> 4) | ((value & 0x0Fu) << 4));
    value = (uint8_t)(((value & 0xCCu) >> 2) | ((value & 0x33u) << 2));
    value = (uint8_t)(((value & 0xAAu) >> 1) | ((value & 0x55u) << 1));
    return value;
}

static void fill_response(uint8_t frame[TEST_FRAME_SIZE]) {
    memset(frame, 0, TEST_FRAME_SIZE);
    frame[0] = 0xA4u;
    frame[1] = 0x29u;
    frame[2] = 0x53u; /* S */
    frame[3] = 0x50u; /* P */
    frame[4] = 0x49u; /* I */
    frame[5] = 0x4Du; /* M */
    frame[6] = 0x49u; /* I */
    frame[7] = 0x4Eu; /* N */
    for (uint8_t i = 8u; i < TEST_FRAME_SIZE; ++i) {
        frame[i] = i;
    }
}

static uint32_t pack_tx_word_msb_first(const uint8_t *bytes) {
    return ((uint32_t)reverse8(bytes[0]) << 0) |
           ((uint32_t)reverse8(bytes[1]) << 8) |
           ((uint32_t)reverse8(bytes[2]) << 16) |
           ((uint32_t)reverse8(bytes[3]) << 24);
}

static void unpack_rx_word_msb_first(uint32_t word, uint8_t *bytes) {
    bytes[0] = reverse8((uint8_t)((word >> 0) & 0xFFu));
    bytes[1] = reverse8((uint8_t)((word >> 8) & 0xFFu));
    bytes[2] = reverse8((uint8_t)((word >> 16) & 0xFFu));
    bytes[3] = reverse8((uint8_t)((word >> 24) & 0xFFu));
}

static void preload_response(PIO pio, uint sm_tx, const uint8_t frame[TEST_FRAME_SIZE]) {
    for (uint8_t i = 0u; i < TEST_FRAME_WORDS; ++i) {
        const uint32_t word = pack_tx_word_msb_first(&frame[i * 4u]);
        pio_sm_put_blocking(pio, sm_tx, word);
    }
}

static bool read_request(PIO pio, uint sm_rx, uint8_t frame[TEST_FRAME_SIZE]) {
    if (pio_sm_get_rx_fifo_level(pio, sm_rx) < TEST_FRAME_WORDS) {
        return false;
    }

    for (uint8_t i = 0u; i < TEST_FRAME_WORDS; ++i) {
        const uint32_t word = pio_sm_get(pio, sm_rx);
        unpack_rx_word_msb_first(word, &frame[i * 4u]);
    }
    return true;
}

static void init_pio_spi_frame(PIO pio,
                               uint sm_rx,
                               uint sm_tx,
                               uint rx_offset,
                               uint tx_offset) {
    pio_gpio_init(pio, TEST_SPI_MOSI_PIN);
    pio_gpio_init(pio, TEST_SPI_CSN_PIN);
    pio_gpio_init(pio, TEST_SPI_SCK_PIN);
    pio_gpio_init(pio, TEST_SPI_MISO_PIN);
    gpio_pull_up(TEST_SPI_CSN_PIN);
    gpio_pull_down(TEST_SPI_SCK_PIN);

    pio_sm_config rx_config = spi_frame_rx32_program_get_default_config(rx_offset);
    sm_config_set_in_pins(&rx_config, TEST_SPI_MOSI_PIN);
    sm_config_set_in_shift(&rx_config, true, false, 32);
    sm_config_set_fifo_join(&rx_config, PIO_FIFO_JOIN_RX);
    sm_config_set_clkdiv(&rx_config, 1.0f);

    pio_sm_config tx_config = spi_frame_tx32_program_get_default_config(tx_offset);
    sm_config_set_out_pins(&tx_config, TEST_SPI_MISO_PIN, 1);
    sm_config_set_set_pins(&tx_config, TEST_SPI_MISO_PIN, 1);
    sm_config_set_out_shift(&tx_config, true, false, 32);
    sm_config_set_fifo_join(&tx_config, PIO_FIFO_JOIN_TX);
    sm_config_set_clkdiv(&tx_config, 1.0f);

    pio_sm_init(pio, sm_rx, rx_offset, &rx_config);
    pio_sm_init(pio, sm_tx, tx_offset, &tx_config);
    pio_sm_set_consecutive_pindirs(pio, sm_rx, TEST_SPI_MOSI_PIN, 1, false);
    pio_sm_set_consecutive_pindirs(pio, sm_rx, TEST_SPI_CSN_PIN, 1, false);
    pio_sm_set_consecutive_pindirs(pio, sm_rx, TEST_SPI_SCK_PIN, 1, false);
    pio_sm_set_consecutive_pindirs(pio, sm_tx, TEST_SPI_MISO_PIN, 1, true);
    pio_sm_set_pins_with_mask(pio, sm_tx, 0u, 1u << TEST_SPI_MISO_PIN);

    pio_sm_clear_fifos(pio, sm_rx);
    pio_sm_clear_fifos(pio, sm_tx);
}

int main(void) {
    stdio_init_all();
    sleep_ms(TEST_STARTUP_DELAY_MS);

    printf("ARINC_SNIFFER_SPI_PIO_FRAME - prueba minima SPI slave por PIO\r\n");
    printf("Pico PIO slave: GP16=MOSI | GP17=CSn | GP18=SCK | GP19=MISO | GP20=diag\r\n");
    printf("Frame fijo TX: A4 29 53 50 49 4D 49 4E ...\r\n");
    printf("Modo: SPI mode 0, MSB first, 32 bytes por CS\r\n\r\n");

    gpio_init(TEST_DIAG_PIN);
    gpio_set_dir(TEST_DIAG_PIN, GPIO_OUT);
    gpio_put(TEST_DIAG_PIN, 0u);

    PIO pio = pio1;
    const uint sm_rx = 0u;
    const uint sm_tx = 1u;
    const uint rx_offset = pio_add_program(pio, &spi_frame_rx32_program);
    const uint tx_offset = pio_add_program(pio, &spi_frame_tx32_program);

    uint8_t tx_frame[TEST_FRAME_SIZE];
    uint8_t rx_frame[TEST_FRAME_SIZE];
    uint32_t frame_count = 0u;

    fill_response(tx_frame);
    init_pio_spi_frame(pio, sm_rx, sm_tx, rx_offset, tx_offset);
    preload_response(pio, sm_tx, tx_frame);
    pio_enable_sm_mask_in_sync(pio, (1u << sm_rx) | (1u << sm_tx));

    while (true) {
        if (read_request(pio, sm_rx, rx_frame)) {
            ++frame_count;
            gpio_put(TEST_DIAG_PIN, 1u);
            printf("PIO-FRAME #%lu RX first8=%02X %02X %02X %02X %02X %02X %02X %02X\r\n",
                   (unsigned long)frame_count,
                   rx_frame[0],
                   rx_frame[1],
                   rx_frame[2],
                   rx_frame[3],
                   rx_frame[4],
                   rx_frame[5],
                   rx_frame[6],
                   rx_frame[7]);

            pio_sm_clear_fifos(pio, sm_rx);
            preload_response(pio, sm_tx, tx_frame);
            gpio_put(TEST_DIAG_PIN, 0u);
        }
        tight_loop_contents();
    }
}
