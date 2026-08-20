# Avance del front-end electrico - 2026-08-20

## Resultado

El prototipo de banco basado en TL062 funciono correctamente a las dos
velocidades previstas:

| Velocidad | Tiempo de bit | Media celda activa | Estado |
| --- | ---: | ---: | --- |
| `12,5 kbps` | `80 us` | `40 us` | validado |
| `100 kbps` | `10 us` | `5 us` | validado |

La validacion incluyo observacion de `LINE_A`, `LINE_B`, la diferencia entre
ambas lineas y las salidas logicas recuperadas. Se observaron palabras
completas, retorno a cero y los tres estados diferenciales esperados.

## Configuracion vigente

| Funcion | Conexion o componente |
| --- | --- |
| Pico TX `FWD_A` | `GP6` |
| Pico TX `FWD_B` | `GP7` |
| Operacionales | TL062 en encapsulado DIP8 |
| Alimentacion analogica | `+12 V / GND / -12 V` |
| Salida por rama | aproximadamente `+5 V / 0 V / -5 V` |
| Salida diferencial | aproximadamente `+10 V / 0 V / -10 V` |
| Resistencias serie | `39 ohm` por rama |
| Comparador RX | LM393N con pull-up a `3,3 V` |

Las masas de las Pico, la fuente bipolar y la Raspberry Pi 3B+ se conectan a
una referencia comun de laboratorio.

## Firmware TX

Los targets vigentes son:

- `arinc_tx_arinc429_logic_stream_gp6_gp7`: `100 kbps`;
- `arinc_tx_arinc429_logic_stream_12k5_gp6_gp7`: `12,5 kbps`.

Los UF2 correspondientes se generan en `ARINC-TX/build/`:

- `arinc_tx_arinc429_logic_stream_gp6_gp7.uf2`;
- `arinc_tx_arinc429_logic_stream_12k5_gp6_gp7.uf2`.

## Evidencia

Las 16 capturas originales del osciloscopio se conservan sin modificar en
[`ELECTRICO/frontend_arinc429_lab/evidencia/20-08`](../ELECTRICO/frontend_arinc429_lab/evidencia/20-08/README.md).

Este resultado reemplaza como estado vigente la limitacion dinamica observada
en ensayos anteriores. Para documentos nuevos, el componente validado del
front-end es TL062 y el cableado TX es `GP6/GP7`.
