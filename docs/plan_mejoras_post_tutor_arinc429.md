# Plan de mejoras post reunion con tutor - ARINC 429

Fecha de referencia: 2026-06-04.

Este documento resume las observaciones surgidas en la reunion con el tutor y las acciones concretas para fortalecer la arquitectura ARINC. No incluye trabajo MIL-STD-1553.

## Diagnostico general

La fase actual valida correctamente la recreacion logica/temporal ARINC 429:

- transmision a 100 kbps
- codificacion bipolar logica con retorno a cero
- palabra ARINC de 32 bits
- label, SDI, data, SSM y paridad
- sniffer pasivo de laboratorio
- filtro por label + SDI
- SPI entre sniffer y Raspberry Pi 3B+
- bridge HTTP
- dashboard Flask
- Prometheus/Grafana
- validacion por osciloscopio

Las observaciones del tutor apuntan principalmente a endurecer la arquitectura para una siguiente etapa:

- reducir superficie de red expuesta
- eliminar o explicar los `spi_errors`
- reemplazar SPI byte-a-byte por transacciones de trama completa
- separar claramente modo laboratorio de modo sniffer ARINC real
- documentar el funcionamiento interno de cada placa
- preparar el camino hacia interfaz electrica ARINC 429 real

## Punto 1 - Red local-only para el bridge

### Problema

El bridge SPI/HTTP no necesita quedar expuesto a la notebook para la operacion normal del dashboard. Si tanto Flask como el bridge publican servicios hacia la red, se aumenta innecesariamente la superficie visible y tambien la cantidad de clientes potenciales golpeando al bridge.

### Decision

Arquitectura recomendada:

```text
Notebook -> http://192.168.50.2:5000 -> Flask
Flask    -> http://127.0.0.1:5100  -> Bridge SPI/HTTP
Bridge   -> SPI                    -> ARINC-SNIFFER
```

El bridge debe escuchar por defecto en `127.0.0.1`. Flask queda publicado en `0.0.0.0:5000` para que la notebook acceda al dashboard.

Prometheus, corriendo en la notebook, debe scrapear Flask:

```text
http://192.168.50.2:5000/metrics
```

En modo bridge, Flask devuelve las metricas obtenidas desde el bridge local.

### Estado

Implementado como primer ajuste:

- `HOST_3b+/spi_sniffer_bridge.py` ahora permite configurar `ARINC_BRIDGE_HOST`.
- valor recomendado/default: `127.0.0.1`.
- systemd del bridge fija `ARINC_BRIDGE_HOST=127.0.0.1`.
- Prometheus recomendado scrapea `192.168.50.2:5000`, no `192.168.50.2:5100`.

## Punto 2 - SPI por tramas completas

### Problema

La implementacion previa usaba SPI hardware con transferencias byte-a-byte desde Python. Funcionaba, pero era mas sensible a jitter, framing, CS y planificacion de Linux.

### Accion propuesta

Cambiar el transporte host para usar transacciones completas:

```text
xfer2([paquete request completo])
delay corto
xfer2([ventana de lectura completa])
```

Objetivo:

- mantener CS bajo durante cada trama completa
- reducir desincronizaciones
- bajar `spi_errors` a cero en corrida controlada

### Estado

Implementado en el bridge:

- `ARINC_SPI_TRANSFER_MODE=byte`:
  - fallback estable validado
  - conserva el comportamiento byte-a-byte anterior con pacing
- `ARINC_SPI_TRANSFER_MODE=frame`:
  - modo experimental
  - sincroniza con `RESET`
  - envia `request[32]` como una transaccion completa
  - lee la respuesta con una ventana completa de bytes `IDLE`
  - no quedo como modo recomendado con el SPI slave hardware de la Pico
- `ARINC_SPI_TRANSFER_MODE=pio-frame`:
  - modo validado para tramas completas
  - requiere cargar `sniffer_arinc429_logic_pio_frame` en la Pico sniffer
  - usa `CS` como delimitador de paquete completo de 32 bytes
  - probado con CS manual en la 3B+

