#ifndef BUS_H
#define BUS_H

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Pines del bus.
 */
#define BUS_PIN_P 2u
#define BUS_PIN_N 3u

/*
 * Base estable actual BC/RT.
 */
#define BIT_PERIOD_US 500u

/*
 * Sync simplificados tipo 1553-like.
 */
#define SYNC_CMD_STATUS 0xF0u
#define SYNC_DATA       0x0Fu

/*
 * Command Word:
 *
 * RT  : bits 15..11
 * TR  : bit  10
 * SUB : bits 9..5
 * WC  : bits 4..0
 */
#define BUS_1553_CMD_MAKE(rt, tr, sub, wc) \
    (uint16_t)((((uint16_t)(rt)  & 0x1Fu) << 11) | \
               (((uint16_t)(tr)  & 0x01u) << 10) | \
               (((uint16_t)(sub) & 0x1Fu) << 5)  | \
               ((uint16_t)(wc)   & 0x1Fu))

#define BUS_1553_CMD_RT(cmd)   (uint8_t)(((cmd) >> 11) & 0x1Fu)
#define BUS_1553_CMD_TR(cmd)   (uint8_t)(((cmd) >> 10) & 0x01u)
#define BUS_1553_CMD_SUB(cmd)  (uint8_t)(((cmd) >> 5)  & 0x1Fu)
#define BUS_1553_CMD_WC(cmd)   (uint8_t)((cmd) & 0x1Fu)

#define BUS_1553_TR_BC_TO_RT   0u
#define BUS_1553_TR_RT_TO_BC   1u

#define BUS_1553_MAX_DATA_WORDS 31u

/*
 * Status Word:
 *
 * RT        : bits 15..11
 * MSG_ERROR : bit 10
 */
#define BUS_1553_STATUS_MAKE(rt, msg_error) \
    (uint16_t)((((uint16_t)(rt) & 0x1Fu) << 11) | \
               (((uint16_t)(msg_error) & 0x01u) << 10))

#define BUS_1553_STATUS_RT(status) \
    (uint8_t)(((status) >> 11) & 0x1Fu)

#define BUS_1553_STATUS_MSG_ERROR(status) \
    (uint8_t)(((status) >> 10) & 0x01u)

typedef enum {
    SNIFFER_EVENT_NONE = 0,
    SNIFFER_EVENT_BC_TO_RT,
    SNIFFER_EVENT_RT_TO_BC
} sniffer_event_type_t;

typedef struct {
    sniffer_event_type_t type;

    uint32_t timestamp_us;

    uint16_t cmd;
    uint16_t status;
    uint16_t data[BUS_1553_MAX_DATA_WORDS];

    uint8_t rt;
    uint8_t tr;
    uint8_t sub;
    uint8_t wc;

    uint8_t data_count;
    uint8_t msg_error;
} sniffer_event_t;

/*
 * API pública del sniffer.
 */
void bus_init(void);

bool sniffer_capture_event_data(sniffer_event_t *event);
void sniffer_print_event(const sniffer_event_t *event);
void sniffer_resync(void);

#ifdef __cplusplus
}
#endif

#endif