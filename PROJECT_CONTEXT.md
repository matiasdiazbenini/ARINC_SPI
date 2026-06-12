# PROJECT_CONTEXT

Este archivo existe para que futuras sesiones de Codex, VS Code, Codex app o cualquier otra persona puedan retomar el proyecto sin depender del chat historico.

## 1. Objetivo del proyecto

Recrear una arquitectura de comunicacion inspirada en ARINC 429 en un entorno de laboratorio, validando por fases:

1. transmision entre nodos Pico
2. recepcion y ACK
3. sniffing pasivo
4. integracion host en Raspberry Pi 3B+
5. visualizacion y observabilidad
6. validacion instrumental por osciloscopio
7. futura transicion a interfaz electrica ARINC 429 real

## 2. Arquitectura actual

### Nodos

- Pico `ARINC-TX`
  - transmite palabras sobre `FWD`
- Pico `ARINC-RX`
  - recibe palabras y devuelve ACK sobre `REV`
- Pico `ARINC-SNIFFER`
  - observa ambas direcciones
  - nunca transmite sobre lineas ARINC
  - solo transmite hacia la 3B+ por `SPI MISO`, fuera del enlace ARINC
  - filtra por `label + SDI`
  - exporta snapshot y estadisticas por SPI
- Raspberry Pi 3B+
  - corre `spi_sniffer_bridge.py`
  - corre dashboard Flask
- Notebook
  - consulta el dashboard
  - corre Prometheus y Grafana

### Enlaces

- `master <-> slave`
  - 2 enlaces simplex logicos:
    - `FWD`: `GP2 / GP3`
    - `REV`: `GP4 / GP5`
- `sniffer <-> 3B+`
  - SPI
- `3B+ <-> notebook`
  - Ethernet directo recomendado

### Layout recomendado del repositorio

La rama `ARINC` organiza el sistema completo en un solo repo con subcarpetas por componente:

- `ARINC-TX/`
- `ARINC-RX/`
- `ARINC-SNIFFER/`
- `HOST_3b+/`
- `DASHBOARD_WEB/arinc_dashboard/`
- `prometheus/`
- `imagenes/`
- `docs/`

## 3. Estado funcional validado

La fase `arinc429_logic` se considera validada.

Se comprobo:

- sistema estable en corridas largas
- corrida post tutor con `spi_errors = 0` en regimen estable
- separacion entre `spi_startup_errors` y `spi_errors`
- transporte SPI por tramas completas validado con PIO-frame
- aceptacion estricta por paridad implementada en RX y SNIFFER
- resync FWD separados entre arranque y regimen operativo
- registrador CSV/JSON/PDF agregado para pruebas en la 3B+
- target recomendado del sniffer logico: `sniffer_arinc429_logic_pio_frame`
- corrida PIO-frame de 6-7 horas con `spi_errors = 0`
- labels utiles detectados:
  - `0xA5 TEMPERATURA`
  - `0xB1 VELOCIDAD`
  - `0xC2 ALTITUD`
  - `0xAC ACK_BATCH`
- `ACK` funcional
- dashboard Flask estable
- bridge SPI/HTTP estable
- Prometheus/Grafana funcionales
- validacion por osciloscopio en FWD y REV

## 4. Parametros operativos recomendados

### Enlace master/slave

- `BIT_RATE_HZ = 100000`
- bit time:
  - `10 us`
- media celda activa:
  - `5 us`
- segunda media celda:
  - retorno a cero

### Bridge / dashboard recomendado

- perfil PIO-frame post tutor:
  - `ARINC_SPI_TRANSFER_MODE = pio-frame`
  - `ARINC_SPI_MANUAL_CS = 1`
  - `ARINC_SPI_CS_SETUP_US = 100`
  - `ARINC_SPI_CS_HOLD_US = 100`
  - `ARINC_SPI_HZ = 8000000`
  - `ARINC_SPI_BYTE_DELAY_US = 0`
  - validado durante aproximadamente `9 horas` con `spi_errors = 0`
- fallback PIO-frame conservador:
  - `ARINC_SPI_HZ = 400000`
- perfil byte validado como fallback:
  - `ARINC_SPI_TRANSFER_MODE = byte`
  - `ARINC_SPI_HZ = 800000`
  - `ARINC_SPI_BYTE_DELAY_US = 25`

Perfil historico estable previo al PIO-frame:

- `ARINC_SPI_HZ = 800000`
- `ARINC_SPI_TRANSFER_MODE = byte`
- `ARINC_SPI_BYTE_DELAY_US = 25`
- `ARINC_SPI_POLL_SEC = 0.006`
- `ARINC_SPI_STATS_SEC = 0.06`
- `ARINC_SPI_RESPONSE_DELAY_SEC = 0.00005`
- refresh Flask:
  - `33 ms`