El paquete logico SPI sigue teniendo 32 bytes y CRC16. La prueba de `frame` completo contra el SPI slave hardware demostro que el cuello de botella estaba en ese bloque hardware de la Pico para esta integracion. La solucion validada fue implementar el transporte del sniffer por PIO, controlando directamente `CS`, `SCK`, `MOSI` y `MISO`.

Resultado PIO-frame:

```text
diagnostico pio-frame = 12/12
corrida 6-7 h:
accepted_words     = 11,558,875
ack_events         = 38,401
spi_errors         = 0
spi_startup_errors = 0
spi_drop_events    = 0
overflow_events    = 0
parity_errors      = 0
```

Documento asociado:

- [evidencia_spi_pio_frame_post_tutor.md](evidencia_spi_pio_frame_post_tutor.md)

## Punto 3 - Cero `spi_errors` o clasificacion precisa

### Problema

El tutor pidio cero errores. Los `spi_errors` actuales no son errores ARINC ni errores de paridad de las palabras; son errores del transporte SPI/HTTP bridge-sniffer.

### Accion aplicada

Se separaron los contadores:

- `spi_startup_errors`
  - errores antes de la primera comunicacion SPI valida con el sniffer
- `spi_errors`
  - errores operativos despues de que el enlace ya quedo armado
- `spi_error_armed`
  - indica que ya hubo comunicacion valida y que el contador operativo esta activo

Resultado validado:

```text
spi_errors = 0 en regimen estable
```

Corrida de referencia:

- `accepted_words = 3,495,121`
- `ack_events = 11,611`
- `spi_errors = 0`
- `spi_startup_errors = 19`
- `spi_drop_events = 0`
- `overflow_events = 0`
- `parity_errors_sniffer = 0`

Documento asociado:

- [evidencia_spi_errors_cero_post_tutor.md](evidencia_spi_errors_cero_post_tutor.md)

## Punto 4 - REV y fidelidad ARINC 429

ARINC 429 real es simplex. Un canal fisico tiene un transmisor y uno o varios receptores. Si se necesita respuesta, se usa otro canal fisico ARINC en sentido inverso.

Por lo tanto:

- `FWD` representa un canal simplex.
- `REV` representa otro canal simplex usado en laboratorio para ACK desde `ARINC-RX` hacia `ARINC-TX`.
- el ACK no pretende ser parte del bus ARINC 429 real.
- `ARINC-SNIFFER` nunca debe transmitir sobre lineas ARINC; solamente observa `FWD` y, si existe, `REV`.
- la unica salida activa del sniffer es el enlace host `SPI MISO` hacia la 3B+, que no forma parte del bus ARINC.

Accion propuesta:

- documentar dos modos:
  - modo laboratorio: `ARINC-TX` transmite `FWD`, `ARINC-RX` transmite ACK por `REV`, `ARINC-SNIFFER` escucha ambos
  - modo sniffer real: `ARINC-SNIFFER` solo escucha lineas ARINC reales y no inyecta nada

Estado post ajuste:

- `ARINC-SNIFFER` quedo documentado como receptor pasivo de `FWD` y `REV`; no transmite nunca sobre lineas ARINC.
- `ARINC-TX` conserva el modo laboratorio `arinc_tx_arinc429_logic`.
- `ARINC-RX` conserva el modo laboratorio `arinc_rx_arinc429_logic`.
- se agregaron targets de prueba stream:
  - `arinc_tx_arinc429_logic_stream`
  - `arinc_rx_arinc429_logic_stream`
- en modo stream, `ARINC-TX` transmite rafagas de longitud variable y no espera ACK.
- en modo stream, `ARINC-RX` procesa palabras a medida que llegan, no espera lotes de `1000`, y deja `REV` sin emision.

Prueba realizada:

