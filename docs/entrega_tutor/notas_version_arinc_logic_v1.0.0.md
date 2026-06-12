# Notas de version - arinc-logic-v1.0.0

## Alcance

Esta version cierra la fase logica y temporal del proyecto PAMPA / ARINC 429.

Incluye:

- transmision bipolar RZ logica a 100 kbps;
- modo stream con rafagas de longitud variable;
- RX sin dependencia de lotes fijos ni ACK;
- SNIFFER pasivo sobre FWD y REV;
- rechazo estricto de palabras con paridad incorrecta;
- SPI slave por PIO con transacciones de 32 bytes;
- bridge HTTP restringido a loopback;
- dashboard Flask y metricas Prometheus;
- exportacion automatica de pruebas a CSV, JSON y PDF;
- diagramas internos de TX, RX, SNIFFER y Raspberry Pi 3B+.

## Perfil recomendado

```text
ARINC_SPI_TRANSFER_MODE=pio-frame
ARINC_SPI_MANUAL_CS=1
ARINC_SPI_CS_SETUP_US=100
ARINC_SPI_CS_HOLD_US=100
ARINC_SPI_HZ=8000000
ARINC_SPI_BYTE_DELAY_US=0
ARINC_SPI_FRAME_RESPONSE_DELAY_SEC=0.002
ARINC_SPI_POLL_SEC=0.006
ARINC_SPI_STATS_SEC=0.06
```

## Evidencia de aceptacion

- prueba deliberada de paridad:
  - 610.905 palabras recibidas;
  - 6.109 errores inyectados y rechazados;
  - balance de clasificacion igual a cero;
  - resultado `PASS`;
- corrida final:
  - 8 horas;
  - 58.273.540 palabras recibidas;
  - cero errores SPI operativos;
  - cero errores de paridad;
  - cero overflow y drops;
  - cero resync FWD operativos;
  - resultado `PASS`;
- frecuencia:
  - 8 MHz seleccionado como perfil operativo;
  - 9 MHz pasa la prueba corta;
  - 10 MHz es parcial;
  - 16 MHz y 50 MHz fallan con la implementacion actual.

## Compatibilidad

- El modo `byte` a 800 kHz se conserva como fallback historico.
- El modo batch/ACK se conserva para demostraciones de laboratorio.
- El modo recomendado de operacion es stream sin ACK.

## Fuera de alcance

Esta version no implementa niveles electricos ARINC 429 de campo. No incluye:

- receptor diferencial ARINC 429 dedicado;
- proteccion electrica de linea;
- aislamiento;
- entrada certificada de alta impedancia;
- PCB final ni carcasa.

Esos elementos corresponden a la siguiente fase del producto.
