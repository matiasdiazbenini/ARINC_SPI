#ifndef MIL1553_WORDS_H
#define MIL1553_WORDS_H

#include <stdbool.h>
#include <stdint.h>

typedef struct {
    uint8_t rt_address;   // [15:11]
    bool transmit;        // [10] T/R (1=RT->BC, 0=BC->RT)
    uint8_t subaddress;   // [9:5]
    uint8_t word_count;   // [4:0]
} mil1553_command_word_t;

typedef struct {
    uint8_t rt_address;                         // [15:11]
    bool message_error;                        // [10] ME
    bool service_request;                      // [8] SR
    bool broadcast_command_received;           // [5] BCR
    bool busy;                                 // [4]
    bool subsystem_flag;                       // [3] SF
    bool dynamic_bus_control_acceptance;       // [2] DBCA
    bool terminal_flag;                        // [1] TF
} mil1553_status_word_t;

uint16_t mil1553_logic_build_command_word(uint8_t rt_address,
                                          bool transmit,
                                          uint8_t subaddress,
                                          uint8_t word_count);

void mil1553_logic_decode_command_word(uint16_t word,
                                       mil1553_command_word_t *out);

uint16_t mil1553_logic_build_status_word(const mil1553_status_word_t *status);

void mil1553_logic_decode_status_word(uint16_t word,
                                      mil1553_status_word_t *out);

uint16_t mil1553_logic_build_data_word(uint16_t data);

#endif // MIL1553_WORDS_H