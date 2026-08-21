#ifndef MIL1553_RX_H
#define MIL1553_RX_H

#include <stdint.h>
#include <stdbool.h>


#define MIL1553_RX_EVENT_CMD_BIT0   0x10u
#define MIL1553_RX_EVENT_CMD_BIT1   0x11u

#define MIL1553_RX_EVENT_DATA_BIT0  0x20u
#define MIL1553_RX_EVENT_DATA_BIT1  0x21u


void mil1553_rx_init(void);

bool mil1553_rx_available(void);

uint32_t mil1553_rx_get(void);


#endif