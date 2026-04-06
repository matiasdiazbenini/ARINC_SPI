#ifndef MIL1553_PROTOCOL_H
#define MIL1553_PROTOCOL_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define MIL1553_SPI_SYNC_BYTE 0xAA
#define MIL1553_SPI_FRAME_SIZE 7

typedef enum {
    MIL1553_WORD_COMMAND = 0x01,
    MIL1553_WORD_DATA = 0x02,
    MIL1553_WORD_STATUS = 0x03
} mil1553_word_type_t;

typedef struct {
    uint8_t rt_address;
    bool transmit;
    uint8_t subaddress;
    uint8_t word_count;
} mil1553_command_fields_t;

typedef struct {
    uint8_t rt_address;
    bool message_error;
    bool instrumentation;
    bool service_request;
    bool broadcast_received;
    bool busy;
    bool subsystem_flag;
    bool dynamic_bus_acceptance;
    bool terminal_flag;
} mil1553_status_fields_t;

uint8_t mil1553_calc_transport_checksum(const uint8_t *bytes, size_t len);
uint8_t mil1553_calc_odd_parity(uint16_t word);

uint16_t mil1553_build_command_word(uint8_t rt_address,
                                    bool transmit,
                                    uint8_t subaddress,
                                    uint8_t word_count);

uint16_t mil1553_build_data_word(uint16_t payload);

uint16_t mil1553_build_status_word(const mil1553_status_fields_t *fields);

void mil1553_build_spi_frame(mil1553_word_type_t type,
                             uint16_t word,
                             uint8_t sequence,
                             uint8_t frame[MIL1553_SPI_FRAME_SIZE]);

bool mil1553_validate_spi_frame(const uint8_t frame[MIL1553_SPI_FRAME_SIZE]);

void mil1553_decode_command_word(uint16_t word, mil1553_command_fields_t *out);
void mil1553_decode_status_word(uint16_t word, mil1553_status_fields_t *out);

const char *mil1553_word_type_name(mil1553_word_type_t type);

#endif
