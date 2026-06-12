# Evidencia post tutor - barrido de frecuencia SPI

Fecha real de incorporacion: 2026-06-06.

El archivo generado por la Raspberry Pi registra `2026-06-01 02:25:27`. La diferencia indica que el reloj de la 3B+ estaba atrasado cinco dias al ejecutar la prueba; no afecta los resultados electricos ni logicos, pero debe corregirse antes de generar evidencia definitiva.

Reporte fuente:

- `HOST_3b+/spi_frequency_sweep_20260601_022527.json`

## Configuracion

```text
transfer_mode       = pio-frame
manual_cs           = true
cs_gpio             = 8
cs_setup_us         = 100
cs_hold_us          = 100
attempts            = 24
warmup_attempts     = 3
retries             = 1
delay_ms            = 2
```

El uso de `retries = 1` evita ocultar errores transitorios.

## Resultados

```text
100 kHz  = PASS | warmup 3/3 | PING 24/24 | GET_STATS OK
200 kHz  = PASS | warmup 3/3 | PING 24/24 | GET_STATS OK
400 kHz  = PASS | warmup 3/3 | PING 24/24 | GET_STATS OK
800 kHz  = PASS | warmup 3/3 | PING 24/24 | GET_STATS OK
1 MHz    = PASS | warmup 3/3 | PING 24/24 | GET_STATS OK
2 MHz    = PASS | warmup 3/3 | PING 24/24 | GET_STATS OK
4 MHz    = PASS | warmup 3/3 | PING 24/24 | GET_STATS OK
8 MHz    = PASS | warmup 3/3 | PING 24/24 | GET_STATS OK
16 MHz   = FAIL | warmup 0/3 | PING 0/24  | GET_STATS no ejecutado
```

Resultado automatico:

```text
highest_pass_hz             = 8,000,000
contiguous_pass_ceiling_hz  = 8,000,000
stopped_after_hz            = 16,000,000
```

Durante todos los puntos validos:

- `parity_errors = 0`
- `overflow_events = 0`
- `fwd_resync_events` permanecio en `3`
- `rev_resync_events = 0`
- las palabras ARINC siguieron incrementandose

## Analisis del fallo de 16 MHz

Las respuestas invalidas comenzaron repetidamente con:

```text
D2 14 ...
```

La cabecera SPI correcta es:

```text
A4 29 ...
```

`D214` coincide con `A429` desplazado un bit hacia la derecha e insertando un bit alto. Esto sugiere una perdida o muestreo tardio de un flanco, compatible con alcanzar el margen temporal del programa PIO actual. No presenta el aspecto de errores aleatorios por carga de Flask, HTTP o Linux.

El programa PIO usa, como minimo, varias instrucciones por bit:

```text
out/in
wait SCK alto
wait SCK bajo
jmp
```

Con el state machine a `clkdiv = 1`, el margen disminuye a medida que aumenta `SCK`. Ademas intervienen la sincronizacion de las entradas y los tiempos de propagacion del cableado.

## Prueba aislada de 50 MHz

Luego de reiniciar la sniffer se ejecuto el punto solicitado de `50 MHz` de forma aislada:

```text
50 MHz = FAIL | warmup 0/3 | PING 0/24 | GET_STATS no ejecutado
```

Las `24` respuestas medidas fueron:

```text
magic SPI invalido | cabecera=00 00 00 00 00 00 00 00
```

A diferencia de `16 MHz`, donde todavia se reconocio una cabecera desplazada un bit, a `50 MHz` no se obtuvo ningun byte util. La implementacion PIO actual no puede seguir los flancos y preparar la salida MISO de forma valida a esa frecuencia.

El reporte fuente es:

- `HOST_3b+/spi_frequency_sweep_20260601_022923.json`

## Conclusion

- `8 MHz` queda validado como techo continuo del barrido corto.
- `16 MHz` no funciona con la implementacion PIO actual y esta configuracion fisica.
- `50 MHz` fue probado de forma aislada y no funciona: `0/24`.
- La corrida real posterior de ocho horas confirmo `8 MHz` como frecuencia
  operativa recomendada.
- La solicitud del tutor de probar hasta `50 MHz` queda cumplida experimentalmente.
- Alcanzar `50 MHz` requeriria redisenar el transporte PIO o usar un periferico/esclavo SPI capaz de operar a esa frecuencia; no se resuelve ajustando Flask, HTTP ni la IP del bridge.

## Refinamiento de la frontera

Despues de reiniciar la sniffer se ensayo:

```text
8 MHz  = PASS    | warmup 3/3 | PING 24/24 | GET_STATS OK
9 MHz  = PASS    | warmup 3/3 | PING 24/24 | GET_STATS OK
10 MHz = PARTIAL | warmup 0/3 | PING 13/24 | GET_STATS OK
```

