# Front-end electrico ARINC 429 - banco

Proyecto KiCad inicial para revisar el acondicionamiento electrico de laboratorio.

## Archivos

- `frontend_arinc429_lab.kicad_pro`: proyecto KiCad.
- `frontend_arinc429_lab.kicad_sch`: esquematico funcional.
- `frontend_arinc429_lab.kicad_pcb`: PCB preliminar de laboratorio.
- `exports/frontend_arinc429_lab.svg`: export del esquematico.
- `exports/frontend_arinc429_lab_pcb.svg`: vista rapida de distribucion PCB.
- `relevamiento_componentes_frontend_electrico.md`: componentes, precios, equivalencias y busqueda de disponibilidad.

## Alcance

Este esquema y la PCB son una base de revision y prototipo. No son todavia una
PCB final ni un transceptor certificable para campo.

## Conexion funcional

- `GP2`: TX_A_LOGIC.
- `GP3`: TX_B_LOGIC.
- `GP4`: RX_A_LOGIC.
- `GP5`: RX_B_LOGIC.
- `LINE_A/LINE_B`: par diferencial de banco.
- `+6V/-6V`: alimentacion del driver bipolar.
- `+3V3`: logica Pico y salidas de comparadores.
- `GND`: referencia comun del banco.

## Conexiones explicitas del esquematico

| Net | Desde | Hasta | Funcion |
| --- | --- | --- | --- |
| `TX_A_LOGIC` | Pico `GP2` | entrada `U1A` | dato logico lado A antes de acondicionar |
| `TX_B_LOGIC` | Pico `GP3` | entrada `U1A` | dato logico lado B antes de acondicionar |
| `LINE_A` | salida `U1A` | `J2 pin 1` y entrada `A_SENSE` | conductor A bipolar |
| `LINE_B` | salida `U1B` | `J2 pin 2` y entrada `B_SENSE` | conductor B bipolar |
| `GND_REF` | `J2 pin 3` | `GND_COMUN` | referencia de banco para medicion |
| `A_SENSE` | divisor/proteccion de `LINE_A` | entrada de `U2A` | senal reducida para comparar |
| `B_SENSE` | divisor/proteccion de `LINE_B` | entrada de `U2B` | senal reducida para comparar |
| `VTH_COMP` | divisor `R11/R12` | entradas de referencia de `U2A/U2B` | umbral de decision RX |
| `RX_A_LOGIC` | salida `U2A` | Pico `GP4` | senal recuperada A, ya en 0..3,3 V |
| `RX_B_LOGIC` | salida `U2B` | Pico `GP5` | senal recuperada B, ya en 0..3,3 V |
| `+3V3_LOGIC` | Pico/fuente 3,3 V | U2, pull-ups, divisor de umbral | alimentacion logica |
| `+6V_TX` | fuente bipolar | `U1 V+` | alimentacion positiva del TX analogico |
| `-6V_TX` | fuente bipolar | `U1 V-` | alimentacion negativa del TX analogico |
| `GND_COMUN` | Pico/fuente | J2, RX, divisor de umbral | referencia comun |

## Convencion visual

| Tipo de linea | Convencion |
| --- | --- |
| Senales y linea A/B | verde, trazo continuo |
| Alimentacion | rojo, trazo continuo mas grueso |
| GND y referencias | azul, trazo punteado |
| Umbral `VTH_COMP` | violeta, trazo punteado fino |
| Retorno RX hacia Pico | naranja, trazo punto-raya |

`VTH_COMP` no va a `J2`. `J2` solo expone la linea de banco `LINE_A`,
`LINE_B` y `GND_REF`. El umbral `VTH_COMP` es interno del receptor RX y entra a
los comparadores `U2A/U2B`.

`R11` y `R12` estan conectadas en serie como divisor resistivo:

```text
+3V3_LOGIC -> R11 -> VTH_COMP -> R12 -> GND_COMUN
```

La linea violeta entre `R11` y `R12` es el nodo medio del divisor. Las lineas
violetas que aparecen en `U2A` y `U2B` son esa misma red `VTH_COMP`; representan
la referencia de comparacion, no una conexion hacia la linea ARINC.

Las alimentaciones y referencias se muestran como etiquetas de red locales para
evitar cruces innecesarios. En KiCad, dos puntos con la misma etiqueta pertenecen
a la misma conexion electrica aunque no exista un cable dibujado entre ambos.

## Revision de conexiones criticas

- `U2A VDD` queda en `+3V3_LOGIC`.
- `U2A VSS` queda en `GND_COMUN`.
- `U2B VDD` queda en `+3V3_LOGIC`.
- `U2B VSS` queda en `GND_COMUN`.
- `J2 pin 3 GND_REF` queda unido a `GND_COMUN`.
- Los divisores `A_SENSE` y `B_SENSE` cierran por `GND_COMUN`.
- El divisor `R11/R12` genera `VTH_COMP` desde `+3V3_LOGIC` hacia `GND_COMUN`.
- `VTH_COMP` entra como referencia a `U2A/U2B`; no sale al conector de linea.
- `RX_A_LOGIC` vuelve a `GP4` y `RX_B_LOGIC` vuelve a `GP5`.
- `U1` usa alimentacion bipolar separada: `+6V_TX` y `-6V_TX`.

