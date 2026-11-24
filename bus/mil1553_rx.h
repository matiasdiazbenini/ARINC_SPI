#ifndef MIL1553_RX_H
#define MIL1553_RX_H

#include <stdint.h>
#include <stdbool.h>


/* ============================================================
 * FORMATO DE EVENTO
 * ============================================================
 */

#define MIL1553_RX_TYPE_MASK          0x000C0000u

#define MIL1553_RX_TYPE_CMD_STATUS    0x00040000u
#define MIL1553_RX_TYPE_DATA          0x00080000u

#define MIL1553_RX_PAYLOAD_MASK       0x0001FFFFu

#define MIL1553_RX_WORD_SHIFT         1u
#define MIL1553_RX_WORD_MASK          0x0000FFFFu

#define MIL1553_RX_PARITY_MASK        0x00000001u


/* ============================================================
 * INICIALIZACION
 * ============================================================
 */

void mil1553_rx_init(void);


/* ============================================================
 * ACCESO GENERICO
 * ============================================================
 */

bool mil1553_rx_available(void);

uint32_t mil1553_rx_get(void);


/* ============================================================
 * CLEAR COMPLETO
 * ============================================================
 */

void mil1553_rx_clear(void);


/* ============================================================
 * CMD / STATUS
 * ============================================================
 */

bool mil1553_rx_cmd_status_available(void);

uint32_t mil1553_rx_get_cmd_status(void);


/* ============================================================
 * DATA GENERICO
 * ============================================================
 */

bool mil1553_rx_data_available(void);

uint32_t mil1553_rx_get_data(void);


/* ============================================================
 * DATA BC -> RT
 *
 * SM1
 * GP3 / N
 * ADQUISICION DIRECTA
 * ============================================================
 */

bool mil1553_rx_data_bc_to_rt_available(void);

uint32_t mil1553_rx_get_data_bc_to_rt(void);


/* ============================================================
 * DATA RT -> BC
 *
 * SM2
 * GP3 / N
 * GATE POR IRQ
 * ============================================================
 */

bool mil1553_rx_data_rt_to_bc_available(void);

uint32_t mil1553_rx_get_data_rt_to_bc(void);


/* ============================================================
 * REARME DATA
 * ============================================================
 */

void mil1553_rx_data_rearm(void);
void mil1553_rx_select_rt_to_bc(void);


/* ============================================================
 * REARME RAPIDO CMD / STATUS
 * ============================================================
 */

void mil1553_rx_cmd_status_rearm(void);


/* ============================================================
 * DIAGNOSTICO
 * ============================================================
 */

void mil1553_rx_data_disable(void);


#endif