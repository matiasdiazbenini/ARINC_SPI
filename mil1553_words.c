#include "mil1553_words.h"

#define MIL1553_MASK_5BITS 0x1Fu

uint16_t mil1553_build_command(uint8_t rt,
                               bool transmit,
                               uint8_t subaddress,
                               uint8_t word_count) {
    uint16_t word = 0;

    word |= (uint16_t)((rt & MIL1553_MASK_5BITS) << 11);
    word |= (uint16_t)((transmit ? 1u : 0u) << 10);
    word |= (uint16_t)((subaddress & MIL1553_MASK_5BITS) << 5);
    word |= (uint16_t)(word_count & MIL1553_MASK_5BITS);

    return word;
}

bool mil1553_decode_command(uint16_t word, mil1553_command_t *cmd) {
    if (cmd == NULL) {
        return false;
    }

    cmd->rt_address = (uint8_t)((word >> 11) & MIL1553_MASK_5BITS);
    cmd->transmit = ((word >> 10) & 0x01u) != 0u;
    cmd->subaddress = (uint8_t)((word >> 5) & MIL1553_MASK_5BITS);
    cmd->word_count = (uint8_t)(word & MIL1553_MASK_5BITS);

    return true;
}

uint16_t mil1553_build_status(const mil1553_status_t *status) {
    uint16_t word = 0;

    if (status == NULL) {
        return 0;
    }

    word |= (uint16_t)((status->rt_address & MIL1553_MASK_5BITS) << 11);
    word |= (uint16_t)((status->message_error ? 1u : 0u) << 10);
    word |= (uint16_t)((status->service_request ? 1u : 0u) << 8);
    word |= (uint16_t)((status->busy ? 1u : 0u) << 4);
    word |= (uint16_t)((status->terminal_flag ? 1u : 0u) << 1);

    return word;
}

bool mil1553_decode_status(uint16_t word, mil1553_status_t *status) {
    if (status == NULL) {
        return false;
    }

    status->rt_address = (uint8_t)((word >> 11) & MIL1553_MASK_5BITS);
    status->message_error = ((word >> 10) & 0x01u) != 0u;
    status->service_request = ((word >> 8) & 0x01u) != 0u;
    status->busy = ((word >> 4) & 0x01u) != 0u;
    status->terminal_flag = ((word >> 1) & 0x01u) != 0u;

    return true;
}

uint16_t mil1553_build_data(uint16_t data) {
    return data;
}
