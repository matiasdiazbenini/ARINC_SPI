#include <stdio.h>
#include <stdint.h>
#include "pico/stdlib.h"
#include "hardware/spi.h"
#include "mil1553_protocol.h"

#define SPI_PORT spi0

#define PIN_MISO 16
#define PIN_CS   17
#define PIN_SCK  18
#define PIN_MOSI 19

#define SPI_BAUDRATE_HZ     (100 * 1000)
#define TX_BURST_DELAY_MS   2
#define TX_INTERFRAME_DELAY_MS 250
#define MESSAGE_CYCLES      20
#define REMOTE_TERMINAL_ID  3

typedef struct {
    uint8_t subaddress;
    const char *label;
    uint16_t base_value;
    uint16_t step;
} logical_channel_t;

static inline void cs_select() {
    gpio_put(PIN_CS, 0);
}

static inline void cs_deselect() {
    gpio_put(PIN_CS, 1);
}

static void send_one_byte(uint8_t b) {
    cs_select();
    sleep_us(100);
    spi_write_blocking(SPI_PORT, &b, 1);
    sleep_us(100);
    cs_deselect();
    sleep_ms(TX_BURST_DELAY_MS);
}

static void send_frame(const uint8_t frame[MIL1553_SPI_FRAME_SIZE]) {
    for (size_t i = 0; i < MIL1553_SPI_FRAME_SIZE; i++) {
        send_one_byte(frame[i]);
    }
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

static void log_command_word(uint16_t word) {
    mil1553_command_fields_t decoded;
    mil1553_decode_command_word(word, &decoded);

    printf("COMMAND -> RT: %u | T/R: %s | SUBADDR: %u (%s) | WC: %u",
           decoded.rt_address,
           decoded.transmit ? "TX" : "RX",
           decoded.subaddress,
           channel_name(decoded.subaddress),
           decoded.word_count);
}

static void log_data_word(uint16_t word, uint8_t active_subaddress) {
    printf("DATA    -> VALUE: 0x%04X | DEC: %u | CANAL: %s",
           word,
           word,
           channel_name(active_subaddress));
}

static void log_status_word(uint16_t word) {
    mil1553_status_fields_t decoded;
    mil1553_decode_status_word(word, &decoded);

    printf("STATUS  -> RT: %u | ME:%u SR:%u BUSY:%u TF:%u",
           decoded.rt_address,
           decoded.message_error,
           decoded.service_request,
           decoded.busy,
           decoded.terminal_flag);
}

static void log_spi_frame(mil1553_word_type_t type,
                          uint16_t word,
                          uint8_t sequence,
                          uint8_t active_subaddress,
                          const uint8_t frame[MIL1553_SPI_FRAME_SIZE]) {
    printf("SEQ %02u | %s | WORD: 0x%04X | PARITY: %u | FRAME: ",
           sequence,
           mil1553_word_type_name(type),
           word,
           frame[4]);
    print_hex_frame(frame);
    printf(" | ");

    switch (type) {
        case MIL1553_WORD_COMMAND:
            log_command_word(word);
            break;
        case MIL1553_WORD_DATA:
            log_data_word(word, active_subaddress);
            break;
        case MIL1553_WORD_STATUS:
            log_status_word(word);
            break;
        default:
            printf("TIPO_NO_SOPORTADO");
            break;
    }

    printf("\n");
}

static void send_mil1553_word(mil1553_word_type_t type,
                              uint16_t word,
                              uint8_t *sequence,
                              uint8_t active_subaddress) {
    uint8_t frame[MIL1553_SPI_FRAME_SIZE];

    mil1553_build_spi_frame(type, word, *sequence, frame);
    send_frame(frame);
    log_spi_frame(type, word, *sequence, active_subaddress, frame);

    (*sequence)++;
}

int main() {
    const logical_channel_t channels[] = {
        {.subaddress = 1, .label = "TEMPERATURA", .base_value = 0x1200, .step = 0x0003},
        {.subaddress = 2, .label = "VELOCIDAD",   .base_value = 0x2200, .step = 0x0005},
        {.subaddress = 3, .label = "PRESION",     .base_value = 0x3200, .step = 0x0007},
    };
    const size_t channel_count = sizeof(channels) / sizeof(channels[0]);

    stdio_init_all();
    sleep_ms(3000);

    printf("MASTER - Simulador logico MIL-STD-1553 sobre SPI\n");
    printf("Trama SPI: [SYNC][TYPE][WORD_MSB][WORD_LSB][PARITY][SEQ][CHECKSUM]\n");
    printf("Secuencia por mensaje: COMMAND -> DATA -> STATUS\n\n");

    spi_init(SPI_PORT, SPI_BAUDRATE_HZ);
    spi_set_format(SPI_PORT, 8, SPI_CPOL_0, SPI_CPHA_0, SPI_MSB_FIRST);

    gpio_set_function(PIN_MISO, GPIO_FUNC_SPI);
    gpio_set_function(PIN_SCK,  GPIO_FUNC_SPI);
    gpio_set_function(PIN_MOSI, GPIO_FUNC_SPI);

    gpio_init(PIN_CS);
    gpio_set_dir(PIN_CS, GPIO_OUT);
    cs_deselect();

    uint8_t sequence = 0;

    for (int cycle = 0; cycle < MESSAGE_CYCLES; cycle++) {
        const logical_channel_t *channel = &channels[cycle % channel_count];
        mil1553_status_fields_t status = {
            .rt_address = REMOTE_TERMINAL_ID,
            .message_error = false,
            .instrumentation = false,
            .service_request = false,
            .broadcast_received = false,
            .busy = false,
            .subsystem_flag = false,
            .dynamic_bus_acceptance = false,
            .terminal_flag = false,
        };

        uint16_t command_word = mil1553_build_command_word(
            REMOTE_TERMINAL_ID,
            false,
            channel->subaddress,
            1
        );
        uint16_t data_word = mil1553_build_data_word(channel->base_value + (uint16_t)(cycle * channel->step));
        uint16_t status_word = mil1553_build_status_word(&status);

        printf("MENSAJE %02d/%02d | CANAL: %s\n", cycle + 1, MESSAGE_CYCLES, channel->label);

        send_mil1553_word(MIL1553_WORD_COMMAND, command_word, &sequence, channel->subaddress);
        send_mil1553_word(MIL1553_WORD_DATA, data_word, &sequence, channel->subaddress);
        send_mil1553_word(MIL1553_WORD_STATUS, status_word, &sequence, channel->subaddress);

        printf("\n");
        sleep_ms(TX_INTERFRAME_DELAY_MS);
    }

    printf("\nFin del envio.\n");

    while (true) {
        tight_loop_contents();
    }
}
