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
    uint8_t rt_address;
    uint8_t subaddress;
    uint8_t word_count;
    bool transmit;
    bool available;
} mil1553_message_context_t;

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

static const char *channel_name(uint8_t subaddress) {
    switch (subaddress) {
        case 1:
            return "TEMPERATURA";
        case 2:
            return "VELOCIDAD";
        case 3:
            return "PRESION";
        default:
            return "CANAL_DESCONOCIDO";
    }
}

static void print_hex_frame(const uint8_t frame[MIL1553_SPI_FRAME_SIZE]) {
    for (size_t i = 0; i < MIL1553_SPI_FRAME_SIZE; i++) {
        printf("%02X", frame[i]);

        if (i + 1 < MIL1553_SPI_FRAME_SIZE) {
            printf(" ");
        }
    }
}

static void print_invalid_reason(const uint8_t frame[MIL1553_SPI_FRAME_SIZE]) {
    uint16_t word = ((uint16_t)frame[2] << 8) | frame[3];
    uint8_t expected_parity = mil1553_calc_odd_parity(word);
    uint8_t expected_checksum = mil1553_calc_transport_checksum(frame, MIL1553_SPI_FRAME_SIZE - 1);

    printf("TRAMA INVALIDA -> FRAME: ");
    print_hex_frame(frame);
    printf(" | ERRORES:");

    if (frame[0] != MIL1553_SPI_SYNC_BYTE) {
        printf(" SYNC");
    }

    if (frame[4] != expected_parity) {
        printf(" PARITY");
    }

    if (frame[6] != expected_checksum) {
        printf(" CHECKSUM");
    }

    printf("\r\n");
}

static void print_command_word(uint16_t word, mil1553_message_context_t *context) {
    mil1553_command_fields_t decoded;
    mil1553_decode_command_word(word, &decoded);

    if (context) {
        context->rt_address = decoded.rt_address;
        context->subaddress = decoded.subaddress;
        context->word_count = decoded.word_count;
        context->transmit = decoded.transmit;
        context->available = true;
    }

    printf("MATCH -> TYPE: 0x%02X (%s) | RT: %u | T/R: %s | SUBADDR: %u (%s) | WC: %u",
           MIL1553_WORD_COMMAND,
           mil1553_word_type_name(MIL1553_WORD_COMMAND),
           decoded.rt_address,
           decoded.transmit ? "TX" : "RX",
           decoded.subaddress,
           channel_name(decoded.subaddress),
           decoded.word_count);
}

static void print_data_word(uint16_t word, const mil1553_message_context_t *context) {
    printf("MATCH -> TYPE: 0x%02X (%s) | DATA_WORD: 0x%04X | DEC: %u",
           MIL1553_WORD_DATA,
           mil1553_word_type_name(MIL1553_WORD_DATA),
           word,
           word);

    if (context && context->available) {
        printf(" | RT: %u | SUBADDR: %u (%s) | T/R: %s",
               context->rt_address,
               context->subaddress,
               channel_name(context->subaddress),
               context->transmit ? "TX" : "RX");
    }
}

static void print_status_word(uint16_t word) {
    mil1553_status_fields_t decoded;
    mil1553_decode_status_word(word, &decoded);

    printf("MATCH -> TYPE: 0x%02X (%s) | RT: %u | ME:%u SR:%u BUSY:%u TF:%u",
           MIL1553_WORD_STATUS,
           mil1553_word_type_name(MIL1553_WORD_STATUS),
           decoded.rt_address,
           decoded.message_error,
           decoded.service_request,
           decoded.busy,
           decoded.terminal_flag);
}

static void print_valid_frame(const uint8_t frame[MIL1553_SPI_FRAME_SIZE],
                              mil1553_message_context_t *context) {
    mil1553_word_type_t type = (mil1553_word_type_t)frame[1];
    uint16_t word = ((uint16_t)frame[2] << 8) | frame[3];

    switch (type) {
        case MIL1553_WORD_COMMAND:
            print_command_word(word, context);
            break;
        case MIL1553_WORD_DATA:
            print_data_word(word, context);
            break;
        case MIL1553_WORD_STATUS:
            print_status_word(word);
            break;
        default:
            printf("MATCH -> TYPE: 0x%02X (%s) | WORD: 0x%04X",
                   frame[1],
                   mil1553_word_type_name(type),
                   word);
            break;
    }

    printf(" | PARITY: %u | SEQ: %u | FRAME: ",
           frame[4],
           frame[5]);
    print_hex_frame(frame);
    printf("\r\n");
}

int main() {
    stdio_init_all();
    sleep_ms(4000);

    printf("SNIFFER LOGICO MIL-STD-1553 SOBRE SPI\r\n");
    printf("Trama SPI: [SYNC][TYPE][WORD_MSB][WORD_LSB][PARITY][SEQ][CHECKSUM]\r\n");
    printf("Imprimiendo todas las palabras validas\r\n\r\n");

    spi_init(SPI_PORT, 100 * 1000);
    spi_set_slave(SPI_PORT, true);
    spi_set_format(SPI_PORT, 8, SPI_CPOL_0, SPI_CPHA_0, SPI_MSB_FIRST);

    gpio_set_function(PIN_MISO, GPIO_FUNC_SPI);
    gpio_set_function(PIN_CS,   GPIO_FUNC_SPI);
    gpio_set_function(PIN_SCK,  GPIO_FUNC_SPI);
    gpio_set_function(PIN_MOSI, GPIO_FUNC_SPI);

    uint8_t rx[MIL1553_SPI_FRAME_SIZE];
    mil1553_message_context_t context = {0};

    while (true) {
        for (size_t i = 0; i < MIL1553_SPI_FRAME_SIZE; i++) {
            rx[i] = receive_one_byte();
        }

        if (!mil1553_validate_spi_frame(rx)) {
            print_invalid_reason(rx);
            continue;
        }

        print_valid_frame(rx, &context);
    }
}
