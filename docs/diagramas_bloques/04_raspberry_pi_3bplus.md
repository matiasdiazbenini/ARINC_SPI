# Diagrama interno: Raspberry Pi 3B+ y visualizacion

## 1. Funcion de la placa

La Raspberry Pi 3B+ cumple cuatro funciones:

1. Actua como maestro SPI del ARINC-SNIFFER.
2. Mantiene una imagen en memoria de estadisticas, filtro y ultimos valores.
3. Expone esos datos por HTTP local mediante el bridge.
4. Sirve el dashboard Flask hacia la notebook por Ethernet.

La captura ARINC no ocurre en Flask. Ocurre en el RP2040 del sniffer.

## 2. Arquitectura de procesos y red

```mermaid
flowchart LR
    SN["ARINC-SNIFFER<br/>SPI slave PIO-frame"] -->|"response SPI"| BR["Proceso 1<br/>spi_sniffer_bridge.py<br/>127.0.0.1:5100"]
    BR -->|"request SPI0.0<br/>8 MHz"| SN

    BR -->|"JSON de respuesta"| FL["Proceso 2<br/>Flask dashboard app.py<br/>0.0.0.0:5000"]
    FL -->|"HTTP GET/POST por<br/>127.0.0.1"| BR

    FL -->|"Ethernet<br/>192.168.50.2:5000"| WEB["Navegador notebook"]
    PROM["Prometheus notebook<br/>192.168.50.1"] -->|"GET /metrics cada 1 s"| FL
    PROM --> GRAF["Grafana notebook"]
```

Direcciones recomendadas:

| Equipo | Interfaz | Direccion |
|---|---|---|
| Notebook | Ethernet | `192.168.50.1/24` |
| Raspberry Pi | `eth0` | `192.168.50.2/24` |
| Bridge | loopback | `127.0.0.1:5100` |
| Dashboard | todas las interfaces | `0.0.0.0:5000` |

El bridge no necesita una IP publica. Flask accede a el por `127.0.0.1`, que
recorre la pila TCP/IP local de Linux sin salir por Ethernet. No se usa un Unix
domain socket.

## 3. Camino SPI fisico

| Raspberry Pi | Sniffer | Funcion |
|---|---|---|
| MOSI | GP16 | Request hacia el sniffer |
| CE0 / GPIO8 | GP17 | Chip select manual |
| SCLK | GP18 | Reloj generado por la Pi |
| MISO | GP19 | Response hacia la Pi |
| GPIO25 | GP20 | DRDY, aviso de snapshot nuevo |
| GND | GND | Referencia comun |

Configuracion operativa validada:

| Parametro | Valor |
|---|---|
| Transfer mode | `pio-frame` |
| Frecuencia | 8 MHz |
| Request mode | `drdy` con fallback por timeout |
| CS | Manual por GPIO8 |
| Setup / hold CS | 100 us / 100 us |
| Delay entre request y response | 2 ms |
| Poll del bridge | 6 ms |
| Refresh de stats | 60 ms |

## 4. Una operacion SPI completa

```mermaid
sequenceDiagram
    participant W as Worker bridge
    participant CS as GPIO8 / CS manual
    participant S as spidev0.0
    participant P as Pico sniffer

    W->>W: construir paquete de 32 bytes
    W->>W: sequence++ y CRC16
    W->>CS: llevar CS a 0
    W->>S: xfer2(request[32])
    S->>P: MOSI + SCK a 8 MHz
    W->>CS: llevar CS a 1
    W->>W: esperar 2 ms
    W->>CS: llevar CS a 0
    W->>S: xfer2(idle[32])
    P-->>S: response[32] por MISO
    W->>CS: llevar CS a 1
    W->>W: validar magic, version, CRC y sequence
```

El control manual de CS se realiza con `pinctrl` o `raspi-gpio`. El objetivo es
dar a la state machine del sniffer limites de trama inequivocos.

## 5. Bloques internos del bridge