- cargar `arinc_tx_arinc429_logic_stream.uf2` en TX.
- cargar `arinc_rx_arinc429_logic_stream.uf2` en RX.
- mantener `sniffer_arinc429_logic_pio_frame.uf2` en la sniffer.
- levantar el bridge con `ARINC_SPI_TRANSFER_MODE=pio-frame`.
- en el dashboard deben crecer `TEMPERATURA`, `VELOCIDAD` y `ALTITUD`.
- `ACK_BATCH` debe quedar ausente o sin crecer, porque en este modo no hay ACK por `REV`.
- mantener como criterios de aceptacion:
  - `spi_errors = 0`
  - `spi_startup_errors = 0` si todo arranca ya alimentado
  - `overflow_events = 0`
  - `parity_errors_sniffer = 0`
  - `spi_drop_events = 0`

Resultado validado durante aproximadamente `10 horas`:

```text
accepted_words        = 22,756,775
filtered_words        = 53,099,137
ack_events            = 0
spi_errors            = 0
spi_startup_errors    = 6
spi_drop_events       = 0
overflow_events       = 0
parity_errors_sniffer = 0
fwd_resync_events     = 3
rev_resync_events     = 0
slot_count            = 3
```

El modo stream queda validado. Los errores anteriores al enlace valido quedaron aislados en `spi_startup_errors`; no hubo errores SPI operativos. Documento asociado:

- [evidencia_stream_aleatorio_post_tutor.md](evidencia_stream_aleatorio_post_tutor.md)

## Punto 5 - Osciloscopio

La traza `MATH = CH1 - CH2` representa la reconstruccion diferencial logica:

- positivo: una polaridad
- negativo: polaridad contraria
- cero: retorno a cero

Si aparecen pulsos positivos y negativos superpuestos en el mismo lugar de pantalla, la explicacion mas probable es persistencia/overlay del osciloscopio. En una captura single-shot, la resta no puede ser positiva y negativa a la vez.

Accion propuesta:

- repetir capturas con persistencia apagada
- tomar single-shot de FWD y REV
- documentar CH1, CH2 y MATH

## Punto 6 - Diagramas internos por placa

Preparar un diagrama por cada placa:

1. `ARINC-TX`
   - generador de datos
   - armado de palabra ARINC
   - FIFO TX de PIO
   - maquina de estado PIO TX
   - receptor de ACK por REV

2. `ARINC-RX`
   - maquina de estado PIO RX
   - FIFO RX de PIO
   - decodificacion en C
   - validacion de label/paridad
   - generacion de ACK por REV

3. `ARINC-SNIFFER`
   - PIO RX FWD
   - PIO RX REV
   - cola de captura
   - decodificacion
   - filtro label + SDI
   - snapshot
   - event queue
   - SPI slave

4. Raspberry Pi 3B+
   - SPI master
   - bridge HTTP local
   - memoria/snapshot del bridge
   - Flask
   - Prometheus/Grafana

## Punto 7 - Interfaz electrica ARINC real

La fase actual no valida niveles electricos ARINC reales. El siguiente salto de producto es:

```text
ARINC A/B
  -> proteccion / impedancia / alta impedancia
  -> receptor ARINC 429 dedicado Holt o DEI
  -> logica 3.3 V
  -> Pico sniffer
```

Acciones:

- seleccionar receptor Holt/DEI
- disenar adaptador minimo
- probar con generador ARINC real o fuente representativa
- luego migrar a PCB

## Punto 8 - Barrido de frecuencia SPI

El modo `pio-frame` fue caracterizado desde `100 kHz` hasta `50 MHz`. El
perfil operativo seleccionado es `8 MHz`, validado posteriormente durante
ocho horas sin errores operativos.

El tutor propuso probar hasta `50 MHz`. Esa prueba se puede hacer como barrido de diagnostico, pero no debe asumirse como requisito operativo automatico:

- el enlace ARINC FWD/REV trabaja a `100 kbps`
- SPI no transporta la forma de onda ARINC bit a bit, sino paquetes/snapshots ya decodificados
- `50 MHz` puede ser valido para ciertos controladores SPI en condiciones ideales, pero en esta integracion intervienen PIO, cables, CS manual, timing de Linux y margen electrico
- la frecuencia recomendada debe ser la maxima estable con `12/12`, `spi_errors=0` y corrida larga limpia, no necesariamente la maxima teorica

