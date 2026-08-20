# Evidencia de banco TL062 - 2026-08-20

## Alcance

Esta carpeta conserva las capturas originales del ensayo de laboratorio del 20
de agosto de 2026. Se probo el front-end electrico basado en TL062 con la Pico
TX conectada de la siguiente forma:

- `GP6 = FWD_A`;
- `GP7 = FWD_B`;
- fuente analogica `+12 V / GND / -12 V`;
- masa comun entre instrumentos, fuente, Pico y Raspberry Pi 3B+.

## Resultado observado

El circuito funciono correctamente a `12,5 kbps` y `100 kbps`. Las capturas
muestran los niveles bipolares, el estado NULL, palabras completas y las
mediciones temporales de las medias celdas activas.

| Velocidad | Tiempo de bit | Media celda activa | Resultado |
| --- | ---: | ---: | --- |
| `12,5 kbps` | `80 us` | `40 us` | correcto |
| `100 kbps` | `10 us` | `5 us` | correcto |

## Indice de capturas

| Archivos | Contenido principal |
| --- | --- |
| `TEK0007.JPG` a `TEK0012.JPG` | comparacion entre canales, flancos y mesetas de la etapa con TL062 |
| `TEK0013.JPG` a `TEK0017.JPG` | trenes de pulsos y mediciones a `12,5 kbps`, incluida la media celda de `40 us` |
| `TEK0005 (2).JPG` | palabra completa de `320 us` a `100 kbps` |
| `TEK0018.JPG` a `TEK0021.JPG` | trenes de pulsos y mediciones a `100 kbps`, incluida la media celda de `5 us` |

Los archivos JPG se mantienen con el nombre generado por el osciloscopio para
preservar la trazabilidad del ensayo.
