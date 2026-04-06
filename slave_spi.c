#include <stdio.h>
#include <stdint.h>
#include "pico/stdlib.h"
#include "hardware/spi.h"
#include "hardware/gpio.h"
#include "mil1553_protocol.h"

#define SPI_PORT spi0

#define PIN_MISO 16
#define PIN_CS   17
#define PIN_SCK  18
#define PIN_MOSI 19

typedef struct {
    uint8_t last_seq;
    uint32_t valid_frames;
    uint32_t invalid_frames;
    bool has_last_seq;
} transport_stats_t;

static uint8_t receive_one_byte(void) {
    uint8_t dato = 0;

    while (gpio_get(PIN_CS) == 1) {
        tight_loop_contents();
    }

    spi_read_blocking(SPI_PORT, 0x00, &dato, 1);

    while (gpio_get(PIN_CS) == 0) {
        tight_loop_contents();
    }

    sleep_ms(1);
    return dato;
}

static void print_hex_frame(const uint8_t frame[MIL1553_SPI_FRAME_SIZE]) {
    for (size_t i = 0; i < MIL1553_SPI_FRAME_SIZE; i++) {
        printf("%02X", frame[i]);

        if (i + 1 < MIL1553_SPI_FRAME_SIZE) {
            printf(" ");
        }
    }
}

static void print_invalid_frame(const uint8_t frame[MIL1553_SPI_FRAME_SIZE],
                                transport_stats_t *stats) {
    uint16_t word = ((uint16_t)frame[2] << 8) | frame[3];
    uint8_t expected_parity = mil1553_calc_odd_parity(word);
    uint8_t expected_checksum = mil1553_calc_transport_checksum(frame, MIL1553_SPI_FRAME_SIZE - 1);

    if (stats) {
        stats->invalid_frames++;
    }

    printf("RX_ERR|FRAME=");
    print_hex_frame(frame);
    printf("|ERRORS=");

    if (frame[0] != MIL1553_SPI_SYNC_BYTE) {
        printf("SYNC");
    }

    if (frame[4] != expected_parity) {
        printf("%sPARITY",
               frame[0] != MIL1553_SPI_SYNC_BYTE ? "," : "");
    }

    if (frame[6] != expected_checksum) {
        bool has_prev_error = (frame[0] != MIL1553_SPI_SYNC_BYTE) || (frame[4] != expected_parity);
        printf("%sCHECKSUM",
               has_prev_error ? "," : "");
    }

    if (stats) {
        printf("|VALID=%lu|INVALID=%lu",
               (unsigned long)stats->valid_frames,
               (unsigned long)stats->invalid_frames);
    }

    printf("\r\n");
}

static void print_sequence_gap(uint8_t expected_seq,
                               uint8_t received_seq) {
    printf("RX_WARN|SEQ_GAP|EXPECTED=%u|RECEIVED=%u\r\n",
           expected_seq,
           received_seq);
}

static void print_valid_frame(const uint8_t frame[MIL1553_SPI_FRAME_SIZE],
                              transport_stats_t *stats) {
    mil1553_word_type_t type = (mil1553_word_type_t)frame[1];
    uint16_t word = ((uint16_t)frame[2] << 8) | frame[3];
    uint8_t seq = frame[5];

    if (stats) {
        if (stats->has_last_seq) {
            uint8_t expected_seq = (uint8_t)(stats->last_seq + 1);

            if (seq != expected_seq) {
                print_sequence_gap(expected_seq, seq);
            }
        }

        stats->last_seq = seq;
        stats->has_last_seq = true;
        stats->valid_frames++;
    }

    printf("RX_OK|TYPE=0x%02X|TYPE_NAME=%s|WORD=0x%04X|PARITY=%u|SEQ=%u|FRAME=",
           frame[1],
           mil1553_word_type_name(type),
           word,
           frame[4],
           frame[5]);
    print_hex_frame(frame);
    printf("\r\n");
}

int main() {
    stdio_init_all();
    sleep_ms(4000);

    printf("RECEPTOR MIL-STD-1553 SOBRE SPI\r\n");
    printf("Modo: transporte y validacion minima\r\n");
    printf("Trama SPI: [SYNC][TYPE][WORD_MSB][WORD_LSB][PARITY][SEQ][CHECKSUM]\r\n");
    printf("Salida serial estructurada para una Raspberry externa con sniffer/dashboard\r\n\r\n");

    spi_init(SPI_PORT, 100 * 1000);
    spi_set_slave(SPI_PORT, true);
    spi_set_format(SPI_PORT, 8, SPI_CPOL_0, SPI_CPHA_0, SPI_MSB_FIRST);

    gpio_set_function(PIN_MISO, GPIO_FUNC_SPI);
    gpio_set_function(PIN_CS,   GPIO_FUNC_SPI);
    gpio_set_function(PIN_SCK,  GPIO_FUNC_SPI);
    gpio_set_function(PIN_MOSI, GPIO_FUNC_SPI);

    uint8_t rx[MIL1553_SPI_FRAME_SIZE];
    transport_stats_t stats = {0};

    while (true) {
        for (size_t i = 0; i < MIL1553_SPI_FRAME_SIZE; i++) {
            rx[i] = receive_one_byte();
        }

        if (!mil1553_validate_spi_frame(rx)) {
            print_invalid_frame(rx, &stats);
            continue;
        }

        print_valid_frame(rx, &stats);
    }
}
