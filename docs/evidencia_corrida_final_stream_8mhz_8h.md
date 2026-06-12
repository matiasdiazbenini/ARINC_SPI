# Evidencia de corrida final stream a 8 MHz

## Sesion

```text
20260606_022347_corrida_final_stream_8mhz_8h
```

Configuracion:

- TX: `arinc_tx_arinc429_logic_stream`
- RX: `arinc_rx_arinc429_logic_stream`
- SNIFFER: `sniffer_arinc429_logic_pio_frame`
- transporte SPI: PIO-frame
- frecuencia SPI solicitada: 8 MHz
- CS manual: activo
- duracion objetivo: 8 horas
- intervalo de registro: 5 segundos
- inyeccion deliberada de paridad: desactivada

## Resultado

```text
duration_sec                  = 28801.471
sample_count                  = 5760
received_words                = 58273540
accepted_words                = 17482062
filtered_words                = 40791478
parity_errors_sniffer         = 0
spi_errors                    = 0
spi_startup_errors            = 0
overflow_events               = 0
spi_drop_events               = 0
fwd_operational_resync_events = 0
rev_resync_events             = 0
counter_balance_words         = 0
verdict                       = PASS
```

La clasificacion conserva exactamente todas las palabras:

```text
17482062 + 40791478 + 0 + 0 = 58273540
```

Distribucion:

```text
accepted_words / received_words = 30.0 %
filtered_words / received_words = 70.0 %
```

La proporcion coincide exactamente con el perfil TX de diez palabras, donde
tres combinaciones label/SDI son admitidas por la whitelist y siete son ruido
deliberado.

## Continuidad

- `connected=1` en las 5760 muestras
- no se registro ningun `last_error`
- intervalo nominal: 5 s
- intervalo minimo observado: 4.977 s
- mediana: 5.000 s
- intervalo maximo observado: 5.026 s
- `events.csv` contiene solamente:
  - armado de la sesion
  - inicio de la medicion
  - cierre por duracion cumplida

No hubo eventos de perdida o recuperacion del enlace.

## Rendimiento

```text
tasa media recibida  = 2023.283 words/s
tasa media aceptada  = 606.985 words/s
tasa maxima aceptada = 629.568 words/s
```

Las tres labels validas permanecieron equilibradas. Al cierre, la diferencia
maxima entre sus contadores acumulados fue de diez palabras.

Los estados SSM `NORMAL`, `NCD`, `FAILURE` y `FUNCTIONAL_TEST` observados en
las muestras forman parte del perfil generado por el TX. No representan
errores de transporte ni de paridad.

## Conclusion

La arquitectura stream sin ACK, con SNIFFER pasivo y transporte SPI PIO-frame
a 8 MHz, queda validada durante ocho horas continuas:

- sin errores SPI
- sin errores de paridad
- sin overflow ni drops
- sin resincronizaciones FWD operativas
- sin desconexiones
- con balance exacto de contadores

Esta sesion constituye la evidencia final de estabilidad de la fase logica
actual.
