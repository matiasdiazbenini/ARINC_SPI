#ifndef BUS_H
#define BUS_H

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define BUS_PIN_P 2u
#define BUS_PIN_N 3u

#define BIT_PERIOD_US 1000u
// 1 = TX por PIO (reversible); 0 = TX por software original.
// RX se mantiene en software en ambos casos.
#define BUS_USE_PIO_TX 1

#define SYNC_CMD_STATUS 0xF0u
#define SYNC_DATA       0x0Fu

#define SYNC_PREAMBLE_BYTE 0xAAu
#define SYNC_PREAMBLE_COUNT 2u
#define BUS_SYNC_TYPE_CMD_STATUS 1u
#define BUS_SYNC_TYPE_DATA       2u

void bus_init(void);
void bus_set_tx_mode(void);
void bus_set_rx_mode(void);
void bus_idle(void);


void bus_send_bit(bool bit);
bool bus_read_bit(bool *bit);

void bus_send_byte(uint8_t byte);
bool bus_read_byte(uint8_t *byte);

void bus_send_sync_cmd_status(void);
void bus_send_sync_data(void);
bool bus_read_sync(uint8_t *type);

void bus_send_word16(uint16_t word);
bool bus_read_word16(uint16_t *word);

bool bus_read_test_frame_pio(uint16_t *cmd, uint16_t data[], uint8_t wc);
bool bus_read_test_packet_pio(uint16_t *cmd, uint16_t data[], uint8_t wc);

uint8_t bus_compute_odd_parity(uint16_t word);
void bus_send_word16_parity(uint16_t word);
bool bus_read_word16_parity(uint16_t *word);

#ifdef __cplusplus
}
#endif

#endif
