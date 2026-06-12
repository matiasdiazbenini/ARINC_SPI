# Evidencia post tutor - SPI errors en regimen estable

Fecha de referencia: 2026-06-05.

Este documento registra la validacion posterior a las observaciones del tutor sobre los errores SPI del enlace entre `ARINC-SNIFFER` y la Raspberry Pi 3B+.

## Contexto

Antes de esta correccion, el contador `spi_errors` mezclaba dos situaciones distintas:

- errores de arranque, cuando el bridge ya estaba corriendo pero la Pico sniffer todavia estaba apagada o inicializando
- errores de regimen, cuando el enlace SPI ya habia tenido al menos una comunicacion valida

Esto hacia que una corrida pudiera mostrar `spi_errors > 0` aunque esos errores no pertenecieran a la operacion estable del sistema.

## Correccion aplicada

El bridge ahora separa:

- `spi_startup_errors`:
  - errores ocurridos antes de la primera comunicacion SPI valida con el sniffer
  - explican el arranque desfasado entre la 3B+ y las Pico
- `spi_errors`:
  - errores ocurridos despues de que el enlace SPI ya quedo armado
  - es el contador relevante para regimen operativo
- `spi_error_armed`:
  - indica si el bridge ya tuvo una comunicacion valida y, por lo tanto, si el contador operativo esta armado

La apertura de `/dev/spidev0.0` ya no arma el contador operativo. El contador se arma recien luego de una respuesta SPI valida del sniffer.

## Resultado observado

Corrida de referencia de aproximadamente 2 horas, con el sistema ya operativo:

```text
accepted_words          = 3,495,121
received_words          = 3,495,121
ack_events              = 11,611
filtered_words          = 8,128,185
spi_errors              = 0
spi_startup_errors      = 19
spi_error_armed         = true
spi_drop_events         = 0
overflow_events         = 0
parity_errors_sniffer   = 0
fwd_resync_events       = 3
rev_resync_events       = 0
slot_count              = 4
slot_evictions          = 0
```

Distribucion por label:

```text
0xA5 TEMPERATURA = 1,161,159
0xB1 VELOCIDAD   = 1,161,162
0xC2 ALTITUD     = 1,161,164
0xAC ACK_BATCH   =    11,611
```

La corrida larga posterior confirmo:

```text
spi_errors = 0
```

## Interpretacion tecnica

Los `spi_startup_errors` no representan perdida de datos ARINC ni degradacion del enlace en regimen. Representan intentos del bridge de consultar al sniffer antes de que la Pico estuviera lista.

Una vez que el sniffer respondio correctamente y `spi_error_armed` paso a `true`, el contador operativo `spi_errors` permanecio en cero durante la prueba.

Esto responde directamente a la observacion del tutor:

- los errores de arranque quedan catalogados y visibles
- la medicion de regimen queda limpia
- la corrida estable muestra `spi_errors = 0`

## Estado actual recomendado

Perfil operativo validado originalmente en modo byte:

```text
ARINC_SPI_HZ                 = 800000
ARINC_SPI_TRANSFER_MODE      = byte
ARINC_SPI_BYTE_DELAY_US      = 25
ARINC_SPI_POLL_SEC           = 0.006
ARINC_SPI_STATS_SEC          = 0.06
ARINC_SPI_RESPONSE_DELAY_SEC = 0.00005
ARINC_SPI_RECOVERY_GAP_SEC   = 0.012
```

Este modo queda como fallback estable validado.

## Continuacion: tramas completas por PIO

La objecion pendiente del tutor sobre "SPI byte a byte" se resolvio despues con un nuevo transporte del sniffer por PIO:

- target:
  - `sniffer_arinc429_logic_pio_frame`
- build tag:
  - `SPI-PIOFRAME-ARINC429`
- modo bridge:
  - `ARINC_SPI_TRANSFER_MODE=pio-frame`

La corrida PIO-frame de 6 a 7 horas valido:

```text
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

## Proximo paso tecnico

El siguiente punto tecnico no es el transporte SPI, sino revisar los `fwd_resync_events` del receptor ARINC/FWD del sniffer para distinguir resincronizaciones normales por timeout de posibles ajustes finos de la logica de recepcion.
