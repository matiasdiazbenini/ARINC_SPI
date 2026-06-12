# Prometheus

Configuracion de Prometheus utilizada desde la notebook para scrapear las metricas ARINC expuestas por Flask en la Raspberry Pi 3B+.

## Objetivo

Persistir series temporales del sistema y servirlas a Grafana.

## Configuracion actual

Archivo:

- [prometheus.yml](prometheus.yml)

Target recomendado:

- `192.168.50.2:5000`

con:

- `scrape_interval: 1s`
- `evaluation_interval: 1s`

El bridge SPI/HTTP queda local en la Raspberry Pi 3B+ (`127.0.0.1:5100`). Flask consulta ese bridge por loopback y expone `/metrics` hacia la notebook.

## Comando en Windows

```powershell
cd "C:\Users\usuario\RASPI PICO\PAMPA\prometheus-3.11.2.windows-amd64"
.\prometheus.exe --config.file=prometheus.yml
```

## Verificacion

Abrir:

- `http://127.0.0.1:9090/targets`

y comprobar que el job `arinc_dashboard` este en `UP`.