Plan de prueba:

```text
100 kHz -> 200 kHz -> 400 kHz -> 800 kHz -> 1 MHz -> 2 MHz -> 4 MHz -> 8 MHz -> 16 MHz -> 32 MHz -> 50 MHz
```

Para cada punto:

- diagnostico `pio-frame`
- verificacion de `GET_STATS`
- corrida corta
- si pasa, corrida larga en la frecuencia candidata

Estado:

- runner implementado:
  - `HOST_3b+/spi_frequency_sweep.py`
- procedimiento:
  - [procedimiento_barrido_frecuencia_spi.md](procedimiento_barrido_frecuencia_spi.md)
- el barrido usa `retries=1` para no ocultar errores transitorios
- `PASS` exige PING perfecto y `GET_STATS` valido
- primer barrido fisico ejecutado:
  - `100 kHz` a `8 MHz`: PASS perfecto
  - `16 MHz`: FAIL, `0/24`
  - techo continuo corto: `8 MHz`
- prueba aislada de `50 MHz` ejecutada:
  - FAIL, `0/24`
  - respuestas completamente en cero
  - la solicitud de ensayo hasta `50 MHz` queda cumplida
- frontera refinada:
  - `8 MHz`: PASS
  - `9 MHz`: PASS
  - `10 MHz`: PARTIAL, `13/24`
  - frecuencia candidata con margen para corrida real: `8 MHz`
- corrida operativa corta a `8 MHz`:
  - aproximadamente `15 minutos`
  - `accepted_words = 538,044`
  - `spi_errors = 0`
  - `spi_drop_events = 0`
  - `overflow_events = 0`
  - `parity_errors_sniffer = 0`
  - resultado inicial: PASS
- corrida operativa prolongada a `8 MHz`:
  - aproximadamente `9 horas`
  - `accepted_words = 18,290,772`
  - `filtered_words = 42,678,468`
  - `spi_errors = 0`
  - `spi_startup_errors = 1`
  - `spi_drop_events = 0`
  - `overflow_events = 0`
  - `parity_errors_sniffer = 0`
  - resultado: PASS
  - `8 MHz` pasa a perfil PIO-frame operativo recomendado
- evidencia:
  - [evidencia_barrido_frecuencia_spi.md](evidencia_barrido_frecuencia_spi.md)

## Punto 9 - Recuperacion ante desconexiones y reinicios

### Problema

Las pruebas de falla mostraron dos casos debiles:

- si el sniffer se reinicia o se reconecta con el TX ya transmitiendo, puede
  arrancar en mitad de una palabra ARINC y acumular errores de paridad;
- si el bridge o la 3B+ reinician con el sniffer ya encendido, el transporte
  `pio-frame` puede quedar fuera de fase hasta una resincronizacion manual.

### Accion aplicada

Se agrego recuperacion en tres capas:

1. Sniffer:
   - build `SPI-PIOFRAME-ARINC429-STRICTPARITY-DRDY-AUTORATE-RECOVERY-V5`;
   - si hay racha invalida o timeout, la PIO se reinicia esperando reposo
     electrico estable del canal antes de volver a capturar.
2. Bridge:
   - en `pio-frame`, el reset de transporte es una trama completa de `32 bytes`
     con valor `0xF0`;
   - el bridge la envia al abrir SPI y ante respuestas invalidas;
   - se expone `spi_transport_resets` en `/stats` y
     `arinc_spi_transport_resets_total` en `/metrics`.
3. Raspberry Pi 3B+:
   - se agrego `arinc_supervisor.py`;
   - se agrego `systemd/arinc-supervisor.service`;
   - el supervisor consulta `/stats` cada `5 s`;
   - clasifica estados `BOOTING`, `WAITING_SNIFFER`, `SPI_SYNCING`, `RUNNING`,
     `ARINC_STALLED`, `CABLE_FAULT`, `BRIDGE_FAULT`, `DASHBOARD_FAULT` y
     `RECOVERING`;
   - primero ejecuta `POST /control/recover_spi` si el bridge vive pero SPI esta
     desfasado;
   - reinicia bridge/Flask si hay fallas repetidas de servicio, HTTP o
     protocolo;
   - registra `CABLE_FAULT` sin entrar en reinicios infinitos.

