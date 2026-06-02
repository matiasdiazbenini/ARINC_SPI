# PI3_HOST

Bridge HTTP que corre en la Raspberry Pi 3B+ y consume por SPI los datos filtrados entregados por `ARINC_SNIFFER`.

## Rol

- interrogar al sniffer por SPI
- reconstruir snapshot y contadores
- exponer endpoints HTTP consumibles por Flask, Prometheus y diagnosticos

## Endpoints principales

- `/health`
- `/stats`
- `/records`
- `/filter`
- `/metrics`
- `/control/reset`

## Parametros recomendados

El perfil operativo continuo que mejor resultado dio fue:

- `ARINC_SPI_HZ = 800000`
- `ARINC_SPI_BYTE_DELAY_US = 25`
- `ARINC_SPI_POLL_SEC = 0.006`
- `ARINC_SPI_STATS_SEC = 0.06`
- `ARINC_SPI_RESPONSE_DELAY_SEC = 0.00005`

## Despliegue basico en la 3B+

```bash
cd ~/PAMPA/PI3_HOST
python3 -m venv .venv
source .venv/bin/activate
pip install -r requirements.txt
python3 spi_sniffer_bridge.py
```

Por defecto queda disponible en:

- `http://127.0.0.1:5100`

## Variables de entorno relevantes

- `ARINC_SPI_BUS`
- `ARINC_SPI_DEVICE`
- `ARINC_SPI_HZ`
- `ARINC_SPI_BYTE_DELAY_US`
- `ARINC_SPI_POLL_SEC`
- `ARINC_SPI_STATS_SEC`
- `ARINC_SPI_FILTER_SEC`
- `ARINC_SPI_RESPONSE_DELAY_SEC`
- `ARINC_SPI_RESPONSE_RETRIES`
- `ARINC_BRIDGE_PORT`

## Integracion con Flask

Desde la 3B+:

```bash
cd ~/PAMPA/FLASK/arinc_dashboard
source .venv/bin/activate
export ARINC_SOURCE_MODE=bridge
export ARINC_BRIDGE_URL=http://127.0.0.1:5100
export ARINC_DASHBOARD_HOST=0.0.0.0
export ARINC_DASHBOARD_PORT=5000
python3 app.py
```

## Red recomendada

Para operacion estable se recomienda:

- Raspberry Pi 3B+ por Ethernet directo a la notebook
- IP de la 3B+:
  - `192.168.50.2/24`
- IP de la notebook:
  - `192.168.50.1/24`

Con esa topologia, desde la notebook:

- Flask:
  - `http://192.168.50.2:5000`
- bridge:
  - `http://192.168.50.2:5100`

## Herramientas auxiliares

Se incluyen scripts para diagnostico y capacidad:

- `spi_link_diagnostic.py`
- `spi_capacity_runner.py`
- `gpio_spi_continuity_test.py`
- `spi_byte_proto_test.py`

## Nota

El bridge actual trabaja por polling. `DRDY` existe como opcion fisica, pero no es requisito para la arquitectura validada hasta ahora.
