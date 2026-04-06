#include "mil1553_protocol.h"

#define BIT_MASK_5 0x1F

uint8_t mil1553_calc_transport_checksum(const uint8_t *bytes, size_t len) {
    uint8_t checksum = 0;

    for (size_t i = 0; i < len; i++) {
        checksum ^= bytes[i];
    }

    return checksum;
}

uint8_t mil1553_calc_odd_parity(uint16_t word) {
    uint8_t parity = 1;

    for (int bit = 0; bit < 16; bit++) {
        parity ^= (word >> bit) & 0x01;
    }

    return parity & 0x01;
}

uint16_t mil1553_build_command_word(uint8_t rt_address,
                                    bool transmit,
                                    uint8_t subaddress,
                                    uint8_t word_count) {
    uint16_t word = 0;

    word |= ((uint16_t)(rt_address & BIT_MASK_5)) << 11;
    word |= ((uint16_t)(transmit ? 1 : 0)) << 10;
    word |= ((uint16_t)(subaddress & BIT_MASK_5)) << 5;
    word |= (uint16_t)(word_count & BIT_MASK_5);

    return word;
}

uint16_t mil1553_build_data_word(uint16_t payload) {
    return payload;
}

uint16_t mil1553_build_status_word(const mil1553_status_fields_t *fields) {
    uint16_t word = 0;

    word |= ((uint16_t)(fields->rt_address & BIT_MASK_5)) << 11;
    word |= ((uint16_t)(fields->message_error ? 1 : 0)) << 10;
    word |= ((uint16_t)(fields->instrumentation ? 1 : 0)) << 9;
    word |= ((uint16_t)(fields->service_request ? 1 : 0)) << 8;
    word |= ((uint16_t)(fields->broadcast_received ? 1 : 0)) << 4;
    word |= ((uint16_t)(fields->busy ? 1 : 0)) << 3;
    word |= ((uint16_t)(fields->subsystem_flag ? 1 : 0)) << 2;
    word |= ((uint16_t)(fields->dynamic_bus_acceptance ? 1 : 0)) << 1;
    word |= (uint16_t)(fields->terminal_flag ? 1 : 0);

    return word;
}

void mil1553_build_spi_frame(mil1553_word_type_t type,
                             uint16_t word,
                             uint8_t sequence,
                             uint8_t frame[MIL1553_SPI_FRAME_SIZE]) {
    frame[0] = MIL1553_SPI_SYNC_BYTE;
    frame[1] = (uint8_t)type;
    frame[2] = (uint8_t)(word >> 8);
    frame[3] = (uint8_t)(word & 0xFF);
    frame[4] = mil1553_calc_odd_parity(word);
    frame[5] = sequence;
    frame[6] = mil1553_calc_transport_checksum(frame, MIL1553_SPI_FRAME_SIZE - 1);
}

bool mil1553_validate_spi_frame(const uint8_t frame[MIL1553_SPI_FRAME_SIZE]) {
    uint8_t checksum = mil1553_calc_transport_checksum(frame, MIL1553_SPI_FRAME_SIZE - 1);
    uint16_t word = ((uint16_t)frame[2] << 8) | frame[3];

    return frame[0] == MIL1553_SPI_SYNC_BYTE &&
           frame[4] == mil1553_calc_odd_parity(word) &&
           frame[6] == checksum;
}

void mil1553_decode_command_word(uint16_t word, mil1553_command_fields_t *out) {
    if (!out) {
        return;
    }

    out->rt_address = (uint8_t)((word >> 11) & BIT_MASK_5);
    out->transmit = ((word >> 10) & 0x01) != 0;
    out->subaddress = (uint8_t)((word >> 5) & BIT_MASK_5);
    out->word_count = (uint8_t)(word & BIT_MASK_5);
}

void mil1553_decode_status_word(uint16_t word, mil1553_status_fields_t *out) {
    if (!out) {
        return;
    }

    out->rt_address = (uint8_t)((word >> 11) & BIT_MASK_5);
    out->message_error = ((word >> 10) & 0x01) != 0;
    out->instrumentation = ((word >> 9) & 0x01) != 0;
    out->service_request = ((word >> 8) & 0x01) != 0;
    out->broadcast_received = ((word >> 4) & 0x01) != 0;
    out->busy = ((word >> 3) & 0x01) != 0;
    out->subsystem_flag = ((word >> 2) & 0x01) != 0;
    out->dynamic_bus_acceptance = ((word >> 1) & 0x01) != 0;
    out->terminal_flag = (word & 0x01) != 0;
}

bool mil1553_command_is_mode_code(uint8_t subaddress) {
    return subaddress == 0 || subaddress == 31;
}

bool mil1553_command_is_broadcast(uint8_t rt_address) {
    return rt_address == 31;
}

uint8_t mil1553_command_effective_word_count(const mil1553_command_fields_t *fields) {
    if (!fields || mil1553_command_is_mode_code(fields->subaddress)) {
        return 0;
    }

    return fields->word_count == 0 ? 32 : fields->word_count;
}

const char *mil1553_word_type_name(mil1553_word_type_t type) {
    switch (type) {
        case MIL1553_WORD_COMMAND:
            return "COMMAND";
        case MIL1553_WORD_DATA:
            return "DATA";
        case MIL1553_WORD_STATUS:
            return "STATUS";
        default:
            return "UNKNOWN";
    }
}
