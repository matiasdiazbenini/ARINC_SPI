# Grafana + Prometheus para ARINC

Esta integracion deja a Flask como tablero operativo rapido y a Prometheus/Grafana como capa de observabilidad historica.

## 1. Flujo recomendado

- `ARINC-SNIFFER` entrega datos al bridge por SPI
- `spi_sniffer_bridge.py` publica `/metrics`
- Flask consume al bridge en `http://127.0.0.1:5100`
- Prometheus, corriendo en la notebook, scrapea:
  - `http://192.168.50.2:5100/metrics`
- Grafana consulta a Prometheus en la notebook

## 2. Ejecutar la fuente de datos en la 3B+

### Bridge

```bash
cd ~/PAMPA/HOST_3b+
source .venv/bin/activate
python3 spi_sniffer_bridge.py
```

### Flask en modo bridge

```bash
cd ~/PAMPA/DASHBOARD_WEB/arinc_dashboard
source .venv/bin/activate
export ARINC_SOURCE_MODE=bridge
export ARINC_BRIDGE_URL=http://127.0.0.1:5100
export ARINC_DASHBOARD_HOST=0.0.0.0
export ARINC_DASHBOARD_PORT=5000
python3 app.py
```

## 3. Verificacion

Desde la notebook deberia responder:

- dashboard:
  - `http://192.168.50.2:5000`
- metricas del bridge:
  - `http://192.168.50.2:5100/metrics`

## 4. Ejecutar Prometheus en Windows

Usar la configuracion recomendada:

- `../../prometheus/prometheus.yml`

Comando:

```powershell
cd "C:\Users\usuario\RASPI PICO\PAMPA\prometheus-3.11.2.windows-amd64"
.\prometheus.exe --config.file=prometheus.yml
```

Prometheus queda en:

- `http://127.0.0.1:9090`

## 5. Grafana

- abrir `http://127.0.0.1:3000`
- data source tipo `Prometheus`
- URL:
  - `http://127.0.0.1:9090`

## 6. Consultas utiles

```promql
arinc_latest_value{name="TEMPERATURA"}
```

```promql
arinc_latest_value{name="VELOCIDAD"}
```

```promql
arinc_latest_value{name="ALTITUD"}
```

```promql
arinc_ack_events_total
```

```promql
arinc_filtered_words_total
```

```promql
arinc_resync_events_total{channel="FWD"}
```

```promql
arinc_resync_events_total{channel="REV"}
```
