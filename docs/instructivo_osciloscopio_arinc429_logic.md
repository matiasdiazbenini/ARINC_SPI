# Instructivo de osciloscopio para ARINC 429 logic

## Objetivo
- Mostrar visualmente que en la fase actual ya existe una recreacion logica/temporal con retorno a cero.
- Obtener capturas para documentacion y presentacion.

## Donde conectar

### Canal FWD master -> slave
- Punta CH1 a `FWD_A` de la linea observada.
- Punta CH2 a `FWD_B` de la misma linea.
- Masas del osciloscopio a GND comun del sistema.

### Canal REV slave -> master
- Punta CH1 a `REV_A`.
- Punta CH2 a `REV_B`.
- Masa a GND comun.

## Formas de medir

### Medicion simple contra GND
- CH1 = `A` contra GND
- CH2 = `B` contra GND
- Permite ver el retorno a cero en cada hilo.

### Medicion diferencial
- Ver `CH1 - CH2`
- Sirve para mostrar mejor la bipolaridad logica:
  - `HI = 10`
  - `LO = 01`
  - `NULL = 00`

## Configuracion sugerida
- Acoplamiento: DC
- Escala vertical inicial: 1 V/div o 2 V/div
- Base de tiempo inicial:
  - `100 kbps`: 5 us/div, 10 us/div, 50 us/div o 100 us/div segun se quiera
    ver bit, palabra o gap.
  - `12.5 kbps`: 100 us/div o 500 us/div para ver palabra y gap; 100 us/div
    para medir bit de 80 us.
- Trigger:
  - fuente CH1 o Math diferencial
  - flanco ascendente
  - nivel intermedio
- Modo adquisicion:
  - normal o single para capturas limpias

## Que deberia verse
- En cada bit:
  - primera mitad con actividad
  - segunda mitad en cero
- Eso representa el retorno a cero.
- Entre palabras:
  - una zona mas larga en `NULL`

Valores de referencia validados:

| Velocidad | Media celda activa | Bit completo | Palabra 32 bits | Gap visible validado |
|---|---:|---:|---:|---:|
| `100 kbps` | `5 us` | `10 us` | `320 us` | `44 us` |
| `12.5 kbps` | `40 us` | `80 us` | `2.56 ms` | `360 us` |

## Como saber si esta bien
- Si en `A` y `B` ves pulsos alternados con retorno a cero, la forma temporal es correcta.
- Si en `CH1 - CH2` ves tres estados logicos:
  - positivo
  - cero
  - negativo
  entonces la recreacion bipolar logica esta lograda.

## Capturas recomendadas
- Una captura de `FWD_A` y `FWD_B` contra GND.
- Una captura en modo diferencial `CH1 - CH2`.
- Una captura ampliada de pocos bits.
- Una captura donde se vea el espacio entre palabras.
- Para autorate:
  - una captura a `100 kbps` con palabra de `320 us`;
  - una captura a `100 kbps` con gap cercano a `44 us`;
  - una captura a `12.5 kbps` con bit de `80 us`;
  - una captura a `12.5 kbps` con gap cercano a `360 us`.

## Que aclarar en el informe
- Esto valida la fase logica/temporal.
- No valida todavia la capa electrica ARINC 429 real.
- La futura fase electrica requerira transceptores/line drivers reales.
- La evidencia autorate final quedo documentada en
  `docs/evidencia_osciloscopio_autorate_20260629.md`.
