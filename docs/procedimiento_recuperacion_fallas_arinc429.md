# Recuperacion ante fallas ARINC/SPI

Este documento resume la recuperacion agregada despues de las pruebas de
desconexion y reinicio.

## Capas de recuperacion

### 1. Sniffer: recuperacion de fase ARINC

Si el sniffer arranca o se reconecta en medio de una palabra, puede quedar
tomando grupos de 32 bits corridos. Electricamente ve pulsos, pero a nivel
ARINC las palabras no cierran por paridad/filtro.

La build vigente:

```text
SPI-PIOFRAME-ARINC429-STRICTPARITY-DRDY-AUTORATE-RECOVERY-V5
```

resuelve esto reiniciando la PIO de captura solo despues de ver reposo
electrico del canal:

- a `100 kbps`: espera reposo estable de aproximadamente `20 us`
- a `12.5 kbps`: espera reposo estable de aproximadamente `120 us`

Ese reposo corresponde al espacio nulo entre palabras, no a la mitad nula de
un bit. Por eso la PIO vuelve a entrar alineada al comienzo de una palabra.

### 2. Bridge: recuperacion de transporte SPI

En modo `pio-frame`, el bridge y el sniffer intercambian tramas completas de
`32 bytes`.

Para recuperar arranques desfasados, el bridge envia una trama de reset:

```text
0xF0 repetido 32 veces
```

La sniffer reconoce esa trama como reset de transporte, vuelve a estado idle y
precarga una respuesta idle. El bridge la usa:

- al abrir SPI por primera vez
- despues de una respuesta invalida
- despues de reabrir SPI por error

El contador visible es:

```text
/stats -> spi_transport_resets
/metrics -> arinc_spi_transport_resets_total
```

### 3. 3B+: modo dios / supervisor de servicios

El servicio `arinc-supervisor.service` corre separado del bridge. Cada `5 s`:

- verifica que `arinc-sniffer-bridge` y `arinc-dashboard-flask` esten activos
- consulta `http://127.0.0.1:5100/stats`
- clasifica el estado del sistema
- aplica acciones escalonadas si hay fallas repetidas

No participa del camino de datos SPI ni del dashboard. Su carga es baja porque
hace una consulta HTTP cada varios segundos.

Estados posibles:

| Estado | Significado |
|---|---|
| `BOOTING` | El supervisor acaba de arrancar |
| `WAITING_SNIFFER` | Todavia no hay primera palabra valida |
| `SPI_SYNCING` | El bridge esta intentando sincronizar SPI |
| `RUNNING` | Sistema operativo y con progreso |
| `ARINC_STALLED` | Hay enlace SPI, pero no avanzan palabras aceptadas |
| `CABLE_FAULT` | Probable falla fisica: cable, masa, TX/RX apagado |
| `BRIDGE_FAULT` | Falla HTTP, proceso bridge o protocolo persistente |
| `DASHBOARD_FAULT` | Flask no esta activo |
| `RECOVERING` | El supervisor esta ejecutando una accion correctiva |

Acciones:

1. Ante falla de protocolo SPI con bridge vivo:
   - llama `POST /control/recover_spi`
   - esto envia reset de transporte sin reiniciar procesos
2. Si la falla persiste:
   - reinicia `arinc-sniffer-bridge`
   - reinicia `arinc-dashboard-flask`
3. Ante falla fisica probable:
   - registra `CABLE_FAULT`
   - no entra en reinicios infinitos

## Instalacion en la 3B+

```bash
cd ~/PAMPA/HOST_3b+

sudo cp systemd/arinc-sniffer-bridge.service /etc/systemd/system/
sudo cp systemd/arinc-dashboard-flask.service /etc/systemd/system/
sudo cp systemd/arinc-supervisor.service /etc/systemd/system/

sudo systemctl daemon-reload
sudo systemctl enable arinc-sniffer-bridge arinc-dashboard-flask arinc-supervisor
sudo systemctl restart arinc-sniffer-bridge arinc-dashboard-flask arinc-supervisor
```

## Verificacion rapida

```bash
systemctl status arinc-sniffer-bridge arinc-dashboard-flask arinc-supervisor --no-pager
curl http://127.0.0.1:5100/stats
curl http://127.0.0.1:5100/supervisor/status
journalctl -u arinc-supervisor -n 40 --no-pager
```

## Alcance real

Esto recupera:

- reinicio del bridge
- reinicio de Flask
- reinicio de la Raspberry Pi 3B+
- desalineacion SPI por arranque fuera de fase
- desalineacion ARINC por reconexion o arranque del sniffer en mitad de palabra

Esto no puede resolver por software:

- cable fisicamente desconectado
- masa comun perdida
- alimentacion de una Pico apagada
- pin mal conectado

En esos casos el supervisor puede registrar la condicion y reiniciar servicios,
pero la recuperacion completa depende de volver a tener el enlace electrico
correctamente conectado.
