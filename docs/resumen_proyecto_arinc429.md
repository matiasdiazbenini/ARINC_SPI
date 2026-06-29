# Resumen del proyecto ARINC 429

## Objetivo general
- Interceptar una comunicacion tipo ARINC 429 con una Pico sniffer.
- Filtrar en la Pico solo lo relevante.
- Entregar a la Raspberry Pi 3B+ solo la informacion filtrada.
- Visualizar el estado desde bridge SPI, Flask y dashboard web.

## Arquitectura actual
- Pico master:
  - genera tramas
  - transmite por FWD
- Pico slave:
  - recibe FWD
  - procesa lotes
  - responde ACK por REV
- Pico sniffer:
  - escucha FWD y REV
  - filtra labels
  - exporta snapshots por SPI a la 3B+
- Raspberry Pi 3B+:
  - bridge SPI
  - Flask
  - dashboard

## Fases cerradas

### Fase 1 - Emulacion funcional
- Se valido la arquitectura completa.
- Se estabilizo el transporte SPI byte a byte entre sniffer y 3B+.
- Se paso de cola de eventos a snapshots robustos.
- Se valido el dashboard en corridas largas.

### Fase 1B - Optimizacion bridge/dashboard
- Se ajusto el bridge SPI para mejorar fluidez visual.
- Se compararon perfiles de polling y velocidad SPI.
- Se definio un perfil recomendado y uno de estres.

### Fase 2 - ARINC 429 logic
- Se mantuvo la 3B+ sin cambios funcionales.
- Se agrego un modo nuevo de codificacion logica/temporal.
- Se valido master, slave y sniffer en este modo.
- Se valido estabilidad larga.
- Se agrego soporte autorate `100 kbps` / `12.5 kbps`.
- Se valido osciloscopio para ambas velocidades.
- Se migro el transporte final recomendado a SPI `pio-frame`, `DRDY` y `8 MHz`.

## Perfil recomendado actual
- Bridge SPI:
  - `ARINC_SPI_HZ = 8000000`
  - `ARINC_SPI_TRANSFER_MODE = pio-frame`
  - `ARINC_SPI_REQUEST_MODE = drdy`
  - `ARINC_SPI_DRDY_GPIO = 25`
  - `ARINC_SPI_MANUAL_CS = 1`
  - `ARINC_SPI_BYTE_DELAY_US = 0`
  - `ARINC_SPI_POLL_SEC = 0.006`
  - `ARINC_SPI_STATS_SEC = 0.06`
- Dashboard:
  - `REFRESH_MS = 33`

## Perfil de estres
- Bridge SPI:
  - `ARINC_SPI_HZ = 1200000`
  - `ARINC_SPI_BYTE_DELAY_US = 0`
  - `ARINC_SPI_POLL_SEC = 0.005`
  - `ARINC_SPI_STATS_SEC = 0.05`
  - `ARINC_SPI_RESPONSE_DELAY_SEC = 0`
- Dashboard:
  - `REFRESH_MS = 25`
- Conclusion:
  - funciona
  - pero acumula muchos mas `spi_errors`
  - no conviene como perfil continuo

## Hallazgos principales
- El cuello fuerte del sistema ya no esta en Flask.
- El cuello real aparecia en el bridge SPI cuando se lo forzaba demasiado; se
  resolvio operativamente con SPI `pio-frame`, `DRDY` y perfil a `8 MHz`.
- La fase `arinc429_logic` quedo validada a `100 kbps` y `12.5 kbps`.
- Las corridas finales de ocho horas cerraron con cero errores SPI operativos,
  cero errores de paridad, cero overflow y cero drops.
- El osciloscopio confirmo palabra de `320 us` a `100 kbps`, bit de `80 us` a
  `12.5 kbps` y relacion temporal cercana a `8:1`.
- La siguiente etapa natural es la capa electrica ARINC 429 real.

## Cierre de la parte logica
- Mantener esta fase como baseline estable.
- Usar como evidencia principal:
  - `docs/pdf/trabajo_final_arinc429.pdf`
  - `docs/evidencia_corridas_finales_autorate_8h.md`
  - `docs/evidencia_osciloscopio_autorate_20260629.md`
- Luego pasar a la futura interfaz electrica real con transceptores ARINC 429.
