# Evidencia de recuperacion V5

Fecha de referencia: 2026-06-24.

## Objetivo

Validar que los puntos conflictivos detectados en pruebas de desconexion,
reinicio y arranque fuera de fase quedaron corregidos con la recuperacion V5 y
el modo dios v2.

## Cambios bajo prueba

- Sniffer:
  - build esperada:
    `SPI-PIOFRAME-ARINC429-STRICTPARITY-DRDY-AUTORATE-RECOVERY-V5`
  - resincronizacion ARINC esperando reposo electrico estable antes de
    reactivar la PIO.
- Bridge:
  - reset de transporte `pio-frame` mediante trama completa de `32 bytes`
    `0xF0`.
  - endpoint `POST /control/recover_spi`.
  - contador `spi_transport_resets`.
- Raspberry Pi 3B+:
  - `arinc-supervisor.service`.
  - estados de modo dios:
    `BOOTING`, `WAITING_SNIFFER`, `SPI_SYNCING`, `RUNNING`,
    `ARINC_STALLED`, `CABLE_FAULT`, `BRIDGE_FAULT`, `DASHBOARD_FAULT`,
    `RECOVERING`.

## Resultado

El ensayo de laboratorio informado por el usuario confirma que los puntos que
habian quedado conflictivos quedaron corregidos:

- reinicio/reconexion de sniffer;
- recuperacion del bridge despues de arranque fuera de fase;
- recuperacion tras reinicio de servicios;
- recuperacion tras reinicio de Raspberry Pi 3B+;
- clasificacion del sistema sin ensuciar el camino de datos.

## Criterio operativo

Para una corrida limpia se espera:

- `supervisor_state = RUNNING`;
- `supervisor_health = ok`;
- `connected = true`;
- `spi_errors = 0` en regimen;
- `parity_errors_sniffer = 0` en corrida sin inyeccion;
- `overflow_events = 0`;
- `spi_drop_events = 0`;
- `accepted_words` creciendo.

Si aparece `CABLE_FAULT`, la condicion debe interpretarse como probable falla
fisica o ausencia de actividad ARINC, no como error del bridge.
