#ifndef MIL1553_RX_H
#define MIL1553_RX_H

#include <stdint.h>
#include <stdbool.h>

void mil1553_rx_init(void);

bool mil1553_rx_available(void);

uint32_t mil1553_rx_get(void);

#endif