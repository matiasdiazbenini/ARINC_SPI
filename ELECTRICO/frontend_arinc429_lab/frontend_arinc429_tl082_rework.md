# Front-end ARINC 429-like con TL082CP - revision E

Este documento describe el circuito de banco vigente entre una Pico TX y una
Pico RX. Los valores fueron recalculados con los componentes adquiridos y
coinciden con `frontend_arinc429_tl082_circuit.kicad_sch`.

## Arquitectura electrica

- `U1 TL082CP`: genera `LINE_A` y `LINE_B` bipolares.
- `R9/R10`: fijan la impedancia diferencial de fuente.
- `J2`: expone `LINE_A`, `LINE_B` y `GND_REF`.
- `U2 TL082CP`: atenúa y desplaza la diferencia A-B al dominio positivo.
- `U3 MCP6562`: decide el estado y entrega logica segura de `0..3,3 V`.

El TL082 alimentado con fuente bipolar no debe conectarse directamente a los
GPIO. La frontera segura hacia la Pico RX sigue siendo U3.

## Objetivos de linea

- `LINE_A/LINE_B`: aproximadamente `+5/-5 V`, `0/0 V` y `-5/+5 V`.
- `LINE_A - LINE_B`: aproximadamente `+10 V`, `0 V` y `-10 V`.
- impedancia de fuente diferencial: aproximadamente `78 ohm`.
- receptor: alta impedancia, mayor que `8 kohm`.
- alimentacion de U1/U2: `+12 V / GND / -12 V`.
- alimentacion de U3: `+3,3 V / GND`.

## TX bipolar con los valores adquiridos

Las cuatro redes diferenciales usan:

```text
R1, R3, R5, R7 = 22 kohm
R2, R4, R6, R8 = 33 kohm
k = 33/22 = 1,5
```

Por lo tanto:

```text
TXA_DRV = 1,5 * (TX_A_LOGIC - TX_B_LOGIC)
TXB_DRV = 1,5 * (TX_B_LOGIC - TX_A_LOGIC)
```

Con `3,3 V` logicos, cada conductor alcanza `+/-4,95 V` y el diferencial
alcanza `+/-9,9 V`. El cambio desde `20/30 kohm` a `22/33 kohm` no modifica la
ganancia porque ambas relaciones valen exactamente `1,5`.

Cada GPIO ve aproximadamente:

```text
Z_GPIO = 22 kohm || (22 kohm + 33 kohm) = 15,71 kohm
I_GPIO = 3,3 V / 15,71 kohm = 0,210 mA
```

R9 y R10 deben seguir siendo `39 ohm`, no `39 kohm`:

```text
Zout diferencial ~= 39 ohm + 39 ohm = 78 ohm
```

Las resistencias compradas de `38 kohm` no sirven para esta funcion.

## RX analogico recalculado

U2 usa:

```text
R11, R13, R15, R17 = 99 kohm
R12, R14, R16, R18 = 10 kohm
a = 10/99 = 0,101010
```

Las salidas son:

```text
RX_A_BIASED = VREF_RX + 0,101010 * (LINE_A - LINE_B)
RX_B_BIASED = VREF_RX + 0,101010 * (LINE_B - LINE_A)
```

La impedancia de entrada diferencial resulta aproximadamente igual a la
resistencia de entrada:

```text
Zin diferencial RX ~= 99 kohm
```

La carga supera por mas de doce veces el minimo de `8 kohm` y apenas reduce el
diferencial TX nominal: `9,9 V` pasa a aproximadamente `9,892 V`.

## VREF_RX cargada

R21 y R22 son dos resistencias de `1 kohm`. Las dos redes no inversoras de U2
cargan el divisor mediante dos caminos de `99 kohm + 10 kohm`. Su equivalente
es:

```text
Rload = 109 kohm || 109 kohm = 54,5 kohm
Rbottom_eq = 1 kohm || 54,5 kohm = 981,98 ohm
VREF_RX = 3,3 * 981,98 / (1000 + 981,98) = 1,635 V
```

El valor no es `1,65 V` exacto, pero queda controlado y se usa explicitamente
en el calculo del umbral. Con C10 de `100 nF`:

```text
Rth_REF = 1k || 1k || 54,5k = 495,45 ohm
tau_REF = 49,55 us
fc_REF = 3,21 kHz
```

## VTH_RX recalculado

El umbral no se eligio mirando solamente el NULL ideal de `0 V`. Se considero
la ventana de recepcion completa:

