#ifndef MIL1553_RX_H
#define MIL1553_RX_H

#include <stdint.h>
#include <stdbool.h>


/*
 * Evento entregado al Cortex:
 *
 * bits 19..18:
 *
 *      01 -> CMD / STATUS
 *      10 -> DATA
 *
 * bits 16..1:
 *
 *      palabra de 16 bits
 *
 * bit 0:
 *
 *      paridad recibida
 */

#define MIL1553_RX_TYPE_MASK          0x000C0000u

#define MIL1553_RX_TYPE_CMD_STATUS    0x00040000u
#define MIL1553_RX_TYPE_DATA          0x00080000u

#define MIL1553_RX_PAYLOAD_MASK       0x0001FFFFu

#define MIL1553_RX_WORD_SHIFT         1u
#define MIL1553_RX_WORD_MASK          0x0000FFFFu

#define MIL1553_RX_PARITY_MASK        0x00000001u


void mil1553_rx_init(void);

bool mil1553_rx_available(void);

uint32_t mil1553_rx_get(void);

/*
 * Vaciar las FIFO de recepción.
 *
 * Se utiliza para descartar palabras propias
 * observadas durante una transmisión.
 */
void mil1553_rx_clear(void);
/*
 * Acceso independiente a las dos FIFO.
 *
 * El Bus Monitor utiliza estas funciones
 * para conservar la secuencia lógica de
 * cada transacción.
 */

bool mil1553_rx_cmd_status_available(void);
bool mil1553_rx_data_available(void);

uint32_t mil1553_rx_get_cmd_status(void);
uint32_t mil1553_rx_get_data(void);

void mil1553_rx_data_rearm(void);

void mil1553_rx_data_disable(void);

#endif