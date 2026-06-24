# HOST_3b+

Bridge HTTP que corre en la Raspberry Pi 3B+ y consume por SPI los datos filtrados entregados por `ARINC-SNIFFER`.

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

El endpoint `/stats` tambien informa la configuracion SPI activa y separa:

- `fwd_startup_resync_events`
- `fwd_operational_resync_events`

## Parametros recomendados

El perfil operativo continuo que mejor resultado dio fue:

- `ARINC_SPI_HZ = 800000`
- `ARINC_SPI_BYTE_DELAY_US = 25`
- `ARINC_SPI_POLL_SEC = 0.006`
- `ARINC_SPI_STATS_SEC = 0.06`
- `ARINC_SPI_RESPONSE_DELAY_SEC = 0.00005`

## Despliegue basico en la 3B+

```bash
cd ~/PAMPA/HOST_3b+
python3 -m venv .venv --system-site-packages
source .venv/bin/activate
pip install -r requirements.txt
python3 spi_sniffer_bridge.py
```

Por defecto queda disponible en:

- `http://127.0.0.1:5100`

El bridge esta pensado para quedar local en la Raspberry Pi 3B+ y ser consumido por Flask mediante loopback. Para operacion normal desde la notebook se recomienda entrar al dashboard Flask, no al bridge directamente.

## Variables de entorno relevantes

- `ARINC_SPI_BUS`
- `ARINC_SPI_DEVICE`
- `ARINC_SPI_HZ`
- `ARINC_SPI_TRANSFER_MODE`
- `ARINC_SPI_BYTE_DELAY_US`
- `ARINC_SPI_POLL_SEC`
- `ARINC_SPI_STATS_SEC`
- `ARINC_SPI_FILTER_SEC`
- `ARINC_SPI_RESPONSE_DELAY_SEC`
- `ARINC_SPI_RESPONSE_RETRIES`
- `ARINC_BRIDGE_PORT`
- `ARINC_BRIDGE_HOST`

`ARINC_SPI_TRANSFER_MODE` puede tomar:

- `byte`:
  - fallback estable validado
  - mantiene transferencias byte-a-byte con pacing
  - es compatible con el transporte SPI hardware previo
- `frame`:
  - modo experimental
  - sincroniza con `RESET`
  - envia el request SPI de 32 bytes como trama completa
  - lee la respuesta como ventana completa
  - no es el modo recomendado con el SPI slave hardware de la Pico
- `pio-frame`:
  - modo recomendado post tutor para tramas completas
  - requiere cargar en la sniffer el target `sniffer_arinc429_logic_pio_frame`
  - usa `CS` como delimitador de paquete de 32 bytes
  - se probo con CS manual por GPIO8 en la 3B+

## Integracion con Flask

Desde la 3B+:

```bash
cd ~/PAMPA/DASHBOARD_WEB/arinc_dashboard
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

Para dejar la IP fija versionada en la 3B+:

```bash
sudo bash ~/PAMPA/HOST_3b+/configurar_eth0_ip_fija.sh
```

El script configura `eth0` como `192.168.50.2/24`. Si la Raspberry Pi usa
NetworkManager, crea/actualiza una conexion `arinc-eth0-static`. Si usa
`dhcpcd`, actualiza `/etc/dhcpcd.conf` dejando backup.

Con esa topologia, desde la notebook:

- Flask:
  - `http://192.168.50.2:5000`

El bridge queda en `127.0.0.1:5100` dentro de la 3B+. Si se necesita exponerlo solo para diagnostico puntual, se puede lanzar con `ARINC_BRIDGE_HOST=0.0.0.0`, pero no es el modo recomendado.

## Autoarranque con systemd

Los unit files versionados son:

- `systemd/arinc-sniffer-bridge.service`
- `systemd/arinc-dashboard-flask.service`

Instalacion:

```bash
cd ~/PAMPA/HOST_3b+
sudo cp systemd/arinc-sniffer-bridge.service /etc/systemd/system/
sudo cp systemd/arinc-dashboard-flask.service /etc/systemd/system/
sudo systemctl daemon-reload
sudo systemctl enable arinc-sniffer-bridge arinc-dashboard-flask
sudo systemctl restart arinc-sniffer-bridge arinc-dashboard-flask
```

Verificacion:

```bash
systemctl status arinc-sniffer-bridge arinc-dashboard-flask --no-pager
curl http://127.0.0.1:5100/stats
curl http://127.0.0.1:5000/stats
```

## Transporte SPI recomendado

El modo recomendado post tutor para responder la objecion de "byte-a-byte" es:

```bash
export ARINC_SPI_TRANSFER_MODE=pio-frame
export ARINC_SPI_MANUAL_CS=1
export ARINC_SPI_CS_SETUP_US=100
export ARINC_SPI_CS_HOLD_US=100
export ARINC_SPI_HZ=8000000
export ARINC_SPI_BYTE_DELAY_US=0
export ARINC_SPI_FRAME_RESPONSE_DELAY_SEC=0.002
```

