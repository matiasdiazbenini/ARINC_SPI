# ARINC_SPI

Repositorio de trabajo del proyecto PAMPA orientado a la recreacion logica/temporal de un enlace tipo ARINC 429 usando:

- 3 Raspberry Pi Pico / Pico 2 W
- 1 Raspberry Pi 3B+
- dashboard Flask
- bridge HTTP/SPI
- Prometheus + Grafana

Este repositorio estuvo originalmente centrado en el firmware del transmisor, pero fue ampliado y reorganizado para conservar en un solo lugar el estado real de la fase `arinc429_logic`, su documentacion y la evidencia de laboratorio.

## Estado actual

La fase validada hasta el momento es:

- `arinc429_logic`

Quedo validado:

- enlace `master -> slave` en `100 kbps` y `12.5 kbps`
- ACK reverso `slave -> master`
- captura pasiva con `ARINC-SNIFFER`
- exportacion por SPI hacia la Raspberry Pi 3B+
- bridge HTTP
- dashboard Flask
- Prometheus + Grafana
- corrida post tutor con `spi_errors = 0` en regimen estable
- separacion de errores SPI de arranque (`spi_startup_errors`) y operativos (`spi_errors`)
- transporte SPI por tramas completas validado con `sniffer_arinc429_logic_pio_frame`
- corrida PIO-frame de 6-7 horas con `spi_errors = 0`
- mecanismo DRDY implementado para consultar snapshot por demanda
- recuperacion V5 implementada para:
  - resincronizar captura ARINC esperando reposo electrico del canal
  - resetear transporte SPI `pio-frame` con trama completa de `32 bytes`
  - supervisar bridge/Flask con modo dios v2 (`arinc-supervisor.service`)
  - clasificar estados y ejecutar recuperacion escalonada
- paridad estricta implementada en RX y SNIFFER
- deteccion autonoma en sniffer de velocidad `FWD` entre `100 kbps` y `12.5 kbps`
- resync FWD separados entre arranque y regimen
- exportacion de pruebas a CSV, JSON y PDF desde la 3B+
- prueba de paridad estricta validada con balance exacto de contadores
- corridas finales de `8 horas` a `100 kbps` y `12.5 kbps`, ambas con cero
  errores operativos, cero errores de paridad, cero overflow y cero drops SPI
- validacion con osciloscopio de:
  - retorno a cero
  - pulsos de `5 us`
  - bit time de `10 us`
  - modo autorate:
    - `100 kbps`: palabra de `320 us` y gap visible de `44 us`
    - `12.5 kbps`: media celda de `40 us`, bit de `80 us` y gap visible de
      `360 us`
    - relacion de gaps `360/44 = 8.18`, coherente con la relacion teorica `8:1`
  - reconstruccion diferencial `+3.3 / 0 / -3.3 V`

## Estructura del repo

- [ARINC-TX](ARINC-TX/README.md)
  - firmware del nodo transmisor
- [ARINC-RX](ARINC-RX/README.md)
  - snapshot del firmware del nodo receptor usado en esta etapa
- [ARINC-SNIFFER](ARINC-SNIFFER/README.md)
  - snapshot del firmware sniffer y protocolo SPI asociado
- [HOST_3b+](HOST_3b+/README.md)
  - bridge SPI/HTTP, diagnosticos y scripts de host
- [DASHBOARD_WEB/arinc_dashboard](DASHBOARD_WEB/arinc_dashboard/README.md)
  - dashboard web operativo
- [prometheus](prometheus/README.md)
  - configuracion de Prometheus usada en la notebook
- [ELECTRICO](ELECTRICO/frontend_arinc429_lab/README.md)
  - front-end electrico de laboratorio, esquematico KiCad y relevamiento de componentes
- [imagenes](imagenes/)
  - capturas de osciloscopio organizadas por enlace y canal
- [docs](docs/README.md)
  - indice de documentacion y memoria externa del proyecto
- [PROJECT_CONTEXT.md](PROJECT_CONTEXT.md)
  - archivo maestro de handoff para retomar rapido el trabajo
- raiz del repo:
  - README, contexto maestro y estado de fase
- `docs/`:
  - documentacion general
  - PDFs generados
  - fuentes Overleaf/LaTeX
  - scripts generadores
- `ELECTRICO/`:
  - proyecto KiCad inicial para el acondicionamiento electrico ARINC-like
  - relevamiento de componentes con precios, alternativas y correspondencia con el esquematico

## Topologia actual recomendada

- Pico transmisor:
  - genera `FWD` por `GP2/GP3`
- Pico receptor:
  - recibe `FWD`
  - responde `ACK` por `GP4/GP5`
