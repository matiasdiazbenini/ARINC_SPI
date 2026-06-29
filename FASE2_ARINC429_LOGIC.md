# Fase 2: ARINC 429 Logic

## Estado

- Fase 1 cerrada como `emulacion funcional`.
- Fase 2 cerrada como `recreacion logica/temporal` de ARINC 429 sobre GPIO.
- Velocidades validadas:
  - `100 kbps`
  - `12.5 kbps`
- La `3B+` conserva bridge/dashboard y suma exportacion estadistica mediante
  `stats_recorder.py`.

## Firmware principal

- TX:
  - `arinc_tx_arinc429_logic_stream_autorate`
  - `arinc_tx_arinc429_logic_stream_12k5`
- RX:
  - `arinc_rx_arinc429_logic_stream`
- SNIFFER:
  - `sniffer_arinc429_logic_pio_frame`

## Objetivo logrado

- Mantener la arquitectura:
  - `ARINC-TX -> ARINC-RX`
  - `ARINC-SNIFFER` pasivo sobre la linea
  - `SNIFFER -> 3B+ -> bridge -> Flask/dashboard`
- Recrear palabra, temporizacion, retorno a cero, label, SDI, SSM y paridad.
- Validar captura pasiva y filtrado por `label + SDI`.
- Exportar snapshot, contadores y estado por SPI hacia la 3B+.
- Visualizar y registrar resultados con dashboard, Prometheus/Grafana y
  `stats_recorder.py`.

## Caracteristicas tecnicas validadas

- Codificacion `RZ` bipolar logica:
  - `HI = 10`
  - `LO = 01`
  - `NULL = 00`
- Palabras ARINC logicas de `32 bits`.
- Paridad estricta en RX y SNIFFER.
- Modo stream sin depender de lotes fijos de `1000` palabras.
- Autorate:
  - TX puede arrancar en `100 kbps` o `12.5 kbps`.
  - RX recibe sin configuracion externa de velocidad.
  - SNIFFER detecta autonomamente la velocidad FWD.
- Transporte SPI final:
  - `pio-frame`
  - tramas completas de `32 bytes`
  - `DRDY` como disparador principal
  - `8 MHz` como perfil operativo recomendado
  - `byte` queda solo como fallback historico estable.
- Supervisor en 3B+:
  - autoarranque de bridge/Flask/supervisor
  - recuperacion de transporte SPI
  - clasificacion de estados operativos.

## Perfil recomendado de bridge/dashboard

- Bridge SPI:
  - `ARINC_SPI_HZ = 8000000`
  - `ARINC_SPI_TRANSFER_MODE = pio-frame`
  - `ARINC_SPI_REQUEST_MODE = drdy`
  - `ARINC_SPI_DRDY_GPIO = 25`
  - `ARINC_SPI_MANUAL_CS = 1`
  - `ARINC_SPI_CS_SETUP_US = 100`
  - `ARINC_SPI_CS_HOLD_US = 100`
  - `ARINC_SPI_BYTE_DELAY_US = 0`
  - `ARINC_SPI_POLL_SEC = 0.006`
  - `ARINC_SPI_STATS_SEC = 0.06`
- Dashboard:
  - `REFRESH_MS = 33`

## Evidencia de validacion

### Corridas finales autorate de 8 horas

- `12.5 kbps`:
  - sesion: `20260619_212806_corrida8h_12k5`
  - `received_words = 9,876,638`
  - `accepted_words = 2,962,992`
  - `spi_errors = 0`
  - `parity_errors_sniffer = 0`
  - `overflow_events = 0`
  - `spi_drop_events = 0`
  - resultado: `PASS`
- `100 kbps`:
  - sesion: `20260620_065154_corrida8h_100k`
  - `received_words = 76,707,606`
  - `accepted_words = 23,012,283`
  - `spi_errors = 0`
  - `parity_errors_sniffer = 0`
  - `overflow_events = 0`
  - `spi_drop_events = 0`
  - resultado: `PASS`

### Osciloscopio autorate 2026-06-29

- `100 kbps`:
  - palabra completa: `320 us`
  - palabra mas gap visible: `364 us`
  - gap visible: `44 us`
- `12.5 kbps`:
  - media celda activa: `40 us`
  - bit completo: `80 us`
  - palabra completa esperada: `2.56 ms`
  - gap visible: `360 us`
- relacion entre gaps:
  - `360/44 = 8.18`, coherente con la relacion teorica `8:1`.

### Paridad estricta

- Prueba controlada:
  - `received_words = 610905`
  - `accepted_words = 177163`
  - `filtered_words = 427633`
  - `parity_errors_sniffer = 6109`
  - balance de clasificacion: `0`
  - `spi_errors = 0`
  - resultado: `PASS`

## Documentos asociados

- `docs/pdf/trabajo_final_arinc429.pdf`
- `docs/tex/trabajo_final_arinc429.tex`
- `docs/evidencia_corridas_finales_autorate_8h.md`
- `docs/evidencia_osciloscopio_autorate_20260629.md`
- `docs/evidencia_autorate_100k_12k5.md`
- `docs/evidencia_barrido_frecuencia_spi.md`

## Lectura tecnica

La fase `arinc429_logic` queda validada en funcionalidad, temporizacion,
integracion con la 3B+, estabilidad larga, autorate, paridad estricta,
transporte SPI por trama completa y evidencia instrumental por osciloscopio.

Lo pendiente a futuro ya no es corregir esta capa logica, sino avanzar hacia
la interfaz electrica ARINC 429 real con entrada diferencial, proteccion,
alta impedancia y adaptacion de niveles.
