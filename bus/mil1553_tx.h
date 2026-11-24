#ifndef MIL1553_TX_H
#define MIL1553_TX_H

#include <stdint.h>

#include "bus.h"


#ifdef __cplusplus
extern "C" {
#endif


/* ============================================================
 * INICIALIZACION
 * ============================================================
 */

void mil1553_tx_init(void);


/* ============================================================
 * TRANSMISION
 * ============================================================
 */

void mil1553_tx_send_word(
    bus_1553_sync_t sync_type,
    uint16_t word
);


void mil1553_tx_send_word_blocking(
    bus_1553_sync_t sync_type,
    uint16_t word
);


/* ============================================================
 * ESPERAR FINALIZACION FISICA
 * ============================================================
 */

void mil1553_tx_wait_idle(void);


#ifdef __cplusplus
}
#endif


#endif /* MIL1553_TX_H */