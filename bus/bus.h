#ifndef BUS_H
#define BUS_H

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define BUS_PIN_P 2u
#define BUS_PIN_N 3u
#define BIT_PERIOD_MS 500u

void bus_init(void);
void bus_set_tx_mode(void);
void bus_set_rx_mode(void);
void bus_idle(void);

void bus_send_bit(bool bit);
bool bus_read_bit(bool *bit);

void bus_send_byte(uint8_t byte);
bool bus_read_byte(uint8_t *byte);

void bus_send_word16(uint16_t word);
bool bus_read_word16(uint16_t *word);

#ifdef __cplusplus
}
#endif

#endif // BUS_H
