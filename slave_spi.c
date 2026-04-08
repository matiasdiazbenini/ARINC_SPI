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

typedef struct {
    uint32_t message_id;
    uint32_t completed_messages;
    uint32_t warning_messages;
    bool active;
    bool has_warning;
    bool transmit;
    bool is_mode_code;
    bool is_broadcast;
    bool status_seen;
    uint8_t rt_address;
    uint8_t subaddress;
    uint8_t mode_code;
    uint8_t expected_data_words;
    uint8_t seen_data_words;
} message_context_t;

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
    fflush(stdout);
}

static void print_sequence_gap(uint8_t expected_seq,
                               uint8_t received_seq) {
    printf("RX_WARN|SEQ_GAP|EXPECTED=%u|RECEIVED=%u\r\n",
           expected_seq,
           received_seq);
    fflush(stdout);
}

static void reset_message_context(message_context_t *context) {
    if (!context) {
        return;
    }

    context->active = false;
    context->has_warning = false;
    context->transmit = false;
    context->is_mode_code = false;
    context->is_broadcast = false;
    context->status_seen = false;
    context->rt_address = 0;
    context->subaddress = 0;
    context->mode_code = 0;
    context->expected_data_words = 0;
    context->seen_data_words = 0;
}

static void print_message_warning(const message_context_t *context,
                                  const char *warning) {
    printf("RX_WARN|MSG_ID=%lu|WARNING=%s",
           context ? (unsigned long)context->message_id : 0UL,
           warning);

    if (context && context->active) {
        printf("|DIR=%s|RT=%u|SA=%u",
               context->transmit ? "RT_TO_BC" : "BC_TO_RT",
               context->rt_address,
               context->subaddress);
    }

    printf("\r\n");
    fflush(stdout);
}

static void mark_message_warning(message_context_t *context) {
    if (context) {
        context->has_warning = true;
    }
}

static void start_message_context(message_context_t *context,
                                  const mil1553_command_fields_t *command) {
    if (!context || !command) {
        return;
    }

    context->message_id++;
    context->active = true;
    context->has_warning = false;
    context->transmit = command->transmit;
    context->is_mode_code = mil1553_command_is_mode_code(command->subaddress);
    context->is_broadcast = mil1553_command_is_broadcast(command->rt_address);
    context->status_seen = false;
    context->rt_address = command->rt_address;
    context->subaddress = command->subaddress;
    context->mode_code = command->word_count;
    context->expected_data_words = mil1553_command_effective_word_count(command);
    context->seen_data_words = 0;

    printf("RX_MSG_START|MSG_ID=%lu|DIR=%s|RT=%u|SA=%u|BROADCAST=%u|MODE_CODE=%u|MODE_VALUE=%u|DATA_EXPECTED=%u\r\n",
           (unsigned long)context->message_id,
           context->transmit ? "RT_TO_BC" : "BC_TO_RT",
           context->rt_address,
           context->subaddress,
           context->is_broadcast ? 1 : 0,
           context->is_mode_code ? 1 : 0,
           context->mode_code,
           context->expected_data_words);
    fflush(stdout);
}

static bool message_is_complete(const message_context_t *context) {
    if (!context || !context->active) {
        return false;
    }

    if (context->is_mode_code) {
        return context->status_seen || context->expected_data_words == 0;
    }

    if (context->transmit) {
        return context->status_seen &&
               context->seen_data_words >= context->expected_data_words;
    }

    if (context->is_broadcast) {
        return context->seen_data_words >= context->expected_data_words;
    }

    return context->status_seen &&
           context->seen_data_words >= context->expected_data_words;
}

static void finalize_message_context(message_context_t *context,
                                     bool warning,
                                     const char *result) {
    if (!context || !context->active) {
        return;
    }

    bool final_warning = warning || context->has_warning;

    printf("RX_MSG_END|MSG_ID=%lu|RESULT=%s|DIR=%s|RT=%u|SA=%u|BROADCAST=%u|MODE_CODE=%u|DATA_SEEN=%u|DATA_EXPECTED=%u|STATUS_SEEN=%u\r\n",
           (unsigned long)context->message_id,
           final_warning ? "WARN" : result,
           context->transmit ? "RT_TO_BC" : "BC_TO_RT",
           context->rt_address,
           context->subaddress,
           context->is_broadcast ? 1 : 0,
           context->is_mode_code ? 1 : 0,
           context->seen_data_words,
           context->expected_data_words,
           context->status_seen ? 1 : 0);
    fflush(stdout);

    context->completed_messages++;

    if (final_warning) {
        context->warning_messages++;
    }

    reset_message_context(context);
}

