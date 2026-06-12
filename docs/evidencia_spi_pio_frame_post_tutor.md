# Evidencia post tutor - SPI por tramas completas con PIO

Fecha de referencia: 2026-06-05.

Este documento registra la validacion del transporte SPI por tramas completas entre `ARINC-SNIFFER` y la Raspberry Pi 3B+ usando un SPI slave implementado por PIO en la Pico.

## Contexto

El tutor observo que el enlace SPI entre el sniffer y la 3B+ no debia depender de transferencias byte-a-byte. La prueba con el SPI slave hardware de la Pico no fue satisfactoria para tramas completas: el sniffer podia reconocer actividad, pero la respuesta quedaba desincronizada o incompleta cuando la 3B+ clockeaba una trama completa.

La causa quedo aislada al bloque SPI slave hardware de la Pico en esta integracion, no al cableado ni a Flask ni al bridge HTTP. Una prueba minima posterior con PIO demostro que la misma conexion fisica puede transportar una trama completa de 32 bytes correctamente.

## Cambio aplicado

Se agrego un transporte SPI por PIO:

- target de prueba minima:
  - `ARINC_SNIFFER_SPI_PIO_FRAME`
- target del sniffer real:
  - `sniffer_arinc429_logic_pio_frame`
- build tag esperado:
  - `SPI-PIOFRAME-ARINC429`
- modo del bridge:
  - `ARINC_SPI_TRANSFER_MODE=pio-frame`
- CS manual en la 3B+:
  - `ARINC_SPI_MANUAL_CS=1`
  - `ARINC_SPI_CS_SETUP_US=100`
  - `ARINC_SPI_CS_HOLD_US=100`

En este modo, el delimitador de trama es `CS`: una transaccion SPI de 32 bytes representa un paquete completo. El host ya no necesita enviar `RESET=0xF0` antes de cada paquete como mecanismo de resincronizacion byte-a-byte.

## Diagnostico inicial

La prueba minima PIO-frame respondio correctamente en los 10 intentos:

```text
RX first8 = A4 29 53 50 49 4D 49 4E
resultado = firma_SPI_MIN_OK
```

Luego, el diagnostico del sniffer real con `--transfer-mode pio-frame` devolvio `12/12` respuestas validas.

## Corrida larga

Corrida de referencia de aproximadamente 6 a 7 horas:

```text
accepted_words          = 11,558,875
received_words          = 11,558,875
ack_events              = 38,401
filtered_words          = 26,881,106
spi_errors              = 0
spi_startup_errors      = 0
spi_error_armed         = true
spi_drop_events         = 0
overflow_events         = 0
parity_errors_sniffer   = 0
parity_ok               = 11,558,875
fwd_resync_events       = 147
rev_resync_events       = 0
slot_count              = 4
slot_evictions          = 0
port                    = spi0.0+gpio8
source_mode             = spi_bridge
```

Distribucion por label:

```text
0xA5 TEMPERATURA = 3,840,138
0xB1 VELOCIDAD   = 3,840,142
0xC2 ALTITUD     = 3,840,147
0xAC ACK_BATCH   =    38,401
```

Ultimos valores observados:

```text
TEMPERATURA = 58.5 C
VELOCIDAD   = 311.0 kt
ALTITUD     = 11980.0 ft
ACK_BATCH   = 38401.0 batch
```

## Interpretacion

La corrida valida dos puntos importantes:

- El transporte entre sniffer y 3B+ ya puede operar por tramas completas, no byte-a-byte.
- En regimen, el enlace mantuvo `spi_errors = 0` y `spi_startup_errors = 0`.

La aparicion de `FUNCTIONAL_TEST` en `by_ssm` corresponde al SSM del ultimo slot `VELOCIDAD`. No representa un error del transporte SPI: las palabras mantienen paridad correcta y no hay errores del sniffer.

`fwd_resync_events = 147` pertenece a la logica de recepcion ARINC/FWD del sniffer, no al transporte SPI. Como no hay errores de paridad, drops ni overflow, queda como punto fino de analisis posterior, no como bloqueo para validar el enlace SPI por PIO.

## Perfil usado

Comando recomendado para diagnostico:

```bash
python3 spi_link_diagnostic.py \
  --transfer-mode pio-frame \
  --hz-list 100000,200000,400000,800000 \
  --delay-ms-list 2,5,10 \
  --byte-delay-us 0 \
  --manual-cs \
  --cs-setup-us 100 \
  --cs-hold-us 100
```

Comando usado para bridge:

```bash
nohup env \
  ARINC_SPI_TRANSFER_MODE=pio-frame \
  ARINC_SPI_MANUAL_CS=1 \
  ARINC_SPI_CS_SETUP_US=100 \
  ARINC_SPI_CS_HOLD_US=100 \
  ARINC_SPI_HZ=400000 \
  ARINC_SPI_BYTE_DELAY_US=0 \
  ARINC_SPI_FRAME_RESPONSE_DELAY_SEC=0.002 \
  python3 spi_sniffer_bridge.py > bridge.log 2>&1 &
```

## Estado recomendado

El modo `byte` queda como fallback estable validado. El modo `pio-frame` pasa a ser el candidato recomendado para responder la observacion del tutor sobre tramas completas, sujeto a una corrida final aun mas larga si se desea cerrar con mayor margen.