Resultado:

```text
highest_pass_hz            = 9,000,000
contiguous_pass_ceiling_hz = 9,000,000
stopped_after_hz           = 10,000,000
```

Las respuestas invalidas de `10 MHz` mostraron varias cabeceras desplazadas o corruptas, incluidas `D214` y `D21C`. Que `GET_STATS` haya funcionado una vez no compensa los `11` PING fallidos: `10 MHz` no es una frecuencia aceptable.

Los contadores ARINC permanecieron en cero durante esta prueba corta, por lo que no habia trafico FWD util entrando a la sniffer en ese momento. Esto no invalida el ensayo directo de SPI, pero la prueba del bridge debe realizarse con TX y RX encendidos.

## Seleccion de frecuencia

Para una corrida real del bridge se selecciono inicialmente `8 MHz`:

- `9 MHz` paso el barrido corto, pero queda demasiado cerca del primer punto inestable.
- `8 MHz` ofrece mayor margen temporal.
- sigue siendo veinte veces la frecuencia PIO-frame previamente validada de `400 kHz`.
- SPI transporta paquetes ya decodificados; aumentar el clock no cambia el enlace ARINC de `100 kbps`.

Los criterios para promoverla a perfil operativo fueron:

```text
spi_errors            = 0
spi_drop_events       = 0
overflow_events       = 0
parity_errors_sniffer = 0
```

## Corrida operativa corta a 8 MHz

Se levanto el bridge con `ARINC_SPI_HZ=8000000` y trafico ARINC stream activo. Despues de aproximadamente `15 minutos`:

```text
accepted_words          = 538,044
received_words          = 538,044
filtered_words          = 1,255,432
ack_events              = 0
spi_errors              = 0
spi_startup_errors      = 1
spi_drop_events         = 0
overflow_events         = 0
parity_errors_sniffer   = 0
parity_ok               = 538,044
fwd_resync_events       = 3
rev_resync_events       = 0
slot_count              = 3
slot_evictions          = 0
```

Distribucion:

```text
TEMPERATURA = 179,328
VELOCIDAD   = 179,333
ALTITUD     = 179,337
```

Las palabras aceptadas representan practicamente el `30%` del trafico observado, coherente con el patron de tres labels utiles cada diez palabras. `ack_events = 0` y `slot_count = 3` son correctos para el modo stream sin emision por `REV`.

La corrida corta valida inicialmente `8 MHz` con trafico real. El unico error registrado fue `spi_startup_errors = 1`, anterior al armado del enlace; `spi_errors` permanecio en cero.

## Corrida operativa prolongada a 8 MHz

Sin reiniciar el bridge ni los contadores, la corrida se extendio aproximadamente `9 horas`:

```text
accepted_words          = 18,290,772
received_words          = 18,290,772
filtered_words          = 42,678,468
total_observed_words    = 60,969,240
ack_events              = 0
spi_errors              = 0
spi_startup_errors      = 1
spi_drop_events         = 0
overflow_events         = 0
parity_errors_sniffer   = 0
parity_ok               = 18,290,772
fwd_resync_events       = 3
rev_resync_events       = 0
slot_count              = 3
slot_evictions          = 0
```

Distribucion:

```text
TEMPERATURA = 6,096,913
VELOCIDAD   = 6,096,916
ALTITUD     = 6,096,920
```

La aceptacion fue exactamente el `30%` del trafico observado. La diferencia de `23` palabras entre la suma instantanea de `by_label` y `accepted_words` corresponde al desfase normal entre el snapshot y los contadores mientras el sistema sigue corriendo.

Los contadores de resincronizacion FWD y de error de arranque no aumentaron durante el regimen. No hubo errores SPI operativos en aproximadamente `9 horas`.

## Estado final

`8 MHz` queda validado como perfil PIO-frame operativo recomendado para la arquitectura actual:

- veinte veces la frecuencia PIO-frame inicial de `400 kHz`
- margen respecto del primer punto inestable de `10 MHz`
- `18.29 M` palabras aceptadas sin errores operativos

Esta seleccion fue ratificada posteriormente por la corrida final registrada:

```text
duracion                     = 8.0004 h
received_words               = 58,273,540
spi_errors                   = 0
parity_errors_sniffer        = 0
overflow_events              = 0
spi_drop_events              = 0
fwd_operational_resync       = 0
counter_balance_words        = 0
```

Evidencia:

- [evidencia_corrida_final_stream_8mhz_8h.md](evidencia_corrida_final_stream_8mhz_8h.md)

`400 kHz` se conserva como fallback PIO-frame conservador y el modo `byte` a `800 kHz` como fallback historico.