```mermaid
flowchart TD
    WORKER["Thread worker"] --> LOCK["bridge_lock<br/>serializa toda operacion SPI"]
    LOCK --> CLIENT["SpiSnifferClient"]
    CLIENT --> SPI["spidev0.0 + GPIO8"]

    CLIENT --> GSTATS["GET_STATS cada 60 ms"]
    CLIENT --> GMETA["GET_LATEST_META cada 6 ms"]
    GMETA --> CHANGED{"Cambio snapshot_revision?"}
    CHANGED -->|si| GSLOTS["GET_LATEST_SLOT<br/>para cada slot"]
    CHANGED -->|no| SLEEP["Esperar siguiente poll"]
    CLIENT --> GFILTER["GET_FILTER cada 2 s"]

    GSTATS --> DLOCK["data_lock"]
    GSLOTS --> DLOCK
    GFILTER --> DLOCK

    DLOCK --> STATE["bridge_state<br/>diccionario en RAM"]
    DLOCK --> SLOTS["latest_slots<br/>ultimo estado"]
    DLOCK --> RECORDS["deque records<br/>maximo 500"]

    STATE --> HTTP["Flask del bridge<br/>127.0.0.1:5100"]
    SLOTS --> HTTP
    RECORDS --> HTTP
```

### Locks

| Lock | Protege |
|---|---|
| `bridge_lock` | Evita dos transacciones SPI simultaneas |
| `data_lock` | Evita leer estado mientras otro thread lo actualiza |

Los endpoints de filtro y reset tambien toman `bridge_lock`, por lo que no
pueden intercalar sus bytes con el polling de fondo.

## 6. Actualizacion del snapshot en la Pi

```mermaid
flowchart TD
    META["GET_LATEST_META"] --> REV{"revision distinta?"}
    REV -->|no| NOCHANGE["No copiar slots"]
    REV -->|si| LOOP["Leer slot 0..slot_count-1"]
    LOOP --> CMP{"update_counter<br/>cambio?"}
    CMP -->|no| NEXT["Siguiente slot"]
    CMP -->|si| REC["Convertir slot a record"]
    REC --> DEQUE["append a deque maxlen 500"]
    DEQUE --> NEXT
    NEXT --> STATE["Actualizar latest_slots<br/>y snapshot_revision"]
```

`hit_count` contiene la cantidad acumulada por label. `update_counter` permite
detectar si el slot recibio una nueva palabra desde la consulta anterior,
aunque la magnitud se haya repetido. El bridge agrega un registro reciente
cuando detecta una revision nueva del slot.

No hay una FIFO HTTP. Existen:

- FIFO de hardware en el PIO del sniffer;
- cola circular de captura en el sniffer;
- `deque` de hasta 500 registros en la RAM de la Raspberry;
- diccionarios protegidos por mutex;
- sockets TCP administrados por Linux para las peticiones HTTP.

## 7. Endpoints del bridge

| Endpoint | Metodo | Funcion |
|---|---|---|
| `/health` | GET | Estado basico del bridge |
| `/stats` | GET | Estadisticas combinadas |
| `/records` | GET | Ventana de registros recientes |
| `/filter` | GET | Leer filtro |
| `/filter` | POST | Cambiar filtro del sniffer |
| `/control/reset` | POST | Reiniciar estadisticas |
| `/metrics` | GET | Metricas Prometheus |

`/stats` combina:

- contadores obtenidos con `GET_STATS`;
- `hit_count` de los slots;
- ultimos valores escalados;
- estado del bridge;
- errores SPI de arranque y operativos.

## 8. Clasificacion de errores SPI

```mermaid
flowchart TD
    XFER["Transaccion invalida"] --> ARMED{"Ya hubo una respuesta<br/>SPI valida?"}
    ARMED -->|no| START["spi_startup_errors++"]
    ARMED -->|si| OPER["spi_errors++"]
    VALID["Primera respuesta valida"] --> SET["spi_error_armed = true"]
```

Esta separacion evita presentar como errores operativos los intentos realizados
cuando el bridge ya esta activo pero el sniffer todavia esta apagado. En la
prueba validada de 9 horas a 8 MHz hubo:

- `spi_errors = 0`;
- `spi_startup_errors = 1`;
- `spi_drop_events = 0`;
- `overflow_events = 0`.

## 9. Dashboard Flask

```mermaid
flowchart LR
    BROWSER["JavaScript del navegador<br/>refresh 33 ms"] -->|GET /stats| DASH["Flask dashboard<br/>puerto 5000"]
    DASH -->|urllib HTTP| BRIDGE["Bridge<br/>127.0.0.1:5100"]
    BRIDGE -->|JSON| DASH
    DASH -->|JSON| BROWSER
```