- Pico sniffer:
  - escucha `FWD` y `REV`
  - no transmite sobre lineas ARINC
  - filtra por `label + SDI`
  - exporta snapshot/eventos por SPI
- Raspberry Pi 3B+:
  - consume SPI del sniffer
  - publica bridge HTTP local en `127.0.0.1:5100`
  - corre Flask hacia la red en `:5000`
- Notebook:
  - accede al dashboard
  - scrapea metricas desde Flask
  - corre Prometheus en `:9090`
  - corre Grafana en `:3000`

## Red recomendada

La configuracion que mejor resultado dio para estabilidad larga fue:

- notebook por Wi-Fi a Internet, si hace falta
- notebook por Ethernet directo a la Raspberry Pi 3B+
- IP fija de la notebook en Ethernet:
  - `192.168.50.1/24`
- IP fija de la Raspberry Pi 3B+ en Ethernet:
  - `192.168.50.2/24`

Se observo que esta configuracion es mas robusta que dejar la 3B+ colgada por Wi-Fi para el trafico operativo del sistema.

Acceso recomendado desde la notebook:

- dashboard Flask:
  - `http://192.168.50.2:5000`
- metricas para Prometheus:
  - `http://192.168.50.2:5000/metrics`

El bridge SPI/HTTP queda local dentro de la 3B+ en `http://127.0.0.1:5100`; Flask lo consulta por loopback.

## Modo stream post tutor

Ademas del modo laboratorio batch/ACK, quedo validada la transmision sin depender de bloques fijos de `1000` palabras:

- TX:
  - `arinc_tx_arinc429_logic_stream`
- RX:
  - `arinc_rx_arinc429_logic_stream`
- Sniffer:
  - `sniffer_arinc429_logic_pio_frame`

En este ensayo, `ARINC-TX` transmite rafagas de longitud variable y no espera ACK. `ARINC-RX` procesa palabras a medida que llegan y no emite ACK por `REV`. Por eso, en el dashboard, `TEMPERATURA`, `VELOCIDAD` y `ALTITUD` deben crecer; `ACK_BATCH` puede quedar ausente o congelado.

Criterios de prueba:

- `spi_errors = 0`
- `spi_drop_events = 0`
- `overflow_events = 0`
- `parity_errors_sniffer = 0`

Resultado de aproximadamente `10 horas`:

- `accepted_words = 22,756,775`
- `ack_events = 0`
- `spi_errors = 0`
- `spi_startup_errors = 6`
- `spi_drop_events = 0`
- `overflow_events = 0`
- `parity_errors_sniffer = 0`
- `fwd_resync_events = 3`

Evidencia:

- [evidencia_stream_aleatorio_post_tutor.md](docs/evidencia_stream_aleatorio_post_tutor.md)

## Modo autorate 100 kbps / 12.5 kbps

Para probar la capacidad de `ARINC-SNIFFER` de detectar la velocidad sin que TX
ni RX se la informen por configuracion externa, se agrego el target:

- TX:
  - `arinc_tx_arinc429_logic_stream_autorate`
  - `arinc_tx_arinc429_logic_stream_12k5` para forzar la velocidad baja en una
    prueba controlada

Este firmware elige al arrancar entre `100 kbps` y `12.5 kbps`. El RX stream
no requiere cambio porque su PIO recibe por flancos/nivel activo. El sniffer
publica la velocidad detectada como `detected_bit_rate_bps` y
`detected_bit_rate_txt` en `/stats`, y como `arinc_detected_bit_rate_bps` en
Prometheus.

Validacion observada:

- el TX arranco en distintas corridas tanto a `100 kbps` como a `12.5 kbps`
- corrida de `30 min` a `12.5 kbps`:
  - `detected_bit_rate_bps = 12500`
  - `received_words = 618,842`
  - `accepted_words = 185,652`
  - `spi_errors = 0`
- corrida de `30 min` a `100 kbps`:
  - `detected_bit_rate_bps = 100000`
  - `received_words = 4,807,727`
  - `accepted_words = 1,442,318`
  - `spi_errors = 0`
- ambas corridas cerraron con:
  - `parity_errors_sniffer = 0`
  - `overflow_events = 0`
  - `spi_drop_events = 0`
  - balance de clasificacion igual a `0`
- corridas finales de `8 horas`:
  - `12.5 kbps`: `received_words = 9,876,638`,
    `accepted_words = 2,962,992`, `spi_errors = 0`
  - `100 kbps`: `received_words = 76,707,606`,
    `accepted_words = 23,012,283`, `spi_errors = 0`
  - ambas con `parity_errors_sniffer = 0`, `overflow_events = 0`,
    `spi_drop_events = 0`, `fwd_operational_resync_events = 0` y
    `supervisor_health = ok`