### Alcance

La recuperacion cubre fallas de proceso, arranque fuera de fase y
desalineacion de protocolo. No puede corregir por software un cable fisicamente
desconectado, masa comun perdida o una Pico sin alimentacion.

Documento asociado:

- [procedimiento_recuperacion_fallas_arinc429.md](procedimiento_recuperacion_fallas_arinc429.md)
- [evidencia_recuperacion_fallas_v5.md](evidencia_recuperacion_fallas_v5.md)

## Punto 10 - Paridad estricta y evidencia exportable

Estado de implementacion:

- `ARINC-RX` ya no incrementa `accepted_words` cuando la paridad es incorrecta.
- `ARINC-SNIFFER` cuenta la paridad al extraer la palabra del FIFO PIO.
- una palabra con mala paridad:
  - incrementa `received_words`
  - incrementa `parity_errors`
  - no incrementa `accepted_words`
  - no actualiza el snapshot
- los resync FWD se separan en:
  - `fwd_startup_resync_events`
  - `fwd_operational_resync_events`
- los nuevos contadores usan campos antes reservados de `GET_LATEST_META`, sin
  cambiar el paquete SPI de 32 bytes.
- se agrego `HOST_3b+/stats_recorder.py`.
- cada prueba puede exportar CSV, JSON y PDF en
  `~/PAMPA/ARINC_RESULTS`.

Validacion experimental completada:

- sesion: `20260606_015741_prueba_paridad_estricta_5m`
- duracion: `300.061 s`
- `received_words = 610905`
- `accepted_words = 177163`
- `filtered_words = 427633`
- `parity_errors_sniffer = 6109`
- balance de clasificacion: `0`
- tasa de paridad rechazada: `0.99999 %`
- `spi_errors = 0`
- `overflow_events = 0`
- `spi_drop_events = 0`
- `fwd_operational_resync_events = 0`
- resultado del registrador: `PASS`
- evidencia:
  [evidencia_prueba_paridad_estricta_20260605.md](evidencia_prueba_paridad_estricta_20260605.md)

Corrida larga final completada:

- sesion: `20260606_022347_corrida_final_stream_8mhz_8h`
- duracion: `28801.471 s`
- muestras: `5760`
- `received_words = 58273540`
- `accepted_words = 17482062`
- `filtered_words = 40791478`
- `parity_errors_sniffer = 0`
- `spi_errors = 0`
- `overflow_events = 0`
- `spi_drop_events = 0`
- `fwd_operational_resync_events = 0`
- balance de clasificacion: `0`
- resultado: `PASS`
- evidencia:
  [evidencia_corrida_final_stream_8mhz_8h.md](evidencia_corrida_final_stream_8mhz_8h.md)

## Estado de las observaciones del tutor

Completado:

1. Bridge restringido a loopback y Flask expuesto a la red.
2. Transporte SPI por tramas completas mediante PIO-frame.
3. Separacion entre errores SPI de arranque y operativos.
4. Corridas prolongadas con `spi_errors = 0`.
5. Stream aleatorio sin dependencia de lotes de 1000 palabras.
6. Barrido SPI hasta 50 MHz y seleccion justificada de 8 MHz.
7. Diagramas internos de las cuatro placas/nodos.
8. Separacion documentada entre modo laboratorio y sniffer pasivo real.
9. Modo dios v2 para clasificacion y recuperacion escalonada.
10. Paridad estricta validada mediante inyeccion controlada.
11. Exportacion automatica de CSV, JSON y PDF.

Siguiente fase:

- receptor ARINC 429 electrico real;
- proteccion y alta impedancia de entrada;
- adaptador de laboratorio;
- posterior migracion a PCB.
