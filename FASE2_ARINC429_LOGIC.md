# Fase 2: ARINC 429 Logic

## Estado
- Fase 1 cerrada como `emulacion funcional`.
- Fase 2 validada como `recreacion logica/temporal` a `100 kbps`.
- La `3B+` se mantiene sin cambios funcionales.

## Firmware de esta fase
- `master_arinc429_logic`
- `slave_arinc429_logic`
- `sniffer_arinc429_logic`

## Objetivo logrado
- Mantener la arquitectura:
  - `master -> slave -> sniffer -> 3B+ -> bridge -> Flask/dashboard`
- Cambiar solo la forma de generacion/captura de la trama entre las Pico.
- Conservar:
  - labels
  - filtro
  - SPI con la `3B+`
  - dashboard
  - ACK semantico

## Caracteristicas tecnicas validadas
- Codificacion `RZ` bipolar logica:
  - `HI = 10`
  - `LO = 01`
  - `NULL = 00`
- Adaptacion interna del label al formato wire.
- Modo dual:
  - `legacy`
  - `arinc429_logic`
- Sniffer con snapshot SPI byte-a-byte compatible con la fase anterior.

## Resultado de validacion
- `master_arinc429_logic + slave_arinc429_logic`: operativo.
- `master_arinc429_logic + slave_arinc429_logic + sniffer_arinc429_logic`: operativo.
- `3B+` con bridge + Flask + dashboard: operativo sin cambios funcionales.
- Corrida larga aproximada de `12 horas`: estable.

## Perfil recomendado de bridge/dashboard
- Bridge SPI:
  - `ARINC_SPI_HZ = 800000`
  - `ARINC_SPI_BYTE_DELAY_US = 25`
  - `ARINC_SPI_POLL_SEC = 0.006`
  - `ARINC_SPI_STATS_SEC = 0.06`
  - `ARINC_SPI_RESPONSE_DELAY_SEC = 0.00005`
- Dashboard:
  - `REFRESH_MS = 33`

## Perfil de estres maximo observado
- Bridge SPI:
  - `ARINC_SPI_HZ = 1200000`
  - `ARINC_SPI_BYTE_DELAY_US = 0`
  - `ARINC_SPI_POLL_SEC = 0.005`
  - `ARINC_SPI_STATS_SEC = 0.05`
  - `ARINC_SPI_RESPONSE_DELAY_SEC = 0.0`
- Dashboard:
  - `REFRESH_MS = 25`
- Resultado:
  - se sostuvo muchas horas
  - pero con crecimiento apreciable de `spi_errors`
  - no queda como perfil recomendado para uso continuo

## Indicadores observados en la corrida larga
- `connected = true`
- `last_error = ""`
- `spi_drop_events = 0`
- `overflow_events = 0`
- `parity_error = 0`
- `slot_evictions = 0`
- `rev_resync_events = 0`
- `fwd_resync_events` bajo y tolerable
- `ACK_BATCH` coherente con `ssm = NORMAL`

## Lectura tecnica
- La fase `arinc429_logic` quedo validada en:
  - funcionalidad
  - integracion con la `3B+`
  - estabilidad larga
- Lo pendiente a futuro ya no es esta capa logica, sino la futura interfaz electrica ARINC 429 real.

## Proximo paso futuro
- Mantener esta fase como baseline logica estable.
- En una fase posterior:
  - agregar transceptores/receptores ARINC 429 reales
  - reutilizar SPI, bridge, dashboard y semantica de datos sin rediseñar el resto