El dashboard es otro proceso Python. En modo `bridge` no abre SPI ni duplica el
driver. Solo consulta y controla al bridge mediante HTTP local.

Esto permite:

- reiniciar Flask sin reiniciar el enlace SPI;
- mantener un unico propietario de `spidev0.0`;
- aislar la captura de la interfaz grafica;
- cambiar la visualizacion sin modificar firmware.

## 10. Prometheus y Grafana

```mermaid
sequenceDiagram
    participant P as Prometheus notebook
    participant F as Flask Pi puerto 5000
    participant B as Bridge Pi puerto 5100
    participant G as Grafana

    loop cada 1 segundo
        P->>F: GET /metrics
        F->>B: GET /metrics por 127.0.0.1
        B-->>F: texto Prometheus
        F-->>P: texto Prometheus
    end
    G->>P: consultas temporales
    P-->>G: series e historico
```

Flask es la vista operativa inmediata. Prometheus conserva series temporales y
Grafana permite analizarlas a lo largo de horas o dias.

## 11. Frecuencias SPI

El barrido medido mostro:

| Frecuencia | Resultado |
|---:|---|
| 100 kHz a 8 MHz | Perfecto en el barrido |
| 9 MHz | Perfecto en prueba corta |
| 10 MHz | Parcial, 13/24 |
| 16 MHz | 0/24, cabecera desplazada un bit |
| 50 MHz | 0/24, respuestas en cero |

La capacidad teorica maxima del controlador SPI no garantiza que el sistema
completo funcione a esa frecuencia. Tambien intervienen:

- temporizacion de las state machines PIO;
- latencia para preparar la respuesta;
- control de CS;
- cableado y calidad de senal;
- tiempos de setup/hold;
- orden de bits y sincronizacion de la primera muestra.

Por eso 8 MHz es el punto operacional validado, no una limitacion derivada de
los 100 kbps del ARINC. Son enlaces independientes y cumplen tareas distintas.

## 12. Recorrido completo de una medicion

```mermaid
flowchart LR
    A["Palabra FWD<br/>100 kbps"] --> B["Sniffer PIO RX"]
    B --> C["Cola 8192"]
    C --> D["Filtro y snapshot"]
    D --> E["GET_LATEST_SLOT<br/>SPI 8 MHz"]
    E --> F["bridge_state + deque"]
    F --> G["HTTP 127.0.0.1"]
    G --> H["Flask :5000"]
    H --> I["Navegador"]
    H --> J["Prometheus"]
    J --> K["Grafana"]
```

Cada frontera tiene una responsabilidad definida: PIO conserva temporizacion,
la CPU del sniffer valida, SPI transporta estado, el bridge centraliza acceso y
Flask presenta la informacion.

## 13. Registro de evidencia

`stats_recorder.py` es un tercer proceso opcional. Solo consulta `/stats` por
loopback y no accede a SPI:

```mermaid
flowchart LR
    BR["Bridge 127.0.0.1:5100"] -->|JSON cada 5 s| REC["stats_recorder.py"]
    REC --> CSV["samples.csv"]
    REC --> EVENTS["events.csv"]
    REC --> JSON["metadata + summary"]
    REC --> PDF["report.pdf"]
```

Puede finalizar por duracion, cantidad de palabras, orden manual o desconexion
sostenida. Los archivos se guardan en `~/PAMPA/ARINC_RESULTS`.

## 14. Fuentes de implementacion

- [`HOST_3b+/spi_sniffer_bridge.py`](../../HOST_3b+/spi_sniffer_bridge.py)
- [`HOST_3b+/sniffer_spi_protocol.py`](../../HOST_3b+/sniffer_spi_protocol.py)
- [`HOST_3b+/stats_recorder.py`](../../HOST_3b+/stats_recorder.py)
- [`HOST_3b+/systemd/arinc-sniffer-bridge.service`](../../HOST_3b+/systemd/arinc-sniffer-bridge.service)
- [`DASHBOARD_WEB/arinc_dashboard/app.py`](../../DASHBOARD_WEB/arinc_dashboard/app.py)
- [`prometheus/prometheus.yml`](../../prometheus/prometheus.yml)