## PCB preliminar

La PCB inicial se genero para tener una aproximacion editable en KiCad antes de
cerrar la compra final de componentes. La distribucion esta pensada para banco:

- conectores de entrada/salida en bordes;
- `J1` como header simple hacia la Pico de prueba;
- `J2` como bornera de linea `LINE_A`, `LINE_B`, `GND_REF`;
- `J3` como bornera de alimentacion `+3V3`, `+6V`, `-6V`, `GND`;
- `U1` en DIP8 para TL072/TL082 o equivalente de op-amp dual;
- `U2` en DIP8 para LM393N de banco o adaptador hacia MCP6562/LMV393 si luego se compran SMD;
- resistencias y capacitores THT para facilitar cambios;
- testpoints para `LINE_A`, `LINE_B`, `GND_COMUN` y `VTH_COMP`;
- pull-ups `R13/R14` marcados como `4k7/DNP`: se montan si se usa LM393/LM393B y se dejan sin montar si el comparador final tiene salida push-pull.

La PCB no debe enviarse a fabricar sin revision manual en KiCad. Falta confirmar
footprints exactos, dimensiones reales de borneras, corriente disponible de las
fuentes, separaciones, DRC y comportamiento analogico con componentes reales.

## Validacion minima

1. Medir `LINE_A` y `LINE_B` contra GND.
2. Medir `MATH = LINE_A - LINE_B`.
3. Confirmar aproximadamente `+10 V`, `0 V`, `-10 V` diferencial.
4. Confirmar que `RX_A_LOGIC` y `RX_B_LOGIC` nunca excedan `0..3,3 V`.

## Explicacion tecnica

El esquema representa un acondicionador electrico entre la logica de una
Raspberry Pi Pico y una linea diferencial tipo ARINC 429 de laboratorio.

La Pico entrega dos senales logicas: `TX_A_LOGIC` por `GP2` y `TX_B_LOGIC` por
`GP3`. Esas senales entran al bloque `TX bipolar`. La primera etapa, `U1A`,
esta planteada como un amplificador diferencial con ganancia 1,5:

```text
LINE_A = 1,5 * (TX_A_LOGIC - TX_B_LOGIC)
```

La segunda etapa, `U1B`, invierte `LINE_A`:

```text
LINE_B = -LINE_A
```

De esa manera, cuando `GP2` esta alto y `GP3` bajo, la linea queda
aproximadamente `LINE_A=+5 V` y `LINE_B=-5 V`. Cuando se invierten los GPIO, la
polaridad diferencial tambien se invierte. Cuando ambos GPIO estan en cero, el
resultado esperado es `LINE_A=0 V` y `LINE_B=0 V`.

El conector `J2` expone el par `LINE_A/LINE_B`. En ese punto se debe medir con
osciloscopio tanto cada linea contra masa como la resta `MATH = LINE_A - LINE_B`.
El diferencial esperado es `+10 V`, `0 V` y `-10 V`.

El bloque `RX protegido y comparadores` toma `LINE_A` y `LINE_B` y no las lleva
directamente a la Pico. Primero cada linea pasa por un divisor/proteccion
(`A_SENSE` y `B_SENSE`). Luego `U2A` y `U2B` comparan esos nodos contra
`VTH_COMP`, aproximadamente `1,50 V`.

Las salidas de los comparadores vuelven a la Pico como senales logicas seguras:

```text
U2A -> RX_A_LOGIC -> GP4
U2B -> RX_B_LOGIC -> GP5
```

Esas salidas deben estar siempre entre `0 V` y `3,3 V`. Si el comparador elegido
es `LM393/LM393B`, la salida es open collector y necesita pull-up a `3V3`, no a
`5V`, para no danar la Pico.

## Explicacion simple

La Pico sola no puede sacar una senal ARINC real porque sus pines solo manejan
`0 V` y `3,3 V`. Entonces el circuito hace de traductor.

Primero la Pico dice que quiere transmitir usando dos pines:

- `GP2` representa el lado A.
- `GP3` representa el lado B.

Despues el bloque TX agranda esa senal y la convierte en una senal bipolar:

- si A esta activo, el cable A queda en `+5 V` y el cable B en `-5 V`;
- si B esta activo, el cable A queda en `-5 V` y el cable B en `+5 V`;
- si no hay pulso, ambos cables quedan cerca de `0 V`.

Eso es lo que se quiere ver en el osciloscopio como ARINC de laboratorio.

Luego viene el bloque RX. Ese bloque hace el trabajo inverso: mira cual de los
dos cables esta positivo, baja la senal a un valor seguro y la vuelve a entregar
a la Pico como `0 V` o `3,3 V`.

En criollo: el TX convierte la senal chica de la Pico en una senal grande tipo
ARINC, y el RX convierte esa senal grande otra vez en una senal chica que la Pico
puede leer sin quemarse.

La parte importante que debe quedar clara es esta:

```text
Linea ARINC-like -> RX -> comparadores -> RX_A_LOGIC/RX_B_LOGIC -> GP4/GP5
```

Ese es el regreso hacia la Pico.
