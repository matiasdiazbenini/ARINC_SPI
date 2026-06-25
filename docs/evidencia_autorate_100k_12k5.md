# Evidencia autorate 100 kbps / 12.5 kbps

## Objetivo

Verificar que el sistema puede operar con velocidad ARINC seleccionada al
arrancar y que `ARINC-SNIFFER` detecta autonomamente si el canal `FWD` trabaja
a `100 kbps` o `12.5 kbps`.

## Configuracion

- TX:
  - `arinc_tx_arinc429_logic_stream_autorate`
  - seleccion aleatoria al arranque entre `100 kbps` y `12.5 kbps`
- RX:
  - `arinc_rx_arinc429_logic_stream`
  - recepcion por flancos/nivel activo, sin conocer previamente la velocidad
- SNIFFER:
  - `sniffer_arinc429_logic_pio_frame`
  - build esperada:
    - `SPI-PIOFRAME-ARINC429-STRICTPARITY-DRDY-AUTORATE-RECOVERY-V5`
- Bridge 3B+:
  - `ARINC_SPI_TRANSFER_MODE=pio-frame`
  - `ARINC_SPI_REQUEST_MODE=drdy`
  - `ARINC_SPI_HZ=8000000`
  - `ARINC_SPI_MANUAL_CS=1`

## Resultado inicial observado

Se ejecutaron varios arranques del sistema. En algunas corridas el TX eligio
`100 kbps` y en otras `12.5 kbps`. El bridge reflejo correctamente la velocidad
detectada por el sniffer.

Corrida corta registrada a `12.5 kbps`:

```json
{
  "accepted_words": 68865,
  "received_words": 229544,
  "filtered_words": 160679,
  "detected_bit_rate_bps": 12500,
  "detected_bit_rate_txt": "12.5 kbps",
  "parity_errors_sniffer": 0,
  "overflow_events": 0,
  "spi_drop_events": 0,
  "spi_errors": 0,
  "spi_startup_errors": 4,
  "fwd_resync_events": 3,
  "fwd_startup_resync_events": 3,
  "fwd_operational_resync_events": 0
}
```

## Lectura tecnica

El balance principal cierra exacto:

```text
received_words = accepted_words + filtered_words
229544 = 68865 + 160679
```

No hubo errores de paridad, overflow, drops SPI ni errores SPI operativos. Los
`spi_startup_errors` pertenecen al arranque y no contaminan el contador
operativo. Los `fwd_resync_events` corresponden al arranque
(`fwd_startup_resync_events = 3`) y no a regimen
(`fwd_operational_resync_events = 0`).

La proporcion de palabras aceptadas es coherente con el patron de TX:

- `3` labels utiles
- `7` labels de ruido
- aceptacion esperada aproximada:
  - `3 / 10 = 30%`

Resultado medido:

```text
accepted_words / received_words = 68865 / 229544 = 30.0%
```

## Corridas de 30 minutos

Luego de la prueba corta, se ejecutaron dos sesiones de `30 min` con
`stats_recorder.py`: una con el TX en `12.5 kbps` y otra con el TX en
`100 kbps`.

### 12.5 kbps

Sesion:

```text
20260619_152747_autorate_12k5_30m
```

Resumen:

| Campo | Valor |
|---|---:|
| Resultado | PASS |
| Motivo de cierre | duration_reached |
| Duracion | 1800.111 s |
| Muestras | 360 |
| Velocidad detectada inicial | 12.5 kbps |
| Velocidad detectada final | 12.5 kbps |
| Palabras recibidas | 618842 |
| Palabras aceptadas | 185652 |
| Palabras filtradas | 433190 |
| Balance recibidas-clasificadas | 0 |
| Errores de paridad | 0 |
| Errores SPI operativos | 0 |
| Errores SPI de arranque | 0 |
| Overflow | 0 |
| Drops SPI | 0 |
| Resync FWD operativos | 0 |
| Tasa media aceptada | 103.134 words/s |
| Tasa maxima aceptada | 105.437 words/s |

