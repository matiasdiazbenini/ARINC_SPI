#ifndef BUS_H
#define BUS_H

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

// Diferencial logico del bus (P/N) sobre RP2040.
#define BUS_PIN_P 2u
#define BUS_PIN_N 3u

// Inicializa GPIO del bus y deja el nodo en modo RX (alta impedancia).
void bus_init(void);

// Habilita conduccion del bus (TX): ambos pines pasan a OUTPUT.
void bus_set_tx_mode(void);

// Deshabilita conduccion del bus (RX): ambos pines pasan a INPUT (alta impedancia).
void bus_set_rx_mode(void);

// Envia 1 bit Manchester-II (1 Mbps, 1 us por bit).
// bit=1 -> BUS_P: alto->bajo; bit=0 -> BUS_P: bajo->alto.
// BUS_N siempre se mantiene complementario de BUS_P.
void bus_send_bit(int bit);

// Recibe 1 bit Manchester-II leyendo la transicion de mitad de bit.
// Devuelve:
//   0 o 1 si el bit es valido
//  -1 si hay error de nivel/temporizacion (sin transicion valida).
// Nota: esta funcion asume que el llamador ya esta alineado al inicio del bit.
int bus_receive_bit(void);

// Envia una secuencia simple de sincronizacion de palabra para banco.
// Esta secuencia NO es el sync final MIL-STD-1553.
void bus_send_sync(void);

// Espera y detecta la secuencia simple de sincronizacion.
// timeout_us: tiempo maximo de espera en microsegundos.
// Devuelve true si detecta sync valido, false por timeout.
bool bus_wait_sync(uint32_t timeout_us);

#ifdef __cplusplus
}
#endif

#endif // BUS_H