### Perfil de estres probado

- SPI a `1.2 MHz`
- refresh Flask a `25 ms`

Sirvio para encontrar el borde, pero no se recomienda para operacion continua.

## 5. Comparacion clave normal vs stress

Corridas largas historicas comparables:

- modo recomendado:
  - `accepted_words ~ 20.1M`
  - `spi_errors = 42`
- modo stress:
  - `accepted_words ~ 18.3M`
  - `spi_errors = 2074`

Conclusion:

- el modo recomendado mueve mas palabras
- con muchisimos menos errores SPI
- el modo stress queda solo como referencia de limite

Estado posterior a la correccion post tutor:

- el bridge separa errores de arranque y errores operativos
- `spi_startup_errors` registra intentos antes de que la Pico sniffer responda
- `spi_errors` cuenta solo despues de la primera comunicacion SPI valida
- corrida de referencia:
  - `accepted_words = 3,495,121`
  - `ack_events = 11,611`
  - `spi_startup_errors = 19`
  - `spi_errors = 0`
  - `spi_drop_events = 0`
  - `overflow_events = 0`
  - `parity_errors_sniffer = 0`
- corrida larga posterior:
  - `spi_errors = 0`

Documento asociado:

- [evidencia_spi_errors_cero_post_tutor.md](docs/evidencia_spi_errors_cero_post_tutor.md)
- [evidencia_spi_pio_frame_post_tutor.md](docs/evidencia_spi_pio_frame_post_tutor.md)

## 6. Red operativa recomendada

Se comprobo que la 3B+ por Wi-Fi podia introducir inestabilidad indirecta sobre la sniffer, probablemente por una combinacion de:

- jitter del host
- patron de polling menos estable
- ruido / consumo / entorno fisico

Configuracion recomendada:

- notebook Ethernet:
  - `192.168.50.1/24`
- Raspberry Pi 3B+ `eth0`:
  - `192.168.50.2/24`
- acceso desde notebook:
  - `http://192.168.50.2:5000`
- bridge SPI/HTTP dentro de la 3B+:
  - `http://127.0.0.1:5100`
- Prometheus scrapea las metricas expuestas por Flask:
  - `192.168.50.2:5000/metrics`

Decision post reunion con tutor:

- no exponer el bridge hacia la notebook en operacion normal
- dejar el bridge ligado a loopback (`127.0.0.1`)
- exponer solo Flask hacia la red Ethernet directa
- Flask consume al bridge local por HTTP sobre loopback
- el transporte byte sigue disponible como fallback estable
- el transporte por tramas completas quedo validado con el target `sniffer_arinc429_logic_pio_frame`
- el modo recomendado para responder la objecion de "byte-a-byte" es `ARINC_SPI_TRANSFER_MODE=pio-frame`
- la causa del problema en `frame` por hardware quedo aislada al SPI slave hardware de la Pico; PIO permite controlar `CS`, `SCK`, `MOSI` y `MISO`
- el bridge separa errores de arranque (`spi_startup_errors`) de errores operativos (`spi_errors`)
- `spi_errors` empieza a contar recien despues de la primera comunicacion SPI valida con el sniffer

## 7. Resultados de osciloscopio

### FWD

- canales individuales:
  - `0 a 3.3 V`
- `Pos Width`:
  - `~5 us`
- retorno a cero confirmado
- en `MATH = CH1 - CH2`:
  - `~+3.3 V`
  - `0 V`
  - `~-3.3 V`

### REV

- mismo comportamiento bipolar logico
- actividad en rafagas, consistente con ACK emitido por `ARINC-RX`
- `ARINC-SNIFFER` solo observa `REV`; nunca lo transmite

### Archivos relacionados

- [instructivo_osciloscopio_arinc429_logic.md](docs/instructivo_osciloscopio_arinc429_logic.md)
- [osciloscopio_descripcion_imagenes.tex](docs/tex/osciloscopio_descripcion_imagenes.tex)
- carpeta [imagenes](imagenes/)

## 8. Limites actuales conocidos

- snapshot del sniffer:
  - `16` slots maximos
- filtro SPI:
  - `10` entradas maximas

La paridad estricta y la nueva clasificacion de resync estan implementadas y
validadas experimentalmente. En la sesion
`20260606_015741_prueba_paridad_estricta_5m` se observaron `610905` palabras
recibidas, `6109` rechazos de paridad, balance de clasificacion igual a cero y
`spi_errors=0`.

Si en el futuro se quiere trabajar con mas variables simultaneas:

- habra que subir `LATEST_SLOT_CAPACITY`
- y posiblemente `SNIFFER_SPI_FILTER_CAPACITY`

## 9. DRDY

`DRDY` no esta conectado en la arquitectura validada actual.