Evidencia:

- [evidencia_autorate_100k_12k5.md](docs/evidencia_autorate_100k_12k5.md)
- [evidencia_corridas_finales_autorate_8h.md](docs/evidencia_corridas_finales_autorate_8h.md)

## Forma recomendada de trabajo

Hay dos maneras comodas de trabajar:

- abrir la raiz del repo para tener toda la arquitectura visible
- abrir una subcarpeta puntual cuando quieras concentrarte en un componente

Por ejemplo:

- `ARINC-TX/`
- `ARINC-RX/`
- `ARINC-SNIFFER/`
- `HOST_3b+/`
- `DASHBOARD_WEB/arinc_dashboard/`

Tambien se pueden abrir varias ventanas de VS Code en paralelo, una por componente, sin problema.

## Perfil operativo recomendado

Para el bridge/dashboard, el perfil que mejor balance dio entre fluidez y estabilidad fue:

- modo PIO-frame validado post tutor:
  - `ARINC_SPI_TRANSFER_MODE = pio-frame`
  - `ARINC_SPI_MANUAL_CS = 1`
  - `ARINC_SPI_CS_SETUP_US = 100`
  - `ARINC_SPI_CS_HOLD_US = 100`
  - `ARINC_SPI_HZ = 8000000`
  - `ARINC_SPI_BYTE_DELAY_US = 0`
  - corrida de aproximadamente `9 horas` con `spi_errors = 0`
- fallback PIO-frame conservador:
  - `ARINC_SPI_HZ = 400000`
- modo byte validado como fallback estable:
  - `ARINC_SPI_TRANSFER_MODE = byte`
  - `ARINC_SPI_HZ = 800000`
  - `ARINC_SPI_BYTE_DELAY_US = 25`

Perfil historico estable previo al PIO-frame:

- `ARINC_SPI_HZ = 800000`
- `ARINC_SPI_TRANSFER_MODE = byte`
- `ARINC_SPI_BYTE_DELAY_US = 25`
- `ARINC_SPI_POLL_SEC = 0.006`
- `ARINC_SPI_STATS_SEC = 0.06`
- `ARINC_SPI_RESPONSE_DELAY_SEC = 0.00005`
- refresh Flask:
  - `33 ms`

El perfil de estres mas agresivo fue probado y aguanto varias horas, pero con una tasa de `spi_errors` mucho mayor, por lo que no se deja como modo continuo recomendado.

## Documentos clave

- [Entrega consolidada para el tutor](docs/entrega_tutor/README.md)
- [Trabajo final en formato tesis PDF](docs/pdf/trabajo_final_arinc429.pdf)
- [Fuente LaTeX del trabajo final](docs/tex/trabajo_final_arinc429.tex)
- [PROJECT_CONTEXT.md](PROJECT_CONTEXT.md)
- [FASE2_ARINC429_LOGIC.md](FASE2_ARINC429_LOGIC.md)
- [resumen_proyecto_arinc429.md](docs/resumen_proyecto_arinc429.md)
- [instructivo_osciloscopio_arinc429_logic.md](docs/instructivo_osciloscopio_arinc429_logic.md)
- [evidencia_spi_errors_cero_post_tutor.md](docs/evidencia_spi_errors_cero_post_tutor.md)
- [evidencia_spi_pio_frame_post_tutor.md](docs/evidencia_spi_pio_frame_post_tutor.md)
- [procedimiento_prueba_paridad_estricta.md](docs/procedimiento_prueba_paridad_estricta.md)
- [procedimiento_registro_estadistico.md](docs/procedimiento_registro_estadistico.md)
- [evidencia_corrida_final_stream_8mhz_8h.md](docs/evidencia_corrida_final_stream_8mhz_8h.md)
- [evidencia_corridas_finales_autorate_8h.md](docs/evidencia_corridas_finales_autorate_8h.md)
- [evidencia_osciloscopio_autorate_20260629.md](docs/evidencia_osciloscopio_autorate_20260629.md)
- [tesis_fuente_overleaf_arinc429.tex](docs/tex/tesis_fuente_overleaf_arinc429.tex)
- [trabajo_final_arinc429.tex](docs/tex/trabajo_final_arinc429.tex)
- [modelo_osi_arquitectura_arinc429.tex](docs/tex/modelo_osi_arquitectura_arinc429.tex)
- [osciloscopio_descripcion_imagenes.tex](docs/tex/osciloscopio_descripcion_imagenes.tex)

## Nota sobre ramas

Por pedido explicito del usuario, no deben tocarse las ramas:

- `MIL-M`
- `MIL-E`
- `FLASK-MIL`
- `version_estable`

correspondientes a trabajo ajeno al desarrollo ARINC actual.
