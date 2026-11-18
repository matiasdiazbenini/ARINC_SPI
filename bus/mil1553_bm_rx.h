#ifndef MIL1553_BM_RX_H
#define MIL1553_BM_RX_H


#include <stdint.h>
#include <stdbool.h>


/* ============================================================
 * EVENTO ENTREGADO AL CORTEX
 * ============================================================
 *
 * bits 19..18:
 *
 *      01 -> CMD / STATUS
 *      10 -> DATA
 *
 * bit 17:
 *
 *      0
 *
 * bits 16..1:
 *
 *      WORD[15:0]
 *
 * bit 0:
 *
 *      parity
 *
 * ============================================================
 */

#define MIL1553_BM_RX_TYPE_MASK          0x000C0000u

#define MIL1553_BM_RX_TYPE_CMD_STATUS    0x00040000u
#define MIL1553_BM_RX_TYPE_DATA          0x00080000u

#define MIL1553_BM_RX_PAYLOAD_MASK       0x0001FFFFu

#define MIL1553_BM_RX_WORD_SHIFT         1u
#define MIL1553_BM_RX_WORD_MASK          0x0000FFFFu

#define MIL1553_BM_RX_PARITY_MASK        0x00000001u


void mil1553_bm_rx_init(void);

void mil1553_bm_rx_clear(void);

bool mil1553_bm_rx_available(void);

uint32_t mil1553_bm_rx_get(void);


#endif