# Evidencia final autorate 8 h - 100 kbps y 12.5 kbps

## Objetivo

Validar la fase logica ARINC con el sistema completo en las dos velocidades
soportadas por la etapa actual:

- `100 kbps`
- `12.5 kbps`

La prueba verifica que `ARINC-SNIFFER` detecta autonomamente la velocidad, que
el transporte SPI `pio-frame` con `DRDY` se mantiene estable, que el supervisor
permanece sano y que no aparecen errores operativos durante una corrida larga.

## Configuracion comun

| Bloque | Configuracion |
|---|---|
| TX | `arinc_tx_arinc429_logic_stream_autorate` |
| RX | `arinc_rx_arinc429_logic_stream` |
| SNIFFER | `sniffer_arinc429_logic_pio_frame` |
| SPI | `pio-frame`, `8 MHz`, CS manual |
| Peticion host | `DRDY` en `GPIO25`, fallback por timeout |
| Bridge | `http://127.0.0.1:5100` |
| Recorder | `stats_recorder.py`, muestra cada `5 s` |
| Duracion objetivo | `28800 s` |

Build esperada del sniffer:

```text
SPI-PIOFRAME-ARINC429-STRICTPARITY-DRDY-AUTORATE-RECOVERY-V5
```

## Sesiones

| Velocidad | Sesion | Inicio | Fin | Duracion | Muestras | Veredicto |
|---|---|---|---|---:|---:|---|
| 12.5 kbps | `20260619_212806_corrida8h_12k5` | 2026-06-19 21:28:11 | 2026-06-20 05:28:13 | 28801.451 s | 5760 | PASS |
| 100 kbps | `20260620_065154_corrida8h_100k` | 2026-06-20 06:51:59 | 2026-06-20 14:52:01 | 28801.407 s | 5760 | PASS |

En ambas sesiones el cierre fue por `duration_reached`.

## Resultado comparativo

| Metrica | 12.5 kbps | 100 kbps |
|---|---:|---:|
| Velocidad detectada | 12500 bps | 100000 bps |
| Palabras recibidas | 9876638 | 76707606 |
| Palabras aceptadas | 2962992 | 23012283 |
| Palabras filtradas | 6913646 | 53695323 |
| Tasa media aceptada | 102.876 words/s | 798.999 words/s |
| Tasa maxima aceptada | 108.233 words/s | 828.346 words/s |
| Balance de clasificacion | 0 | 0 |
| Errores de paridad | 0 | 0 |
| Errores SPI operativos | 0 | 0 |
| Errores SPI de arranque durante la corrida | 0 | 0 |
| Overflow | 0 | 0 |
| Drops SPI | 0 | 0 |
| Resync FWD operativos | 0 | 0 |
| Muestras desconectadas | 0 | 0 |
| Muestras con `last_error` | 0 | 0 |

La relacion de tasa media aceptada fue:

```text
798.999 / 102.876 = 7.77
```

Esto es coherente con la relacion teorica entre `100 kbps` y `12.5 kbps`, que
es `8:1`. La diferencia pequena se explica por rafagas, pausas del generador,
muestreo cada `5 s` y redondeos.

## Lectura tecnica

En ambas corridas se cumple:

```text
received_words = accepted_words + filtered_words
```

Para `12.5 kbps`:

```text
9876638 = 2962992 + 6913646
```

Para `100 kbps`:

```text
76707606 = 23012283 + 53695323
```

Eso indica que el bridge conserva la contabilidad completa: cada palabra
recibida queda clasificada como aceptada o filtrada, sin palabras perdidas en
la estadistica.

## Supervisor y recuperacion

Durante ambas corridas:

- `supervisor_state = RUNNING`
- `supervisor_health = ok`
- `supervisor_issue = none`
- `actions_total = 0`
- `restarts_total = 0`
- `spi_recovers_total = 0`
- `physical_faults_total = 0`

Esto significa que el supervisor estuvo activo, pero no tuvo que intervenir. El
sistema permanecio sano durante toda la prueba.

## Evidencia incluida en el repo

Resumenes JSON:

- [summary_corrida8h_12k5.json](entrega_tutor/datos/summary_corrida8h_12k5.json)
- [summary_corrida8h_100k.json](entrega_tutor/datos/summary_corrida8h_100k.json)

Reportes PDF:

- [reporte_corrida8h_12k5.pdf](entrega_tutor/reportes/reporte_corrida8h_12k5.pdf)
- [reporte_corrida8h_100k.pdf](entrega_tutor/reportes/reporte_corrida8h_100k.pdf)

Los `samples.csv` completos permanecen fuera de Git en `ARINC_RESULTS` para no
versionar miles de muestras. Los JSON y PDF incluidos conservan configuracion,
contadores finales, deltas, tasas y veredicto.

## Conclusion

Las corridas de 8 h validan el cierre de la fase logica `arinc-logic-v1.1.0`:

- operacion estable a `100 kbps` y `12.5 kbps`;
- deteccion autonoma de velocidad por parte del sniffer;
- SPI `pio-frame` a `8 MHz` sin errores operativos;
- peticion por `DRDY` funcionando;
- paridad estricta sin errores en corrida limpia;
- supervisor activo sin necesidad de recuperacion;
- balance de contadores exacto.

El siguiente paso tecnico ya no es de software logico, sino de interfaz
electrica ARINC real: entrada diferencial, proteccion, alta impedancia y
adaptacion de niveles hacia la Pico sniffer.