Este perfil fue validado durante aproximadamente `9 horas`, con mas de `18.29 M` palabras aceptadas y `spi_errors = 0`. Para una puesta en marcha especialmente conservadora puede usarse `ARINC_SPI_HZ=400000`.

Este modo requiere que la Pico sniffer use el target:

```text
sniffer_arinc429_logic_pio_frame
```

Build tag esperado por USB:

```text
SPI-PIOFRAME-ARINC429
```

La prueba de `frame` completo contra el SPI slave hardware mostro desincronizacion. El problema quedo aislado al uso del bloque SPI slave hardware de la Pico para este caso. La solucion validada fue implementar el slave SPI por PIO y usar `ARINC_SPI_TRANSFER_MODE=pio-frame`.

El modo `byte` queda como fallback estable:

```bash
export ARINC_SPI_TRANSFER_MODE=byte
export ARINC_SPI_HZ=800000
export ARINC_SPI_BYTE_DELAY_US=25
```

## Errores SPI de arranque

El bridge separa los errores SPI de arranque de los errores operativos:

- `spi_startup_errors`:
  - errores ocurridos antes de la primera comunicacion SPI valida con el sniffer
  - sirven para diagnosticar que el bridge arranco antes que la Pico
- `spi_errors`:
  - errores ocurridos despues de que el enlace SPI ya tuvo al menos una comunicacion valida
  - son los errores relevantes para una corrida estable

Asi se puede dejar el bridge arrancado antes de alimentar las Pico sin ensuciar el contador operativo de la prueba.

## Registro estadistico de pruebas

`stats_recorder.py` consulta el bridge por HTTP y guarda cada ensayo fuera del
repositorio, en `~/PAMPA/ARINC_RESULTS`.

Prueba de 12 horas en segundo plano:

```bash
cd ~/PAMPA/HOST_3b+
source .venv/bin/activate

nohup python3 stats_recorder.py start \
  --name stream_8mhz_12h \
  --duration 12h \
  --interval 5 \
  > stats_recorder.log 2>&1 &
```

Estado y cierre manual:

```bash
python3 stats_recorder.py status
python3 stats_recorder.py stop
```

Cada sesion produce `samples.csv`, `events.csv`, `metadata.json`,
`summary.json` y `report.pdf`.

Procedimiento completo:

- [../docs/procedimiento_registro_estadistico.md](../docs/procedimiento_registro_estadistico.md)

## Herramientas auxiliares

Se incluyen scripts para diagnostico y capacidad:

- `spi_link_diagnostic.py`
- `spi_frequency_sweep.py`
- `spi_capacity_runner.py`
- `stats_recorder.py`
- `gpio_spi_continuity_test.py`
- `spi_byte_proto_test.py`

Prueba recomendada del enlace SPI por tramas completas:

```bash
cd ~/PAMPA/HOST_3b+
source .venv/bin/activate
python3 spi_link_diagnostic.py \
  --transfer-mode pio-frame \
  --hz-list 100000,200000,400000,800000 \
  --delay-ms-list 2,5,10 \
  --byte-delay-us 0 \
  --manual-cs \
  --cs-setup-us 100 \
  --cs-hold-us 100
```

Prueba fallback del enlace SPI byte-a-byte:

```bash
cd ~/PAMPA/HOST_3b+
source .venv/bin/activate
python3 spi_link_diagnostic.py --transfer-mode byte --hz-list 800000,400000,200000 --delay-ms-list 0,2,5
```

Barrido estricto de frecuencia PIO-frame:

```bash
cd ~/PAMPA/HOST_3b+
source .venv/bin/activate

pkill -f '[s]pi_sniffer_bridge.py'

python3 spi_frequency_sweep.py \
  --hz-list 100000,200000,400000,800000,1000000,2000000,4000000,8000000,16000000,32000000,50000000 \
  --attempts 24 \
  --warmup-attempts 3 \
  --retries 1 \
  --delay-ms 2 \
  --manual-cs \
  --cs-setup-us 100 \
  --cs-hold-us 100
```

El bridge debe estar detenido durante esta prueba para evitar dos procesos accediendo a `/dev/spidev0.0`. El runner guarda un reporte JSON y considera `PASS` solamente un resultado perfecto sin reintentos ocultos, con `GET_STATS` valido.

El barrido se detiene en el primer punto no perfecto. Si ocurre antes de `50 MHz`, reiniciar la Pico sniffer y ejecutar `50 MHz` como punto aislado. Esto evita que una perdida de flancos deje el PIO desalineado y contamine las frecuencias siguientes.

## Nota

El bridge actual trabaja por polling. `DRDY` existe como opcion fisica, pero no es requisito para la arquitectura validada hasta ahora.
