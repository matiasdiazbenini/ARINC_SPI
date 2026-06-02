#ifndef ARINC429_LOGIC_H
#define ARINC429_LOGIC_H

#include <stdbool.h>
#include <stdint.h>

typedef enum {
    ARINC429_LINE_NULL = 0u,
    ARINC429_LINE_LO = 1u,
    ARINC429_LINE_HI = 2u,
    ARINC429_LINE_INVALID = 3u,
} arinc429_line_symbol_t;

static inline uint8_t arinc429_reverse_u8(uint8_t value) {
    value = ((value & 0xF0u) >> 4) | ((value & 0x0Fu) << 4);
    value = ((value & 0xCCu) >> 2) | ((value & 0x33u) << 2);
    value = ((value & 0xAAu) >> 1) | ((value & 0x55u) << 1);
    return value;
}

static inline uint8_t arinc429_label_to_wire(uint8_t label) {
    return arinc429_reverse_u8(label);
}

static inline uint8_t arinc429_label_from_wire(uint8_t wire_label) {
    return arinc429_reverse_u8(wire_label);
}

static inline uint8_t arinc429_calc_odd_parity_31bits(uint32_t word_without_parity) {
    int ones = 0;

    for (int i = 0; i < 31; ++i) {
        if ((word_without_parity >> i) & 1u) {
            ++ones;
        }
    }

    return (ones % 2 == 0) ? 1u : 0u;
}

static inline uint32_t arinc429_build_word_fields(uint8_t label,
                                                  uint8_t sdi,
                                                  uint32_t data,
                                                  uint8_t ssm) {
    uint32_t word = 0u;

    word |= (uint32_t)arinc429_label_to_wire(label);
    word |= ((uint32_t)(sdi & 0x03u)) << 8;
    word |= ((uint32_t)(data & 0x7FFFFu)) << 10;
    word |= ((uint32_t)(ssm & 0x03u)) << 29;
    word |= ((uint32_t)arinc429_calc_odd_parity_31bits(word)) << 31;

    return word;
}

static inline bool arinc429_parity_check(uint32_t word) {
    const uint32_t word_31 = word & 0x7FFFFFFFu;
    const uint8_t parity_rx = (word >> 31) & 0x01u;
    const uint8_t parity_exp = arinc429_calc_odd_parity_31bits(word_31);
    return parity_rx == parity_exp;
}

static inline uint8_t arinc429_word_label(uint32_t word) {
    return arinc429_label_from_wire((uint8_t)(word & 0xFFu));
}

static inline uint8_t arinc429_word_sdi(uint32_t word) {
    return (uint8_t)((word >> 8) & 0x03u);
}

static inline uint32_t arinc429_word_data(uint32_t word) {
    return (word >> 10) & 0x7FFFFu;
}

static inline uint8_t arinc429_word_ssm(uint32_t word) {
    return (uint8_t)((word >> 29) & 0x03u);
}

#endif
