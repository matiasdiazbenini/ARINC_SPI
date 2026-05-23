# PI3_HOST

Bridge para Raspberry Pi 3B+ que consume por SPI los eventos filtrados desde `ARINC_SNIFFER` y los expone por HTTP para Flask/Grafana.

## Rol

- La Pico `ARINC_SNIFFER` sigue haciendo:
  - captura de `FWD` y `REV`
  - filtro primario por whitelist
  - exportacion de eventos y estadisticas por SPI
- La Raspberry Pi 3B+ hace:
  - lectura SPI como master
  - acumulacion de eventos
  - API HTTP local con `/stats`, `/records`, `/filter`, `/metrics`
- El dashboard Flask puede correr aparte en la misma Pi y consumir este bridge en modo `bridge`.

## Pines sugeridos

### Pico sniffer

- `GP16` = `SPI0_RX`  <- MOSI desde la Pi
- `GP17` = `SPI0_CSn` <- CE0 desde la Pi
- `GP18` = `SPI0_SCK` <- SCLK desde la Pi
- `GP19` = `SPI0_TX`  -> MISO hacia la Pi
- `GP20` = `DRDY` opcional -> GPIO libre de la Pi

### Raspberry Pi 3B+

- `GPIO10 / pin 19` = MOSI
- `GPIO8 / pin 24` = CE0
- `GPIO11 / pin 23` = SCLK
- `GPIO9 / pin 21` = MISO
- `GND / pin 6` = masa comun
- `GPIO25 / pin 22` = opcional para `DRDY`

## Despliegue basico

```bash
cd ~/PAMPA/PI3_HOST
python3 -m venv .venv
source .venv/bin/activate
pip install -r requirements.txt
python3 spi_sniffer_bridge.py
```

El bridge queda por defecto en:

```text
http://127.0.0.1:5100
```

Endpoints principales:

- `/health`
- `/records`
- `/stats`
- `/filter`
- `/metrics`
- `/control/reset`

## Variables de entorno

- `ARINC_SPI_BUS` defecto `0`
- `ARINC_SPI_DEVICE` defecto `0`
- `ARINC_SPI_HZ` defecto `200000`
- `ARINC_BRIDGE_PORT` defecto `5100`
- `ARINC_SPI_POLL_SEC` defecto `0.05`
- `ARINC_SPI_STATS_SEC` defecto `0.50`
- `ARINC_SPI_FILTER_SEC` defecto `2.00`
- `ARINC_SPI_RESPONSE_DELAY_SEC` defecto `0.002`
- `ARINC_SPI_RESPONSE_RETRIES` defecto `4`

## Integracion con Flask

En `FLASK/arinc_dashboard`:

```bash
export ARINC_SOURCE_MODE=bridge
export ARINC_BRIDGE_URL=http://127.0.0.1:5100
export ARINC_DASHBOARD_HOST=0.0.0.0
export ARINC_DASHBOARD_PORT=5000
python3 app.py
```

De esta manera el dashboard deja de leer un puerto serial y pasa a consumir el bridge SPI.

## Arranque desde una sola terminal

Si no queres abrir dos sesiones SSH, podes dejar el bridge corriendo en segundo plano y luego levantar Flask en la misma terminal:

```bash
cd ~/PAMPA/PI3_HOST
source .venv/bin/activate
nohup python3 spi_sniffer_bridge.py > bridge.log 2>&1 &

cd ~/PAMPA/FLASK/arinc_dashboard
source .venv/bin/activate
export ARINC_SOURCE_MODE=bridge
export ARINC_BRIDGE_URL=http://127.0.0.1:5100
export ARINC_DASHBOARD_HOST=0.0.0.0
export ARINC_DASHBOARD_PORT=5000
python3 app.py
```

Con ese esquema:

- el bridge sigue escuchando en `http://127.0.0.1:5100`
- Flask queda accesible desde tu PC en `http://IP_DE_LA_PI:5000`

## Diagnostico rapido del enlace SPI

Si el bridge muestra errores como `magic SPI invalido`, conviene probar el enlace fisico sin Flask con:

```bash
cd ~/PAMPA/PI3_HOST
source .venv/bin/activate
python3 spi_link_diagnostic.py
```

La herramienta barre varias combinaciones de frecuencia SPI y espera entre request/respuesta. Si encuentra respuestas validas, te dice cual fue la mejor combinacion. Si no encuentra ninguna, reporta patrones utiles para diagnostico:

- `todo_cero`: suele apuntar a `MISO` desconectado o `CS`/`SCLK` sin transaccion real
- `todo_ff`: suele apuntar a linea flotante o dispositivo no seleccionado
- `magic_desplazado_byte_N`: hay actividad, pero la trama esta corrida o desalineada
- `sin_magic`: la respuesta no se parece al protocolo esperado

## Prueba de capacidad por etapas

Para medir estabilidad y limite operativo del bridge con distintos filtros, se puede usar:

```bash
cd ~/PAMPA/PI3_HOST
source .venv/bin/activate
python3 spi_capacity_runner.py --stages core4@180,known10@180,pass_all@60 --sample-sec 2
```

Presets disponibles:

- `ack_temp`
- `core4`
- `known10`
- `known11`
- `pass_all`

Notas:

- `known11` existe como escenario de referencia, pero hoy excede la capacidad del filtro embebido (`10` entradas).
- El runner la marca como `SKIP` de forma explicita en vez de caer con `502`.

El runner:

- aplica el filtro correspondiente a cada etapa
- muestrea `/stats` durante la duracion indicada
- resume throughput, reconexiones, `spi_errors`, `resync`, `slot_evictions`
- guarda un reporte JSON en `spi_capacity_report.json`

Si queres aislar cada etapa con contadores desde cero:

```bash
python3 spi_capacity_runner.py --reset-before-stage
```
