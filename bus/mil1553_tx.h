#ifndef MIL1553_TX_H
#define MIL1553_TX_H

#include <stdint.h>

#include "bus.h"

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Inicializa el transmisor físico MIL-STD-1553.
 */
void mil1553_tx_init(void);

/*
 * Transmite una palabra MIL-STD-1553:
 *
 * SYNC    : 3 tiempos de bit
 * DATA    : 16 bits Manchester II
 * PARITY  : 1 bit de paridad impar
 *
 * Total: 20 tiempos de bit.
 */
void mil1553_tx_send_word(bus_1553_sync_t sync_type,
                          uint16_t word);

#ifdef __cplusplus
}
#endif

#endif /* MIL1553_TX_H */