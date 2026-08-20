# Front-end electrico ARINC 429 - banco

Proyecto KiCad inicial para revisar el acondicionamiento electrico de laboratorio.

## Estado vigente

La configuracion de banco vigente usa TL062 y fue validada el 20 de agosto de
2026 a `12,5 kbps` y `100 kbps`. El TX recibe `FWD_A` por `GP6` y `FWD_B` por
`GP7`. El detalle se encuentra en
[avance_frontend_electrico_20260820.md](../../docs/avance_frontend_electrico_20260820.md)
y las capturas originales en [evidencia/20-08](evidencia/20-08/README.md).

## Archivos

- `frontend_arinc429_lab.kicad_pro`: proyecto KiCad.
- `frontend_arinc429_lab.kicad_sch`: esquematico funcional.
- `frontend_arinc429_lab.kicad_pcb`: PCB preliminar de laboratorio.
- `exports/frontend_arinc429_lab.svg`: export del esquematico.
- `exports/frontend_arinc429_lab_pcb.svg`: vista rapida de distribucion PCB.
- `relevamiento_componentes_frontend_electrico.md`: componentes, precios, equivalencias y busqueda de disponibilidad.
- `frontend_arinc429_tl082_rework.md`: revision nueva usando TL082CP como base de TX/RX analogico.
- `exports/frontend_arinc429_tl082_rework.svg`: esquematico funcional de la revision TL082CP.
- `frontend_arinc429_tl082_circuit.kicad_pro`: proyecto KiCad del circuito TL082CP completo.
- `frontend_arinc429_tl082_circuit.kicad_sch`: esquematico de circuito con resistencias, capacitores, conectores, TL082CP y adaptacion logica.
- `exports/frontend_arinc429_tl082_circuit.svg`: export del esquematico de circuito.
- `exports/frontend_arinc429_tl082_circuit.pdf`: export PDF del esquematico de circuito.
- `memoria_calculo_frontend_tl082.tex`: memoria de calculo editable en LaTeX.
- `exports/memoria_calculo_frontend_tl082.pdf`: memoria de calculo compilada.
- `proteus/frontend_arinc429_proteus.pdsprj`: simulacion Proteus 8.15 del
  front-end TX/RX con TL082 y comparadores LM393.
- `evidencia/20260814_banco_tl082/README.md`: resultado y capturas de la
  validacion fisica del prototipo a `12,5 kbps` y `100 kbps`.
- `evidencia/20-08/README.md`: validacion vigente con TL062 a `12,5 kbps` y
  `100 kbps`.

## Alcance

Este esquema y la PCB son una base de revision y prototipo. No son todavia una
PCB final ni un transceptor certificable para campo.

## Conexion funcional

- `GP6`: TX_A_LOGIC / FWD_A.
- `GP7`: TX_B_LOGIC / FWD_B.
- `GP4`: RX_A_LOGIC.
- `GP5`: RX_B_LOGIC.
- `LINE_A/LINE_B`: par diferencial de banco.
- `+6V/-6V`: alimentacion del driver bipolar.
- `+3V3`: logica Pico y salidas de comparadores.
- `GND`: referencia comun del banco.

## Conexiones explicitas del esquematico

| Net | Desde | Hasta | Funcion |
| --- | --- | --- | --- |
| `TX_A_LOGIC` | Pico `GP6` | entrada `U1A` | dato logico lado A antes de acondicionar |
| `TX_B_LOGIC` | Pico `GP7` | entrada `U1A` | dato logico lado B antes de acondicionar |
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

La PCB preliminar se genero para tener una aproximacion editable en KiCad antes
de cerrar la compra final de componentes. La revision vigente es `Rev B`: deja
los footprints separados por bloque funcional y no incluye ruteo final. KiCad
debe mostrar las conexiones pendientes como airwires/ratsnest para que el ruteo
se haga manualmente cuando ya esten definidos los encapsulados reales.

La version compacta anterior, con pistas preliminares cruzando la placa, no se
considera valida como layout para mostrar ni fabricar. La distribucion vigente
esta pensada para banco:

- conectores de entrada/salida en bordes;
- `J1` como header simple hacia la Pico de prueba;
- `J2` como bornera de linea `LINE_A`, `LINE_B`, `GND_REF`;
- `J3` como bornera de alimentacion `+3V3`, `+6V`, `-6V`, `GND`;
- `U1` en DIP8 para TL062 o equivalente de op-amp dual;
- `U2` en DIP8 para LM393N de banco o adaptador hacia MCP6562/LMV393 si luego se compran SMD;
- resistencias y capacitores THT para facilitar cambios;
- testpoints para `LINE_A`, `LINE_B`, `GND_COMUN` y `VTH_COMP`;
- pull-ups `R13/R14` marcados como `4k7/DNP`: se montan si se usa LM393/LM393B y se dejan sin montar si el comparador final tiene salida push-pull.

La PCB no debe enviarse a fabricar sin revision manual en KiCad. Falta confirmar
footprints exactos, dimensiones reales de borneras, corriente disponible de las
fuentes, separaciones, DRC y comportamiento analogico con componentes reales.

## Revision TL082CP

La revision [frontend_arinc429_tl082_rework.md](frontend_arinc429_tl082_rework.md)
documenta el rediseño pedido con TL082CP:

- TX bipolar con dos amplificadores diferenciales retroalimentados;
- `Zout` diferencial aproximada de `78 ohm` usando `39 ohm` por rama;
- J2 como linea ARINC-like de banco;
- RX analogico con TL082CP y alta impedancia;
- restriccion explicita: el RX TL082-only no debe conectarse directo a GPIO sin
  una etapa final de decision/proteccion.