Esto no invalida resultados, porque el bridge actual funciona por polling. En una evolucion futura puede ser util para reducir polling inutil y hacer el host mas orientado a eventos.

## 10. Cierre de fase y proximo paso real

Baseline final:

1. corrida final sin inyeccion:
   - sesion: `20260606_022347_corrida_final_stream_8mhz_8h`
   - duracion: `8.0004 h`
   - `received_words = 58273540`
   - `accepted_words = 17482062`
   - `filtered_words = 40791478`
   - `parity_errors_sniffer = 0`
   - `spi_errors = 0`
   - `overflow_events = 0`
   - `spi_drop_events = 0`
   - `fwd_operational_resync_events = 0`
   - balance de contadores: `0`
   - resultado: `PASS`
   - evidencia:
     [evidencia_corrida_final_stream_8mhz_8h.md](docs/evidencia_corrida_final_stream_8mhz_8h.md)
2. `HOST_3b+/stats_recorder.py` queda disponible para futuras evidencias:
   - `samples.csv`
   - `events.csv`
   - `summary.json`
   - `report.pdf`
3. modo PIO-frame validado:
   - mantener `sniffer_arinc429_logic_pio_frame` como candidato recomendado
   - conservar `byte` como fallback estable
   - analizar los `fwd_resync_events` para distinguir timeout normal de posible ajuste fino
4. modo laboratorio batch/ACK separado del modo stream aleatorio:
   - estado: validado en corrida de aproximadamente `10 horas`
   - targets:
     - TX: `arinc_tx_arinc429_logic_stream`
     - RX: `arinc_rx_arinc429_logic_stream`
   - evita dependencia de lotes fijos de `1000` palabras
   - permite rafagas de cantidad variable
   - en stream, `ARINC-RX` no transmite ACK por `REV`
   - `ARINC-SNIFFER` sigue siendo pasivo: escucha `FWD/REV`, pero nunca transmite sobre ARINC
   - resultado:
     - `accepted_words = 22,756,775`
     - `ack_events = 0`
     - `spi_errors = 0`
     - `spi_startup_errors = 6`
     - `spi_drop_events = 0`
     - `overflow_events = 0`
     - `parity_errors_sniffer = 0`
     - `fwd_resync_events = 3`
   - evidencia: [evidencia_stream_aleatorio_post_tutor.md](docs/evidencia_stream_aleatorio_post_tutor.md)
5. frecuencia SPI caracterizada:
   - barrido desde frecuencias conservadoras hasta la prueba solicitada de `50 MHz`
   - seleccionar la frecuencia recomendada por estabilidad real, no por maximo teorico
   - runner listo: `HOST_3b+/spi_frequency_sweep.py`
   - procedimiento: [procedimiento_barrido_frecuencia_spi.md](docs/procedimiento_barrido_frecuencia_spi.md)
   - el bridge debe estar detenido durante el barrido directo
   - primer resultado:
     - `100 kHz` a `8 MHz`: PASS, `24/24` y `GET_STATS` valido
     - `16 MHz`: FAIL, `0/24`
     - techo continuo corto: `8 MHz`
     - `50 MHz` aislado: FAIL, `0/24`, respuesta en cero
     - la prueba solicitada hasta `50 MHz` fue realizada
     - refinamiento:
       - `8 MHz`: PASS
       - `9 MHz`: PASS
       - `10 MHz`: PARTIAL, `13/24`
     - frecuencia candidata con margen: `8 MHz`
     - corrida operativa a `8 MHz`, primeros `15 minutos`:
       - `accepted_words = 538,044`
       - `spi_errors = 0`
       - `spi_drop_events = 0`
       - `overflow_events = 0`
       - `parity_errors_sniffer = 0`
     - corrida prolongada a `8 MHz`, aproximadamente `9 horas`:
       - `accepted_words = 18,290,772`
       - `filtered_words = 42,678,468`
       - `spi_errors = 0`
       - `spi_startup_errors = 1`
       - `spi_drop_events = 0`
       - `overflow_events = 0`
       - `parity_errors_sniffer = 0`
       - perfil operativo recomendado: `8 MHz`
   - evidencia: [evidencia_barrido_frecuencia_spi.md](docs/evidencia_barrido_frecuencia_spi.md)

El proximo desarrollo tecnico es avanzar hacia producto sniffer ARINC real:

1. definir requisitos electricos y de conexion;
2. seleccionar receptor ARINC 429 dedicado;
3. disenar proteccion y entrada de alta impedancia;
4. construir un adaptador de laboratorio;
5. validar con una fuente ARINC real;
6. migrar a PCB.


## 11. Ramas que no deben tocarse

No modificar:

- `MIL-M`
- `MIL-E`
- `FLASK-MIL`
- `version_estable`

Son ramas ajenas al trabajo ARINC actual.
