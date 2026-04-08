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
#define MESSAGE_CYCLES      12
#define DEFAULT_RT_ID       3
#define BROADCAST_RT_ID     31

typedef enum {
    SIM_MSG_BC_TO_RT,
    SIM_MSG_RT_TO_BC,
    SIM_MSG_BROADCAST,
    SIM_MSG_MODE_CODE
} sim_message_kind_t;

typedef struct {
    sim_message_kind_t kind;
    uint8_t rt_address;
    uint8_t subaddress;
    const char *label;
    uint8_t word_count;
    uint8_t mode_code;
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
        case 0:
        case 31:
            return "MODE_CODE";
        default:
            return "CANAL_DESCONOCIDO";
    }
}

static const char *message_kind_name(sim_message_kind_t kind) {
    switch (kind) {
        case SIM_MSG_BC_TO_RT:
            return "BC->RT";
        case SIM_MSG_RT_TO_BC:
            return "RT->BC";
        case SIM_MSG_BROADCAST:
            return "BROADCAST";
        case SIM_MSG_MODE_CODE:
            return "MODE_CODE";
        default:
            return "UNKNOWN";
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

    if (mil1553_command_is_mode_code(decoded.subaddress)) {
        printf("COMMAND -> RT: %u | MODE_CODE: %u | SA: %u",
               decoded.rt_address,
               decoded.word_count,
               decoded.subaddress);
        return;
    }

    printf("COMMAND -> RT: %u | DIR: %s | SUBADDR: %u (%s) | WC: %u%s",
           decoded.rt_address,
           decoded.transmit ? "RT->BC" : "BC->RT",
           decoded.subaddress,
           channel_name(decoded.subaddress),
           mil1553_command_effective_word_count(&decoded),
           mil1553_command_is_broadcast(decoded.rt_address) ? " | BROADCAST" : "");
}

static void log_data_word(uint16_t word,
                          uint8_t active_subaddress,
                          uint8_t data_index,
                          uint8_t word_count) {
    printf("DATA    -> IDX: %u/%u | VALUE: 0x%04X | DEC: %u | CANAL: %s",
           data_index + 1,
           word_count,
           word,
           word,
           channel_name(active_subaddress));
}

static void log_status_word(uint16_t word) {
    mil1553_status_fields_t decoded;
    mil1553_decode_status_word(word, &decoded);

    printf("STATUS  -> RT: %u | ME:%u SR:%u BUSY:%u TF:%u BCR:%u",
           decoded.rt_address,
           decoded.message_error,
           decoded.service_request,
           decoded.busy,
           decoded.terminal_flag,
           decoded.broadcast_received);
}

static void log_spi_frame(mil1553_word_type_t type,
                          uint16_t word,
                          uint8_t sequence,
                          uint8_t active_subaddress,
                          uint8_t data_index,
                          uint8_t word_count,
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
            log_data_word(word, active_subaddress, data_index, word_count);
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
                              uint8_t active_subaddress,
                              uint8_t data_index,
                              uint8_t word_count) {
    uint8_t frame[MIL1553_SPI_FRAME_SIZE];

    mil1553_build_spi_frame(type, word, *sequence, frame);
    send_frame(frame);
    log_spi_frame(type, word, *sequence, active_subaddress, data_index, word_count, frame);

    (*sequence)++;
}

static uint16_t build_data_word_value(const logical_channel_t *channel,
                                      int cycle,
                                      uint8_t data_index) {
    return channel->base_value + (uint16_t)(cycle * channel->step) + data_index;
}

static mil1553_status_fields_t build_status_fields(uint8_t rt_address,
                                                   bool service_request,
                                                   bool broadcast_received) {
    mil1553_status_fields_t status = {
        .rt_address = rt_address,
        .message_error = false,
        .instrumentation = false,
        .service_request = service_request,
        .broadcast_received = broadcast_received,
        .busy = false,
        .subsystem_flag = false,
        .dynamic_bus_acceptance = false,
        .terminal_flag = false,
    };

    return status;
}

static void send_bc_to_rt_message(const logical_channel_t *channel,
                                  int cycle,
                                  uint8_t *sequence) {
    mil1553_status_fields_t status = build_status_fields(channel->rt_address, false, false);
    uint16_t command_word = mil1553_build_command_word(
        channel->rt_address,
        false,
        channel->subaddress,
        channel->word_count
    );
    uint16_t status_word = mil1553_build_status_word(&status);

    send_mil1553_word(MIL1553_WORD_COMMAND, command_word, sequence, channel->subaddress, 0, channel->word_count);

    for (uint8_t i = 0; i < channel->word_count; i++) {
        uint16_t data_word = mil1553_build_data_word(build_data_word_value(channel, cycle, i));
        send_mil1553_word(MIL1553_WORD_DATA, data_word, sequence, channel->subaddress, i, channel->word_count);
    }

    send_mil1553_word(MIL1553_WORD_STATUS, status_word, sequence, channel->subaddress, 0, channel->word_count);
}

static void send_rt_to_bc_message(const logical_channel_t *channel,
                                  int cycle,
                                  uint8_t *sequence) {
    mil1553_status_fields_t status = build_status_fields(channel->rt_address, true, false);
    uint16_t command_word = mil1553_build_command_word(
        channel->rt_address,
        true,
        channel->subaddress,
        channel->word_count
    );
    uint16_t status_word = mil1553_build_status_word(&status);

    send_mil1553_word(MIL1553_WORD_COMMAND, command_word, sequence, channel->subaddress, 0, channel->word_count);
    send_mil1553_word(MIL1553_WORD_STATUS, status_word, sequence, channel->subaddress, 0, channel->word_count);

    for (uint8_t i = 0; i < channel->word_count; i++) {
        uint16_t data_word = mil1553_build_data_word(build_data_word_value(channel, cycle, i));
        send_mil1553_word(MIL1553_WORD_DATA, data_word, sequence, channel->subaddress, i, channel->word_count);
    }
}

static void send_broadcast_message(const logical_channel_t *channel,
                                   int cycle,
                                   uint8_t *sequence) {
    uint16_t command_word = mil1553_build_command_word(
        BROADCAST_RT_ID,
        false,
        channel->subaddress,
        channel->word_count
    );

    send_mil1553_word(MIL1553_WORD_COMMAND, command_word, sequence, channel->subaddress, 0, channel->word_count);

    for (uint8_t i = 0; i < channel->word_count; i++) {
        uint16_t data_word = mil1553_build_data_word(build_data_word_value(channel, cycle, i));
        send_mil1553_word(MIL1553_WORD_DATA, data_word, sequence, channel->subaddress, i, channel->word_count);
    }
}

static void send_mode_code_message(const logical_channel_t *channel,
                                   uint8_t *sequence) {
    mil1553_status_fields_t status = build_status_fields(channel->rt_address, false, false);
    uint16_t command_word = mil1553_build_command_word(
        channel->rt_address,
        false,
        channel->subaddress,
        channel->mode_code
    );
    uint16_t status_word = mil1553_build_status_word(&status);

    send_mil1553_word(MIL1553_WORD_COMMAND, command_word, sequence, channel->subaddress, 0, 0);
    send_mil1553_word(MIL1553_WORD_STATUS, status_word, sequence, channel->subaddress, 0, 0);
}

int main() {
    const logical_channel_t channels[] = {
        {.kind = SIM_MSG_BC_TO_RT,  .rt_address = DEFAULT_RT_ID,   .subaddress = 1,  .label = "TEMPERATURA", .word_count = 2, .mode_code = 0,  .base_value = 0x1200, .step = 0x0003},
        {.kind = SIM_MSG_RT_TO_BC,  .rt_address = DEFAULT_RT_ID,   .subaddress = 2,  .label = "VELOCIDAD",   .word_count = 3, .mode_code = 0,  .base_value = 0x2200, .step = 0x0005},
        {.kind = SIM_MSG_BROADCAST, .rt_address = BROADCAST_RT_ID, .subaddress = 3,  .label = "PRESION_ALL", .word_count = 2, .mode_code = 0,  .base_value = 0x3200, .step = 0x0007},
        {.kind = SIM_MSG_MODE_CODE, .rt_address = DEFAULT_RT_ID,   .subaddress = 31, .label = "MODE_SYNC",   .word_count = 0, .mode_code = 2,  .base_value = 0x0000, .step = 0x0000},
    };
    const size_t channel_count = sizeof(channels) / sizeof(channels[0]);

    stdio_init_all();
    sleep_ms(3000);

    printf("MASTER - Simulador logico MIL-STD-1553 sobre SPI\n");
    printf("Trama SPI: [SYNC][TYPE][WORD_MSB][WORD_LSB][PARITY][SEQ][CHECKSUM]\n");
    printf("Mensajes soportados: BC->RT, RT->BC, BROADCAST y MODE_CODE\n\n");

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
        printf("MENSAJE %02d/%02d | TIPO: %s | RT: %u | CANAL: %s | WC: %u",
               cycle + 1,
               MESSAGE_CYCLES,
               message_kind_name(channel->kind),
               channel->rt_address,
               channel->label,
               channel->word_count);

        if (channel->kind == SIM_MSG_MODE_CODE) {
            printf(" | MC: %u", channel->mode_code);
        }

        printf("\n");

        switch (channel->kind) {
            case SIM_MSG_BC_TO_RT:
                send_bc_to_rt_message(channel, cycle, &sequence);
                break;
            case SIM_MSG_RT_TO_BC:
                send_rt_to_bc_message(channel, cycle, &sequence);
                break;
            case SIM_MSG_BROADCAST:
                send_broadcast_message(channel, cycle, &sequence);
                break;
            case SIM_MSG_MODE_CODE:
                send_mode_code_message(channel, &sequence);
                break;
            default:
                break;
        }

        printf("\n");
        sleep_ms(TX_INTERFRAME_DELAY_MS);
    }

    printf("\nFin del envio.\n");

    while (true) {
        tight_loop_contents();
    }
}
