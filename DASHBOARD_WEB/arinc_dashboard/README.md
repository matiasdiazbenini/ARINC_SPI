# DASHBOARD_WEB / arinc_dashboard

Dashboard web principal del sistema.

## Rol

- consultar por loopback el bridge HTTP que corre en la Raspberry Pi 3B+
- mostrar variables actuales, estado y estadisticas
- ofrecer una vista operativa mas fluida que Grafana para uso en vivo
- exponer hacia la notebook el dashboard y las metricas `/metrics`

## Modo recomendado

Se usa en modo `bridge`:

```bash
export ARINC_SOURCE_MODE=bridge
export ARINC_BRIDGE_URL=http://127.0.0.1:5100
export ARINC_DASHBOARD_HOST=0.0.0.0
export ARINC_DASHBOARD_PORT=5000
python3 app.py
```

## Refresh recomendado

El valor que mejor resultado dio fue:

- `33 ms`

No se recomienda usar el perfil de estres continuo salvo para pruebas de limite.

## Acceso desde la notebook

Con la 3B+ por Ethernet directo:

- `http://192.168.50.2:5000`

El bridge queda local en la 3B+:

- `http://127.0.0.1:5100`

La notebook no necesita acceder a `:5100` para operacion normal.

## Grafana / Prometheus

La integracion historica con Prometheus/Grafana queda explicada en:

- [GRAFANA_PROMETHEUS.md](GRAFANA_PROMETHEUS.md)

Target recomendado para Prometheus desde la notebook:

- `192.168.50.2:5000/metrics`
