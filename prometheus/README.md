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

## Arranque recomendado en Windows

Usar el script del repo desde PowerShell:

```powershell
cd "C:\Users\usuario\RASPI PICO\PAMPA\ARINC_SPI\prometheus"
powershell -ExecutionPolicy Bypass -File .\start_prometheus_grafana.ps1
```

El script:

- Busca `prometheus.exe` en `C:\Users\usuario\RASPI PICO\PAMPA\prometheus-*.windows-amd64\`.
- Busca `grafana-server.exe` en `C:\Users\usuario\RASPI PICO\PAMPA\grafana*\bin\` o en la instalacion normal de Windows.
- Usa `prometheus.yml` de esta carpeta.
- Guarda la base de datos de Prometheus en `C:\Users\usuario\RASPI PICO\PAMPA\prometheus-data`.
- Guarda logs en `C:\Users\usuario\RASPI PICO\PAMPA\observability-logs`.
- Deja ambos procesos corriendo en segundo plano, sin bloquear la terminal.

Si Grafana no se detecta automaticamente:

```powershell
powershell -ExecutionPolicy Bypass -File .\start_prometheus_grafana.ps1 -GrafanaHome "C:\ruta\a\grafana"
```

Para ver estado:

```powershell
powershell -ExecutionPolicy Bypass -File .\start_prometheus_grafana.ps1 -Status
```

Para detener Prometheus y Grafana:

```powershell
powershell -ExecutionPolicy Bypass -File .\start_prometheus_grafana.ps1 -Stop
```

URLs locales en la notebook:

- Prometheus: `http://127.0.0.1:9090`
- Targets de Prometheus: `http://127.0.0.1:9090/targets`
- Grafana: `http://127.0.0.1:3000`

## Comando manual en Windows

```powershell
cd "C:\Users\usuario\RASPI PICO\PAMPA\prometheus-3.11.2.windows-amd64"
.\prometheus.exe --config.file=prometheus.yml
```

## Verificacion

Abrir:

- `http://127.0.0.1:9090/targets`

y comprobar que el job `arinc_dashboard` este en `UP`.
