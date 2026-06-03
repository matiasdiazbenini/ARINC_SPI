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

- enlace `master -> slave` en `100 kbps`
- ACK reverso `slave -> master`
- captura pasiva con `ARINC-SNIFFER`
- exportacion por SPI hacia la Raspberry Pi 3B+
- bridge HTTP
- dashboard Flask
- Prometheus + Grafana
- validacion con osciloscopio de:
  - retorno a cero
  - pulsos de `5 us`
  - bit time de `10 us`
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
- [imagenes](imagenes/)
  - capturas de osciloscopio organizadas por enlace y canal
- [docs](docs/README.md)
  - indice de documentacion y memoria externa del proyecto
- [PROJECT_CONTEXT.md](PROJECT_CONTEXT.md)
  - archivo maestro de handoff para retomar rapido el trabajo
- raiz del repo:
  - documentacion general
  - PDFs
  - fuentes Overleaf/LaTeX
  - evidencia e integracion de sistema

## Topologia actual recomendada

- Pico transmisor:
  - genera `FWD` por `GP2/GP3`
- Pico receptor:
  - recibe `FWD`
  - responde `ACK` por `GP4/GP5`
- Pico sniffer:
  - escucha `FWD` y `REV`
  - filtra por `label + SDI`
  - exporta snapshot/eventos por SPI
- Raspberry Pi 3B+:
  - consume SPI del sniffer
  - publica bridge HTTP en `:5100`
  - corre Flask en `:5000`
- Notebook:
  - accede al dashboard
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

- `ARINC_SPI_HZ = 800000`
- `ARINC_SPI_BYTE_DELAY_US = 25`
- `ARINC_SPI_POLL_SEC = 0.006`
- `ARINC_SPI_STATS_SEC = 0.06`
- `ARINC_SPI_RESPONSE_DELAY_SEC = 0.00005`
- refresh Flask:
  - `33 ms`

El perfil de estres mas agresivo fue probado y aguanto varias horas, pero con una tasa de `spi_errors` mucho mayor, por lo que no se deja como modo continuo recomendado.

## Documentos clave

- [PROJECT_CONTEXT.md](PROJECT_CONTEXT.md)
- [FASE2_ARINC429_LOGIC.md](FASE2_ARINC429_LOGIC.md)
- [resumen_proyecto_arinc429.md](resumen_proyecto_arinc429.md)
- [instructivo_osciloscopio_arinc429_logic.md](instructivo_osciloscopio_arinc429_logic.md)
- [tesis_fuente_overleaf_arinc429.tex](tesis_fuente_overleaf_arinc429.tex)
- [modelo_osi_arquitectura_arinc429.tex](modelo_osi_arquitectura_arinc429.tex)
- [osciloscopio_descripcion_imagenes.tex](osciloscopio_descripcion_imagenes.tex)

## Nota sobre ramas

Por pedido explicito del usuario, no deben tocarse las ramas:

- `MIL-M`
- `MIL-E`
- `FLASK-MIL`
- `version_estable`

correspondientes a trabajo ajeno al desarrollo ARINC actual.
