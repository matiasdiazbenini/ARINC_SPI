#ifndef MIL1553_RX_H
#define MIL1553_RX_H

#include <stdint.h>
#include <stdbool.h>


/*
 * Descriptor temporal para esta etapa:
 *
 * bits 17..16:
 *
 *      01 -> CMD / STATUS
 *      10 -> DATA WORD
 *
 * bits 15..0:
 *
 *      palabra recibida
 */

#define MIL1553_RX_TYPE_MASK          0x00030000u

#define MIL1553_RX_TYPE_CMD_STATUS    0x00010000u
#define MIL1553_RX_TYPE_DATA          0x00020000u

#define MIL1553_RX_WORD_MASK          0x0000FFFFu


void mil1553_rx_init(void);

bool mil1553_rx_available(void);

uint32_t mil1553_rx_get(void);


#endif