#ifndef BUS_H
#define BUS_H

#include <stdbool.h>
#include <stdint.h>

#define SYNC_CMD_STATUS 0xF0u
#define SYNC_DATA       0x0Fu

#define BUS_SYNC_TYPE_CMD_STATUS 1u
#define BUS_SYNC_TYPE_DATA       2u

#ifdef __cplusplus
extern "C" {
#endif

// Pines del bus diferencial.
#define BUS_PIN_P 2u
#define BUS_PIN_N 3u

// Periodo de bit total (ms).
#define BIT_PERIOD_MS 200u

// Inicializa GPIO del bus.
void bus_init(void);

// Modo transmision (OUTPUT).
void bus_set_tx_mode(void);

// Modo recepcion (INPUT).
void bus_set_rx_mode(void);

// Fuerza estado IDLE: P=0, N=0.
void bus_idle(void);

// Manchester:
// bit 1 -> HIGH -> LOW
// bit 0 -> LOW  -> HIGH
void bus_send_bit(bool bit);

// Lee un bit Manchester. Devuelve false si el patron no es valido.
bool bus_read_bit(bool *bit);

// Envio/recepcion de byte (MSB primero).
void bus_send_byte(uint8_t byte);
bool bus_read_byte(uint8_t *byte);

// Aproximacion de laboratorio al sync MIL:
// se modela con bytes distintos para command/status y data.
// Mas adelante se reemplazara por el sincronismo MIL real.
#define SYNC_CMD_STATUS 0xF0u
#define SYNC_DATA       0x0Fu

typedef enum {
    CMD_STATUS = 0u,
    DATA = 1u,
} bus_sync_type_t;

void bus_send_sync_cmd_status(void);
void bus_send_sync_data(void);
bool bus_read_sync(uint8_t *type);

// Envio/recepcion de word de 16 bits (MSB primero).
void bus_send_word16(uint16_t word);
bool bus_read_word16(uint16_t *word);

// Calcula bit de paridad impar para una palabra de 16 bits.
// Devuelve 0 o 1 tal que (word + parity) tenga cantidad impar de unos.
uint8_t bus_compute_odd_parity(uint16_t word);

// Envia 16 bits MSB primero y luego 1 bit de paridad impar.
void bus_send_word16_parity(uint16_t word);

// Lee 16 bits + 1 bit de paridad y valida paridad impar.
// Devuelve false si hay error de lectura o paridad invalida.
bool bus_read_word16_parity(uint16_t *word);

#ifdef __cplusplus
}
#endif

#endif // BUS_H
