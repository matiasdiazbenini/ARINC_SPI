#ifndef MIL1553_WORDS_H
#define MIL1553_WORDS_H

#include <stdbool.h>
#include <stdint.h>

typedef struct {
    uint8_t rt_address;  // bits 15..11
    bool transmit;       // bit 10
    uint8_t subaddress;  // bits 9..5
    uint8_t word_count;  // bits 4..0
} mil1553_command_t;

typedef struct {
    uint8_t rt_address;    // bits 15..11
    bool message_error;    // bit 10 (formato simplificado del proyecto)
    bool service_request;  // bit 8
    bool busy;             // bit 4
    bool terminal_flag;    // bit 1
} mil1553_status_t;

uint16_t mil1553_build_command(uint8_t rt,
                               bool transmit,
                               uint8_t subaddress,
                               uint8_t word_count);

bool mil1553_decode_command(uint16_t word, mil1553_command_t *cmd);

uint16_t mil1553_build_status(const mil1553_status_t *status);

bool mil1553_decode_status(uint16_t word, mil1553_status_t *status);

uint16_t mil1553_build_data(uint16_t data);

#endif // MIL1553_WORDS_H