El esquematico de circuito [frontend_arinc429_tl082_circuit.kicad_sch](frontend_arinc429_tl082_circuit.kicad_sch)
complementa esa revision con una implementacion electrica concreta:

- `U1A/U1B TL082CP`: convierten `TX_A_LOGIC/TX_B_LOGIC` en `LINE_A/LINE_B`
  de aproximadamente `+5/0/-5 V` por conductor, con redes
  `22 kohm / 33 kohm` y ganancia `1,5`.
- `R9/R10 = 39 ohm`: aproximan `Zout` diferencial a `78 ohm`.
- `U2A/U2B TL082CP`: leen `LINE_A/LINE_B` con alta impedancia y generan
  senales analogicas con `99 kohm / 10 kohm`, ganancia `10/99` y
  `Zin` diferencial aproximada de `99 kohm`.
- `U3 MCP6562`: convierte las senales analogicas a `RX_A_LOGIC/RX_B_LOGIC`
  seguros y activos en alto para `GP4/GP5`; la senal sesgada entra por la
  entrada no inversora y `VTH_RX` por la inversora.
- `R21/R22 = 1 kohm / 1 kohm`: generan `VREF_RX = 1,635 V` al incluir
  la carga real.
- `R23 || R25 = 6,7 kohm || 38 kohm` y `R24 = 10 kohm`: generan
  `VTH_RX = 2,102 V`.
- Con resistencias independientes de `1 %`, el peor caso conserva unos
  `176,6 mV` contra falso activo y `144,2 mV` para el activo minimo. Con `5 %`,
  el segundo margen puede hacerse negativo, por lo que hay que medir y aparear.
- `C1..C11`: desacople y filtrado local de referencias.

Aunque se prioriza TL082CP, el esquematico incluye `U3` porque la salida de un
TL082 alimentado en bipolar no debe entrar directamente a una Raspberry Pi Pico.
Sin esa etapa, el circuito no queda cerrado de forma segura.

La [memoria de calculo](exports/memoria_calculo_frontend_tl082.pdf) justifica
ganancias, impedancias, referencias, tolerancias, desacople y respuesta temporal
para `100 kbit/s` y `12,5 kbit/s`. La revision E ya incorpora el recalculo de
`VREF_RX` y `VTH_RX` con los valores adquiridos.

## Estado de componentes para la revision E

Ya disponibles: dos TL082CP, resistencias de `22 kohm`, `33 kohm`, `99 kohm`,
`10 kohm`, `1 kohm`, `6,7 kohm` y `38 kohm`, siete capacitores codigo `104`
de `100 nF`, cuatro capacitores de `10 uF`, bornera y cableado.

Pendientes o por confirmar:

- un `MCP6562-E/P`;
- dos resistencias de `39 ohm` para R9/R10;
- fuente bipolar regulada `+12 V / GND / -12 V`;
- confirmar tres zocalos PDIP-8 y definir la placa de montaje.

Las resistencias de `38 kohm` se usan como R25 en paralelo con R23 para ajustar
el umbral; no reemplazan las resistencias de salida de `39 ohm`.

## Checkpoint de validacion en banco

La evidencia del 20 de agosto de 2026 confirma el funcionamiento correcto del
front-end con TL062 a `12,5 kbps` y `100 kbps`. Se verificaron la salida
diferencial, los tiempos de bit, el retorno a cero y la recuperacion logica. El
detalle y las capturas originales se conservan en
[evidencia/20-08](evidencia/20-08/README.md).

### Antecedente de la revision anterior

La evidencia del 14 de agosto de 2026 confirma el funcionamiento de la cadena
completa a `12,5 kbps`, desde la Pico TX y el front-end electrico hasta la
recuperacion logica, el sniffer y la visualizacion. En la corrida observada no
se registraron errores.

El mismo montaje no resulto confiable a `100 kbps`: los TL082CP ensayados
presentaron rampas de aproximadamente `8..12 us`, un tiempo comparable con el
periodo de bit de `10 us`. A `12,5 kbps`, cuyo periodo es `80 us`, queda una
meseta util suficiente. El detalle y las capturas originales se conservan en
[evidencia/20260814_banco_tl082](evidencia/20260814_banco_tl082/README.md).

## Validacion minima

1. Medir `LINE_A` y `LINE_B` contra GND.
2. Medir `MATH = LINE_A - LINE_B`.
3. Confirmar aproximadamente `+10 V`, `0 V`, `-10 V` diferencial.
4. Confirmar que `RX_A_LOGIC` y `RX_B_LOGIC` nunca excedan `0..3,3 V`.

## Explicacion tecnica

El esquema representa un acondicionador electrico entre la logica de una
Raspberry Pi Pico y una linea diferencial tipo ARINC 429 de laboratorio.

La Pico entrega dos senales logicas: `TX_A_LOGIC` por `GP6` y `TX_B_LOGIC` por
`GP7`. Esas senales entran al bloque `TX bipolar`. La primera etapa, `U1A`,
esta planteada como un amplificador diferencial con ganancia 1,5:

```text
LINE_A = 1,5 * (TX_A_LOGIC - TX_B_LOGIC)
```

La segunda etapa, `U1B`, invierte `LINE_A`:

```text
LINE_B = -LINE_A
```

De esa manera, cuando `GP6` esta alto y `GP7` bajo, la linea queda
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

- `GP6` representa el lado A.
- `GP7` representa el lado B.

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
