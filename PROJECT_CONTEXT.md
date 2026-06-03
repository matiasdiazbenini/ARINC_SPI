# PROJECT_CONTEXT

Este archivo existe para que futuras sesiones de Codex, VS Code, Codex app o cualquier otra persona puedan retomar el proyecto sin depender del chat historico.

## 1. Objetivo del proyecto

Recrear una arquitectura de comunicacion inspirada en ARINC 429 en un entorno de laboratorio, validando por fases:

1. transmision entre nodos Pico
2. recepcion y ACK
3. sniffing pasivo
4. integracion host en Raspberry Pi 3B+
5. visualizacion y observabilidad
6. validacion instrumental por osciloscopio
7. futura transicion a interfaz electrica ARINC 429 real

## 2. Arquitectura actual

### Nodos

- Pico `ARINC-TX`
  - transmite palabras sobre `FWD`
- Pico `ARINC-RX`
  - recibe palabras y devuelve ACK sobre `REV`
- Pico `ARINC-SNIFFER`
  - observa ambas direcciones
  - filtra por `label + SDI`
  - exporta snapshot y estadisticas por SPI
- Raspberry Pi 3B+
  - corre `spi_sniffer_bridge.py`
  - corre dashboard Flask
- Notebook
  - consulta el dashboard
  - corre Prometheus y Grafana

### Enlaces

- `master <-> slave`
  - 2 enlaces simplex logicos:
    - `FWD`: `GP2 / GP3`
    - `REV`: `GP4 / GP5`
- `sniffer <-> 3B+`
  - SPI
- `3B+ <-> notebook`
  - Ethernet directo recomendado

### Layout recomendado del repositorio

La rama `ARINC` organiza el sistema completo en un solo repo con subcarpetas por componente:

- `ARINC-TX/`
- `ARINC-RX/`
- `ARINC-SNIFFER/`
- `HOST_3b+/`
- `DASHBOARD_WEB/arinc_dashboard/`
- `prometheus/`
- `imagenes/`
- `docs/`

## 3. Estado funcional validado

La fase `arinc429_logic` se considera validada.

Se comprobo:

- sistema estable en corridas largas
- labels utiles detectados:
  - `0xA5 TEMPERATURA`
  - `0xB1 VELOCIDAD`
  - `0xC2 ALTITUD`
  - `0xAC ACK_BATCH`
- `ACK` funcional
- dashboard Flask estable
- bridge SPI/HTTP estable
- Prometheus/Grafana funcionales
- validacion por osciloscopio en FWD y REV

## 4. Parametros operativos recomendados

### Enlace master/slave

- `BIT_RATE_HZ = 100000`
- bit time:
  - `10 us`
- media celda activa:
  - `5 us`
- segunda media celda:
  - retorno a cero

### Bridge / dashboard recomendado

- `ARINC_SPI_HZ = 800000`
- `ARINC_SPI_BYTE_DELAY_US = 25`
- `ARINC_SPI_POLL_SEC = 0.006`
- `ARINC_SPI_STATS_SEC = 0.06`
- `ARINC_SPI_RESPONSE_DELAY_SEC = 0.00005`
- refresh Flask:
  - `33 ms`

### Perfil de estres probado

- SPI a `1.2 MHz`
- refresh Flask a `25 ms`

Sirvio para encontrar el borde, pero no se recomienda para operacion continua.

## 5. Comparacion clave normal vs stress

Corridas largas comparables:

- modo recomendado:
  - `accepted_words ~ 20.1M`
  - `spi_errors = 42`
- modo stress:
  - `accepted_words ~ 18.3M`
  - `spi_errors = 2074`

Conclusion:

- el modo recomendado mueve mas palabras
- con muchisimos menos errores SPI
- el modo stress queda solo como referencia de limite

## 6. Red operativa recomendada

Se comprobo que la 3B+ por Wi-Fi podia introducir inestabilidad indirecta sobre la sniffer, probablemente por una combinacion de:

- jitter del host
- patron de polling menos estable
- ruido / consumo / entorno fisico

Configuracion recomendada:

- notebook Ethernet:
  - `192.168.50.1/24`
- Raspberry Pi 3B+ `eth0`:
  - `192.168.50.2/24`
- acceso desde notebook:
  - `http://192.168.50.2:5000`
- Prometheus scrapea:
  - `192.168.50.2:5100/metrics`

## 7. Resultados de osciloscopio

### FWD

- canales individuales:
  - `0 a 3.3 V`
- `Pos Width`:
  - `~5 us`
- retorno a cero confirmado
- en `MATH = CH1 - CH2`:
  - `~+3.3 V`
  - `0 V`
  - `~-3.3 V`

### REV

- mismo comportamiento bipolar logico
- actividad en rafagas, consistente con ACK

### Archivos relacionados

- [instructivo_osciloscopio_arinc429_logic.md](docs/instructivo_osciloscopio_arinc429_logic.md)
- [osciloscopio_descripcion_imagenes.tex](docs/tex/osciloscopio_descripcion_imagenes.tex)
- carpeta [imagenes](imagenes/)

## 8. Limites actuales conocidos

- snapshot del sniffer:
  - `16` slots maximos
- filtro SPI:
  - `10` entradas maximas

Si en el futuro se quiere trabajar con mas variables simultaneas:

- habra que subir `LATEST_SLOT_CAPACITY`
- y posiblemente `SNIFFER_SPI_FILTER_CAPACITY`

## 9. DRDY

`DRDY` no esta conectado en la arquitectura validada actual.

Esto no invalida resultados, porque el bridge actual funciona por polling. En una evolucion futura puede ser util para reducir polling inutil y hacer el host mas orientado a eventos.

## 10. Proximo paso real del proyecto

El siguiente salto importante ya no es seguir refinando esta fase, sino avanzar hacia:

- interfaz electrica ARINC 429 real
- proteccion y adaptacion de linea
- eventual migracion a PCB

## 11. Ramas que no deben tocarse

No modificar:

- `MIL-M`
- `MIL-E`
- `FLASK-MIL`
- `version_estable`

Son ramas ajenas al trabajo ARINC actual.
