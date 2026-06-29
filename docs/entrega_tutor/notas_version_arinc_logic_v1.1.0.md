# Notas de version - arinc-logic-v1.1.0

## Alcance

Esta version consolida la fase logica del proyecto PAMPA / ARINC 429 despues
de incorporar autorate, `DRDY`, recuperacion V5, supervisor y dashboard
simplificado.

La version mantiene el alcance de laboratorio: reproduce la logica, palabra,
temporizacion y captura pasiva ARINC sobre niveles GPIO de `3.3 V`. Todavia no
implementa la interfaz electrica ARINC 429 de campo.

## Cambios principales respecto de v1.0.0

- TX autorate:
  - `arinc_tx_arinc429_logic_stream_autorate`
  - seleccion de arranque entre `100 kbps` y `12.5 kbps`
  - target forzado `arinc_tx_arinc429_logic_stream_12k5`
- RX stream:
  - recibe sin conocer previamente la velocidad
  - conserva el flujo sin dependencia de lotes fijos ni ACK
- SNIFFER:
  - deteccion autonoma de velocidad FWD
  - exportacion de `detected_bit_rate_bps` y `detected_bit_rate_txt`
  - recuperacion V5 ante reinicio/desconexion
  - paridad estricta
- SPI:
  - `pio-frame` a `8 MHz`
  - transacciones completas de `32 bytes`
  - `DRDY` como disparador principal de snapshot
  - fallback por timeout
- 3B+:
  - `arinc-supervisor.service`
  - endpoint de recuperacion SPI
  - metricas Prometheus del supervisor y bitrate detectado
- Dashboard:
  - interfaz mas limpia, operativa y formal
  - textos explicativos removidos de la vista principal

## Perfil recomendado

```text
ARINC_SPI_TRANSFER_MODE=pio-frame
ARINC_SPI_REQUEST_MODE=drdy
ARINC_SPI_DRDY_GPIO=25
ARINC_SPI_MANUAL_CS=1
ARINC_SPI_CS_GPIO=8
ARINC_SPI_CS_SETUP_US=100
ARINC_SPI_CS_HOLD_US=100
ARINC_SPI_HZ=8000000
ARINC_SPI_BYTE_DELAY_US=0
ARINC_SPI_FRAME_RESPONSE_DELAY_SEC=0.002
ARINC_SPI_POLL_SEC=0.006
ARINC_SPI_STATS_SEC=0.06
```

## Evidencia de aceptacion

### Osciloscopio autorate

La validacion instrumental final del 2026-06-29 registro:

- a `100 kbps`:
  - palabra completa de `320 us`;
  - palabra mas gap visible de `364 us`;
  - gap visible de `44 us`;
- a `12.5 kbps`:
  - media celda activa de `40 us`;
  - bit completo de `80 us`;
  - gap visible de `360 us`;
- relacion de gaps visibles:
  - `360/44 = 8.18`, coherente con la relacion teorica `8:1`.

### Corrida 12.5 kbps

Sesion:

```text
20260619_212806_corrida8h_12k5
```

Resultado:

- duracion: `28801.451 s`;
- muestras: `5760`;
- velocidad detectada: `12500 bps`;
- palabras recibidas: `9876638`;
- palabras aceptadas: `2962992`;
- palabras filtradas: `6913646`;
- tasa media aceptada: `102.876 words/s`;
- balance de clasificacion: `0`;
- errores de paridad: `0`;
- errores SPI operativos: `0`;
- overflow: `0`;
- drops SPI: `0`;
- resync FWD operativos: `0`;
- supervisor: `RUNNING`, `ok`;
- veredicto: `PASS`.

### Corrida 100 kbps

Sesion:

```text
20260620_065154_corrida8h_100k
```

Resultado:

- duracion: `28801.407 s`;
- muestras: `5760`;
- velocidad detectada: `100000 bps`;
- palabras recibidas: `76707606`;
- palabras aceptadas: `23012283`;
- palabras filtradas: `53695323`;
- tasa media aceptada: `798.999 words/s`;
- balance de clasificacion: `0`;
- errores de paridad: `0`;
- errores SPI operativos: `0`;
- overflow: `0`;
- drops SPI: `0`;
- resync FWD operativos: `0`;
- supervisor: `RUNNING`, `ok`;
- veredicto: `PASS`.

## Criterio de cierre

La version se considera aceptada porque ambas velocidades soportadas cerraron
8 h con:

- velocidad detectada constante;
- cero errores SPI operativos;
- cero errores de paridad;
- cero overflow;
- cero drops SPI;
- cero resync FWD operativos;
- cero muestras desconectadas;
- supervisor sano y sin acciones correctivas;
- balance exacto entre palabras recibidas, aceptadas y filtradas.

## Fuera de alcance

No incluye:

- niveles electricos ARINC 429 reales;
- receptor diferencial ARINC dedicado o equivalente discreto;
- proteccion contra sobretension/transitorios;
- aislamiento;
- PCB final;
- validacion en una linea ARINC real de campo.

Esos puntos corresponden a la siguiente fase del proyecto: sniffer fisico
plug-and-play para linea ARINC real.