static void handle_command_word(uint16_t word,
                                message_context_t *context) {
    mil1553_command_fields_t command;
    mil1553_decode_command_word(word, &command);

    if (context && context->active) {
        print_message_warning(context, "COMMAND_BEFORE_PREVIOUS_END");
        finalize_message_context(context, true, "ABORTED");
    }

    start_message_context(context, &command);
}

static void handle_status_word(uint16_t word,
                               message_context_t *context) {
    mil1553_status_fields_t status;
    mil1553_decode_status_word(word, &status);

    if (!context || !context->active) {
        printf("RX_WARN|WARNING=ORPHAN_STATUS|RT=%u\r\n", status.rt_address);
        fflush(stdout);
        return;
    }

    if (context->is_broadcast) {
        mark_message_warning(context);
        print_message_warning(context, "STATUS_IN_BROADCAST");
    }

    if (!context->transmit && context->seen_data_words < context->expected_data_words) {
        mark_message_warning(context);
        print_message_warning(context, "STATUS_BEFORE_ALL_DATA");
    }

    if (context->status_seen) {
        mark_message_warning(context);
        print_message_warning(context, "DUPLICATE_STATUS");
    }

    context->status_seen = true;

    printf("RX_STATUS|MSG_ID=%lu|RT=%u|ME=%u|SR=%u|BUSY=%u|TF=%u|BCR=%u\r\n",
           (unsigned long)context->message_id,
           status.rt_address,
           status.message_error ? 1 : 0,
           status.service_request ? 1 : 0,
           status.busy ? 1 : 0,
           status.terminal_flag ? 1 : 0,
           status.broadcast_received ? 1 : 0);
    fflush(stdout);

    if (message_is_complete(context)) {
        finalize_message_context(context, false, "OK");
    }
}

static void handle_data_word(message_context_t *context) {
    if (!context || !context->active) {
        printf("RX_WARN|WARNING=ORPHAN_DATA\r\n");
        fflush(stdout);
        return;
    }

    context->seen_data_words++;

    if (!context->transmit && context->status_seen) {
        mark_message_warning(context);
        print_message_warning(context, "DATA_AFTER_STATUS");
    }

    if (context->expected_data_words > 0 &&
        context->seen_data_words > context->expected_data_words) {
        mark_message_warning(context);
        print_message_warning(context, "DATA_OVERFLOW");
    }

    printf("RX_DATA|MSG_ID=%lu|INDEX=%u|EXPECTED=%u\r\n",
           (unsigned long)context->message_id,
           context->seen_data_words,
           context->expected_data_words);
    fflush(stdout);

    if (message_is_complete(context)) {
        finalize_message_context(context, false, "OK");
    }
}

static void print_valid_frame(const uint8_t frame[MIL1553_SPI_FRAME_SIZE],
                              transport_stats_t *stats,
                              message_context_t *context) {
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
    fflush(stdout);

    switch (type) {
        case MIL1553_WORD_COMMAND:
            handle_command_word(word, context);
            break;
        case MIL1553_WORD_DATA:
            handle_data_word(context);
            break;
        case MIL1553_WORD_STATUS:
            handle_status_word(word, context);
            break;
        default:
            printf("RX_WARN|WARNING=UNKNOWN_TYPE|TYPE=0x%02X\r\n", frame[1]);
            fflush(stdout);
            break;
    }
}

int main() {
    stdio_init_all();
    sleep_ms(4000);

    printf("[INFO] RECEPTOR MIL-STD-1553 SOBRE SPI\r\n");
    printf("[INFO] Modo: transporte y validacion minima\r\n");
    printf("[INFO] Trama SPI: [SYNC][TYPE][WORD_MSB][WORD_LSB][PARITY][SEQ][CHECKSUM]\r\n");
    printf("[INFO] Salida serial estructurada para Flask o Raspberry externa\r\n\r\n");
    fflush(stdout);

    spi_init(SPI_PORT, 100 * 1000);
    spi_set_slave(SPI_PORT, true);
    spi_set_format(SPI_PORT, 8, SPI_CPOL_0, SPI_CPHA_0, SPI_MSB_FIRST);

    gpio_set_function(PIN_MISO, GPIO_FUNC_SPI);
    gpio_set_function(PIN_CS,   GPIO_FUNC_SPI);
    gpio_set_function(PIN_SCK,  GPIO_FUNC_SPI);
    gpio_set_function(PIN_MOSI, GPIO_FUNC_SPI);

    uint8_t rx[MIL1553_SPI_FRAME_SIZE];
    transport_stats_t stats = {0};
    message_context_t context = {0};

    while (true) {
        for (size_t i = 0; i < MIL1553_SPI_FRAME_SIZE; i++) {
            rx[i] = receive_one_byte();
        }

        if (!mil1553_validate_spi_frame(rx)) {
            print_invalid_frame(rx, &stats);
            continue;
        }

        print_valid_frame(rx, &stats, &context);
    }
}
