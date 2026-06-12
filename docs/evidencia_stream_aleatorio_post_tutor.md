# Evidencia post tutor - modo stream aleatorio sin ACK

Fecha de referencia: 2026-06-06.

Este documento registra la validacion prolongada del modo stream de `ARINC-TX` y `ARINC-RX`. El objetivo fue eliminar la dependencia funcional de bloques fijos de `1000` palabras y comprobar que el sistema puede transportar rafagas de longitud variable sin esperar una respuesta por `REV`.

## Configuracion probada

- TX:
  - target `arinc_tx_arinc429_logic_stream`
  - rafagas aleatorias de `1` a `2000` palabras
  - pausa aleatoria de `0` a `25000 us`
  - sin espera de ACK
- RX:
  - target `arinc_rx_arinc429_logic_stream`
  - procesamiento continuo, palabra por palabra
  - sin agrupamiento obligatorio de `1000` palabras
  - sin transmision sobre `REV`
- Sniffer:
  - target `sniffer_arinc429_logic_pio_frame`
  - escucha pasiva de `FWD` y `REV`
  - transporte SPI por tramas completas
- Bridge:
  - `ARINC_SPI_TRANSFER_MODE=pio-frame`
  - `ARINC_SPI_HZ=400000`
  - CS manual por `GPIO8`

## Corrida larga

Duracion aproximada: `10 horas`.

```text
accepted_words          = 22,756,775
received_words          = 22,756,775
filtered_words          = 53,099,137
ack_events              = 0
spi_errors              = 0
spi_startup_errors      = 6
spi_error_armed         = true
spi_drop_events         = 0
overflow_events         = 0
parity_errors_sniffer   = 0
parity_ok               = 22,756,775
fwd_resync_events       = 3
rev_resync_events       = 0
slot_count              = 3
slot_evictions          = 0
port                    = spi0.0+gpio8
source_mode             = spi_bridge
```

Distribucion por label util:

```text
0xA5 TEMPERATURA = 7,585,576
0xB1 VELOCIDAD   = 7,585,580
0xC2 ALTITUD     = 7,585,585
```

## Interpretacion

La prueba valida los siguientes puntos:

- El enlace ya no depende de batches fijos ni de un ACK cada `1000` palabras.
- `ARINC-TX` puede transmitir cantidades variables de palabras y pausas variables.
- `ARINC-RX` procesa el flujo a medida que llega.
- `ack_events = 0` y `slot_count = 3` son resultados esperados: `ARINC-RX` no emite por `REV` y el sniffer solo mantiene las tres labels utiles.
- Las tres labels tienen conteos practicamente iguales, compatibles con el patron periodico de generacion.
- Las palabras aceptadas representan aproximadamente el `30%` del trafico observado, coherente con tres labels utiles cada diez palabras transmitidas.
- Todas las palabras aceptadas tuvieron paridad correcta.
- No hubo errores SPI operativos, drops ni overflow durante la corrida.

Los `6` errores de arranque ocurrieron antes de armar el enlace SPI y quedaron correctamente clasificados como `spi_startup_errors`; no contaminaron `spi_errors`.

Los `3` eventos de resincronizacion FWD son bajos frente al volumen total y no estuvieron asociados a errores de paridad, overflow ni perdida SPI. Pueden corresponder a la secuencia de arranque o a una pausa inicial del transmisor.

La diferencia pequena entre la suma instantanea de `by_label`, `accepted_words` y `snapshot_revision` puede aparecer porque los contadores y el snapshot son consultados en momentos ligeramente distintos mientras el sistema continua recibiendo palabras.

## Conclusion

El modo stream aleatorio queda validado para la etapa logica de laboratorio:

- flujo variable sin dependencia de bloques de `1000`
- `REV` inactivo
- sniffer completamente pasivo sobre ARINC
- transporte PIO-frame estable
- `spi_errors = 0` durante aproximadamente `10 horas`

El siguiente ensayo planificado es el barrido de frecuencia SPI, manteniendo esta configuracion stream como fuente de trafico.