- maximo diferencial que todavia puede ser NULL: `+2,5 V`;
- minimo diferencial que ya debe ser reconocido como activo: `+6,5 V`.

La rama superior del divisor usa dos valores comprados en paralelo:

```text
R23 || R25 = 6,7 kohm || 38 kohm = 5,6957 kohm
R24 = 10 kohm
VTH_RX = 3,3 * 10 / (5,6957 + 10) = 2,1025 V
```

Con `VREF_RX = 1,635 V` y `a = 10/99`:

```text
NULL maximo (+2,5 V): 1,635 + 0,101010*2,5 = 1,8875 V
activo minimo (+6,5 V): 1,635 + 0,101010*6,5 = 2,2916 V
```

Los margenes nominales son:

```text
margen contra NULL = 2,1025 - 1,8875 = 0,2150 V
margen activo       = 2,2916 - 2,1025 = 0,1891 V
```

El umbral queda casi centrado en la zona indeterminada. C11 de `100 nF` ve:

```text
Rth_VTH = (6,7k || 38k) || 10k = 3,6288 kohm
tau_VTH = 362,9 us
fc_VTH = 438,6 Hz
```

## Niveles esperados en U2

| Diferencial A-B | RX_A_BIASED | RX_B_BIASED | Lectura |
|---:|---:|---:|---|
| `+13 V` | `2,948 V` | `0,322 V` | extremo activo positivo |
| `+9,9 V` | `2,635 V` | `0,635 V` | activo nominal |
| `+6,5 V` | `2,292 V` | `0,978 V` | activo minimo |
| `+2,5 V` | `1,888 V` | `1,382 V` | limite NULL |
| `0 V` | `1,635 V` | `1,635 V` | NULL ideal |

Todos estos valores nominales permanecen dentro de `0..3,3 V`, dominio de
entrada del MCP6562. U3 entrega la salida digital final y trabaja activo en
alto: `RX_x_BIASED` entra por la entrada no inversora y `VTH_RX` por la
inversora. Por lo tanto, la salida correspondiente sube a `3,3 V` cuando el
conductor supera el umbral.

## Tolerancias

Con resistencias independientes de `1 %`, el peor caso calculado conserva:

- margen contra falso activo desde NULL: aproximadamente `176,6 mV`;
- margen para detectar el activo minimo: aproximadamente `144,2 mV`.

Con resistencias independientes de `5 %`, el peor caso teorico puede llevar el
margen contra falso activo a apenas `19,8 mV` y el margen activo a
aproximadamente `-31,9 mV`. Por eso el montaje requiere una de estas dos
condiciones:

1. resistencias de `1 %`; o
2. medir las unidades compradas y seleccionar grupos apareados cercanos al
   valor nominal.

La segunda opcion es viable porque se compraron diez unidades de cada valor.

## Respuesta temporal

El cambio de valores no altera la ganancia de ruido de U1, que sigue siendo
`1 + 33/22 = 2,5`. Con slew rate del TL082 de `13 V/us` tipico y `8 V/us`
conservador, un cambio de `4,95 V` requiere aproximadamente:

```text
t_slew tipico = 4,95/13 = 0,381 us
t_slew conservador = 4,95/8 = 0,619 us
```

Esto es suficientemente rapido para medias celdas de `5 us` a `100 kbps` y de
`40 us` a `12,5 kbps`. El flanco bruto es mas rapido que el perfil ARINC, por
lo que el conformado opcional debe comprobarse con osciloscopio.

Con Rf de `33 kohm`, los valores orientativos en paralelo con R2/R4/R6/R8 son:

- `22 pF`: rise time RC aproximado `1,60 us` para 100 kbps;
- `150 pF`: rise time RC aproximado `10,89 us` para 12,5 kbps.

No se montan ambos valores simultaneamente en un sistema autorate.

## Componentes disponibles y pendientes

Los `TL082CP`, redes de `22/33 kohm`, `99/10 kohm`, referencias, capacitores
`104` de `100 nF`, capacitores de `10 uF`, bornera y cableado ya estan
disponibles.

Todavia son necesarios o deben confirmarse:

- `MCP6562-E/P` para U3;
- dos resistencias de `39 ohm` para R9/R10;
- fuente bipolar regulada `+12 V / GND / -12 V`;
- tres zocalos PDIP-8 correctos y la placa de montaje.

El codigo `104` corresponde a `100 nF`; no corresponde a `1 uF`.
