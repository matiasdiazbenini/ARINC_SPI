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

## Integracion con Flask

En `FLASK/arinc_dashboard`:

```bash
export ARINC_SOURCE_MODE=bridge
export ARINC_BRIDGE_URL=http://127.0.0.1:5100
python3 app.py
```

De esta manera el dashboard deja de leer un puerto serial y pasa a consumir el bridge SPI.
