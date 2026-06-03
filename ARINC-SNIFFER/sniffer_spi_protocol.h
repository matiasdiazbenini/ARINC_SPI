#ifndef SNIFFER_SPI_PROTOCOL_H
#define SNIFFER_SPI_PROTOCOL_H

#include <stdint.h>

#define SNIFFER_SPI_MAGIC_0          0xA4u
#define SNIFFER_SPI_MAGIC_1          0x29u
#define SNIFFER_SPI_PROTOCOL_VERSION 0x01u
#define SNIFFER_SPI_PACKET_SIZE      32u
#define SNIFFER_SPI_FILTER_CAPACITY  10u
#define SNIFFER_SPI_TRANSPORT_RESET  0xF0u
#define SNIFFER_SPI_TRANSPORT_IDLE   0xEEu

enum {
    SNIFFER_SPI_CMD_NOP = 0x00u,
    SNIFFER_SPI_CMD_PING = 0x01u,
    SNIFFER_SPI_CMD_POP_EVENT = 0x02u,
    SNIFFER_SPI_CMD_GET_STATS = 0x03u,
    SNIFFER_SPI_CMD_GET_FILTER = 0x04u,
    SNIFFER_SPI_CMD_SET_FILTER = 0x05u,
    SNIFFER_SPI_CMD_RESET_STATS = 0x06u,
    SNIFFER_SPI_CMD_GET_LATEST_META = 0x07u,
    SNIFFER_SPI_CMD_GET_LATEST_SLOT = 0x08u,
};

enum {
    SNIFFER_SPI_STATUS_OK = 0x00u,
    SNIFFER_SPI_STATUS_EMPTY = 0x01u,
    SNIFFER_SPI_STATUS_BAD_CRC = 0x02u,
    SNIFFER_SPI_STATUS_BAD_MAGIC = 0x03u,
    SNIFFER_SPI_STATUS_BAD_COMMAND = 0x04u,
    SNIFFER_SPI_STATUS_BAD_PAYLOAD = 0x05u,
};

enum {
    SNIFFER_SPI_RESPONSE_NONE = 0x00u,
    SNIFFER_SPI_RESPONSE_PONG = 0x10u,
    SNIFFER_SPI_RESPONSE_EVENT = 0x11u,
    SNIFFER_SPI_RESPONSE_STATS = 0x12u,
    SNIFFER_SPI_RESPONSE_FILTER = 0x13u,
    SNIFFER_SPI_RESPONSE_ACK = 0x14u,
    SNIFFER_SPI_RESPONSE_LATEST_META = 0x15u,
    SNIFFER_SPI_RESPONSE_LATEST_SLOT = 0x16u,
};

enum {
    SNIFFER_SPI_EVENT_DATA = 0x01u,
    SNIFFER_SPI_EVENT_ACK = 0x02u,
};

enum {
    SNIFFER_SPI_FILTER_MODE_WHITELIST = 0x00u,
    SNIFFER_SPI_FILTER_MODE_PASS_ALL = 0x01u,
};

#pragma pack(push, 1)
typedef struct {
    uint8_t magic[2];
    uint8_t version;
    uint8_t command;
    uint8_t status;
    uint8_t flags;
    uint16_t sequence;
    uint8_t payload[22];
    uint16_t crc16;
} sniffer_spi_packet_t;

typedef struct {
    uint8_t event_type;
    uint8_t channel;
    uint8_t label;
    uint8_t sdi;
    uint8_t ssm;
    uint8_t parity_ok;
    uint32_t raw_value;
    int32_t scaled_tenths;
    uint32_t event_counter;
    uint16_t drop_counter;
    uint16_t queue_depth;
} sniffer_spi_event_payload_t;

typedef struct {
    uint32_t received_words;
    uint32_t accepted_words;
    uint32_t filtered_words;
    uint32_t parity_errors;
    uint16_t overflow_events;
    uint16_t fwd_resync_events;
    uint16_t rev_resync_events;
} sniffer_spi_stats_payload_t;

typedef struct {
    uint8_t mode;
    uint8_t count;
    uint8_t entries[SNIFFER_SPI_FILTER_CAPACITY][2];
} sniffer_spi_filter_payload_t;

typedef struct {
    uint8_t slot_count;
    uint8_t slot_capacity;
    uint8_t flags;
    uint8_t reserved0;
    uint32_t snapshot_revision;
    uint32_t slot_evictions;
    uint32_t last_update_counter;
    uint32_t reserved1;
    uint16_t reserved2;
} sniffer_spi_latest_meta_payload_t;

typedef struct {
    uint8_t channel;
    uint8_t label;
    uint8_t sdi;
    uint8_t ssm;
    uint8_t flags;
    uint32_t raw_value;
    int32_t scaled_tenths;
    uint32_t update_counter;
    uint32_t hit_count;
    uint8_t reserved;
} sniffer_spi_latest_slot_payload_t;
#pragma pack(pop)

#endif