La velocidad detectada se mantuvo constante en todas las muestras:

```text
360/360 muestras = 12500 bps
```

### 100 kbps

Sesion:

```text
20260619_160452_autorate_100k_30m
```

Resumen:

| Campo | Valor |
|---|---:|
| Resultado | PASS |
| Motivo de cierre | duration_reached |
| Duracion | 1800.119 s |
| Muestras | 360 |
| Velocidad detectada inicial | 100 kbps |
| Velocidad detectada final | 100 kbps |
| Palabras recibidas | 4807727 |
| Palabras aceptadas | 1442318 |
| Palabras filtradas | 3365409 |
| Balance recibidas-clasificadas | 0 |
| Errores de paridad | 0 |
| Errores SPI operativos | 0 |
| Errores SPI de arranque | 0 |
| Overflow | 0 |
| Drops SPI | 0 |
| Resync FWD operativos | 0 |
| Tasa media aceptada | 801.235 words/s |
| Tasa maxima aceptada | 826.529 words/s |

La velocidad detectada se mantuvo constante en todas las muestras:

```text
360/360 muestras = 100000 bps
```

## Comparacion

| Metrica | 12.5 kbps | 100 kbps |
|---|---:|---:|
| Palabras recibidas | 618842 | 4807727 |
| Palabras aceptadas | 185652 | 1442318 |
| Tasa media aceptada | 103.134 words/s | 801.235 words/s |
| Errores SPI operativos | 0 | 0 |
| Errores de paridad | 0 | 0 |
| Overflow | 0 | 0 |
| Drops SPI | 0 | 0 |

La relacion de tasa media aceptada fue:

```text
801.235 / 103.134 = 7.77
```

Es coherente con el cambio esperado entre `100 kbps` y `12.5 kbps`, que es una
relacion ideal de `8:1`. La pequena diferencia surge de rafagas, pausas
aleatorias y redondeos de muestreo.

## Estado

La funcionalidad autorate queda validada:

- el TX puede arrancar en `100 kbps` o `12.5 kbps`;
- el RX stream recibe sin configuracion especifica de velocidad;
- el sniffer detecta `100 kbps` y `12.5 kbps` y lo exporta al bridge;
- el bridge, dashboard, Prometheus y recorder ya tienen campos para reportarlo.

## Corridas finales de 8 horas

La validacion final se extendio con dos corridas largas independientes:

| Metrica | 12.5 kbps | 100 kbps |
|---|---:|---:|
| Sesion | `20260619_212806_corrida8h_12k5` | `20260620_065154_corrida8h_100k` |
| Resultado | PASS | PASS |
| Motivo de cierre | duration_reached | duration_reached |
| Duracion | 28801.451 s | 28801.407 s |
| Muestras | 5760 | 5760 |
| Velocidad detectada | 12500 bps | 100000 bps |
| Palabras recibidas | 9876638 | 76707606 |
| Palabras aceptadas | 2962992 | 23012283 |
| Palabras filtradas | 6913646 | 53695323 |
| Tasa media aceptada | 102.876 words/s | 798.999 words/s |
| Tasa maxima aceptada | 108.233 words/s | 828.346 words/s |
| Errores de paridad | 0 | 0 |
| Errores SPI operativos | 0 | 0 |
| Overflow | 0 | 0 |
| Drops SPI | 0 | 0 |
| Resync FWD operativos | 0 | 0 |
| Muestras desconectadas | 0 | 0 |
| Muestras con `last_error` | 0 | 0 |

La relacion entre tasas medias aceptadas fue:

```text
798.999 / 102.876 = 7.77
```

Esto es consistente con la relacion ideal `8:1` entre `100 kbps` y `12.5 kbps`.
La evidencia detallada queda en
[evidencia_corridas_finales_autorate_8h.md](evidencia_corridas_finales_autorate_8h.md).
