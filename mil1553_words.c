#include "mil1553_words.h"

#define BIT_MASK_5 0x1Fu

uint16_t mil1553_logic_build_command_word(uint8_t rt_address,
                                          bool transmit,
                                          uint8_t subaddress,
                                          uint8_t word_count) {
    uint16_t word = 0;

    word |= ((uint16_t)(rt_address & BIT_MASK_5)) << 11;
    word |= ((uint16_t)(transmit ? 1u : 0u)) << 10;
    word |= ((uint16_t)(subaddress & BIT_MASK_5)) << 5;
    word |= (uint16_t)(word_count & BIT_MASK_5);

    return word;
}

void mil1553_logic_decode_command_word(uint16_t word,
                                       mil1553_command_word_t *out) {
    if (!out) {
        return;
    }

    out->rt_address = (uint8_t)((word >> 11) & BIT_MASK_5);
    out->transmit = ((word >> 10) & 0x01u) != 0u;
    out->subaddress = (uint8_t)((word >> 5) & BIT_MASK_5);
    out->word_count = (uint8_t)(word & BIT_MASK_5);
}

uint16_t mil1553_logic_build_status_word(const mil1553_status_word_t *status) {
    if (!status) {
        return 0;
    }

    uint16_t word = 0;
    word |= ((uint16_t)(status->rt_address & BIT_MASK_5)) << 11;
    word |= ((uint16_t)(status->message_error ? 1u : 0u)) << 10;
    word |= ((uint16_t)(status->service_request ? 1u : 0u)) << 8;
    word |= ((uint16_t)(status->broadcast_command_received ? 1u : 0u)) << 5;
    word |= ((uint16_t)(status->busy ? 1u : 0u)) << 4;
    word |= ((uint16_t)(status->subsystem_flag ? 1u : 0u)) << 3;
    word |= ((uint16_t)(status->dynamic_bus_control_acceptance ? 1u : 0u)) << 2;
    word |= ((uint16_t)(status->terminal_flag ? 1u : 0u)) << 1;

    // Bits reservados [9], [7:6] y [0] se mantienen en cero.
    return word;
}

void mil1553_logic_decode_status_word(uint16_t word,
                                      mil1553_status_word_t *out) {
    if (!out) {
        return;
    }

    out->rt_address = (uint8_t)((word >> 11) & BIT_MASK_5);
    out->message_error = ((word >> 10) & 0x01u) != 0u;
    out->service_request = ((word >> 8) & 0x01u) != 0u;
    out->broadcast_command_received = ((word >> 5) & 0x01u) != 0u;
    out->busy = ((word >> 4) & 0x01u) != 0u;
    out->subsystem_flag = ((word >> 3) & 0x01u) != 0u;
    out->dynamic_bus_control_acceptance = ((word >> 2) & 0x01u) != 0u;
    out->terminal_flag = ((word >> 1) & 0x01u) != 0u;
}

uint16_t mil1553_logic_build_data_word(uint16_t data) {
    return data;
}