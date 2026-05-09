#ifndef BUS_H
#define BUS_H

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Pines diferenciales del bus.
 */
#define BUS_PIN_P 2u
#define BUS_PIN_N 3u

/*
 * Tiempo de bit actual.
 */
#define BIT_PERIOD_US 1000u

/*
 * TX por PIO.
 */
#define BUS_USE_PIO_TX 1

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
    BUS_1553_SYNC_CMD_STATUS = 0,
    BUS_1553_SYNC_DATA       = 1
} bus_1553_sync_t;

/*
 * Inicialización y modos del bus.
 */
void bus_init(void);
void bus_set_tx_mode(void);
void bus_set_rx_mode(void);
void bus_idle(void);

/*
 * Utilidades de palabra/paridad.
 */
uint8_t bus_compute_odd_parity(uint16_t word);

void bus_send_byte(uint8_t byte);
void bus_send_word16(uint16_t word);
void bus_send_word16_parity(uint16_t word);

void bus_send_1553_word(bus_1553_sync_t sync_type, uint16_t word);
void bus_send_1553_command(uint16_t cmd);
void bus_send_1553_status(uint8_t rt_addr, bool msg_error);
void bus_send_1553_data_word(uint16_t data);

/*
 * TX de paquetes con paridad.
 */
void bus_send_command_word_parity(uint16_t cmd);

void bus_send_packet_parity(uint16_t cmd,
                            const uint16_t data[],
                            uint8_t wc);

/*
 * RX de respuestas del RT.
 */
bool bus_read_status_word_parity_pio(uint16_t *status);

bool bus_read_status_data_parity_pio(uint16_t *status,
                                     uint16_t data[],
                                     uint8_t expected_wc);

/*
 * API de alto nivel del Bus Controller.
 */
bool bc_send_to_rt(uint8_t rt_addr,
                   uint8_t subaddr,
                   const uint16_t data[],
                   uint8_t wc,
                   uint16_t *out_status);

bool bc_request_from_rt(uint8_t rt_addr,
                        uint8_t subaddr,
                        uint16_t data[],
                        uint8_t wc,
                        uint16_t *out_status);

#ifdef __cplusplus
}
#endif

#endif