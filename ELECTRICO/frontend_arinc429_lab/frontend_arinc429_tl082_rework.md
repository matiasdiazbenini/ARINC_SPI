# Front-end ARINC 429-like con TL082CP

Documento de revision para usar TL082CP entre una Pico TX y una Pico RX.

## Decision tecnica principal

El TL082CP es util para la etapa TX bipolar de banco. Permite construir dos
amplificadores diferenciales retroalimentados que convierten la logica
`0..3,3 V` de la Pico en una linea balanceada aproximadamente:

```text
LINE_A / LINE_B = +5/-5 V, 0/0 V, -5/+5 V
A-B             = +10 V,   0 V,   -10 V
```

La etapa RX no puede resolverse de forma segura usando solo TL082CP y conectando
la salida directamente a GPIO. Si el TL082 se alimenta con fuente bipolar, su
salida puede ir negativa o por encima de `3,3 V`. Si se alimenta con `0..3,3 V`,
el TL082 no cumple sus condiciones de operacion. Por eso el documento separa:

- RX analogico con TL082CP: calcula/atenua la diferencia con alta impedancia.
- Decision digital obligatoria: comparador, Schmitt trigger o proteccion activa
  antes de entrar a la Pico.

El esquematico KiCad complementario esta en
[frontend_arinc429_tl082_circuit.kicad_sch](frontend_arinc429_tl082_circuit.kicad_sch).
Ese archivo lleva esta idea a circuito: `U1` genera la linea bipolar, `U2`
reduce y sesga la senal recibida, y `U3` convierte el resultado a logica segura
para `GP4/GP5`.

## Referencias de diseno

Valores usados como objetivo de banco:

- cable ARINC: `78 ohm` caracteristico;
- transmisor ARINC: `75 ohm +/-5 ohm`, balanceado entre A y B;
- receptor ARINC: alta impedancia, minimo `8 kohm`;
- niveles diferenciales TX: `+10 V +/-1 V`, `0 V +/-0,5 V`, `-10 V +/-1 V`;
- 100 kbps: bit `10 us`, media celda `5 us`, rise/fall aprox. `1,5 us`;
- 12,5 kbps: bit `80 us`, media celda `40 us`, rise/fall aprox. `10 us`.

Datos relevantes del TL082CP:

- alimentacion recomendada: `+5..+15 V` y `-5..-15 V`;
- entrada comun: `VCC- + 4 V` a `VCC+ - 4 V`;
- slew rate tipico: `13 V/us`;
- slew rate minimo de tabla: `8 V/us`;
- rise time de ensayo: `0,05 us`;
- resistencia de entrada intrinseca: `10^12 ohm`;
- salida no rail-to-rail.

## Etapa TX bipolar

Se usan los dos amplificadores de un TL082CP:

```text
U1A: LINE_A_PRE = 1,5 * (TX_A_LOGIC - TX_B_LOGIC)
U1B: LINE_B_PRE = 1,5 * (TX_B_LOGIC - TX_A_LOGIC)
```

Valores base:

```text
R1/R3 = 20 k
R2/R4 = 30 k
Ganancia = 30 k / 20 k = 1,5
Rserie LINE_A = 39 ohm
Rserie LINE_B = 39 ohm
```

La salida diferencial queda:

```text
Zout diferencial ~= 39 + 39 = 78 ohm
```

Ese valor reproduce el orden de impedancia del cable ARINC. El receptor no debe
terminar la linea con `78 ohm`; debe mirar con alta impedancia.

Alimentacion recomendada:

```text
U1 TL082CP: +12 V / -12 V / GND
```

`+/-9 V` puede servir. `+/-6 V` queda marginal porque el TL082 no es rail-to-rail
y se necesita entregar aproximadamente `+/-5 V` por conductor.

No se colocan capacitores de acople en serie con la senal: el formato ARINC
necesita conservar el estado NULL en DC. Si se colocan capacitores, se deforma el
nivel de reposo. Si se quiere controlar pendiente, se debe hacer con una red de
slew/rise-time medida en osciloscopio, no con acople AC.

Si se desea suavizar flancos:

```text
Rserie 39 ohm + C diferencial 10 nF aprox. -> tau ~= 0,39 us
```

Ese valor es punto de partida. No se debe cerrar sin medir rise/fall real.

## Impedancias del TX

Por cada amplificador diferencial:

- entrada inversora: aproximadamente `20 k`;
- entrada no inversora: aproximadamente `20 k + 30 k = 50 k`;
- la impedancia intrinseca JFET del TL082 es mucho mayor y no domina.

Como cada GPIO alimenta ambos amplificadores, un camino inversor y uno no
inversor:

```text
Zin vista por GPIO ~= 20 k || 50 k = 14,3 k
```

Esto es aceptable para la Pico. La corriente por GPIO es del orden:

```text
3,3 V / 14,3 k ~= 0,23 mA
```

## Etapa J2

J2 representa la linea de transmision ARINC-like de banco:

```text
J2-1 = LINE_A
J2-2 = LINE_B
J2-3 = GND_REF / shield de banco
```

En J2 se debe medir:

```text
CH1 = LINE_A contra GND
CH2 = LINE_B contra GND
MATH = CH1 - CH2
```

Resultado esperado:

```text
LINE_A/LINE_B: +5/-5 V, 0/0 V, -5/+5 V
MATH A-B:      +10 V,   0 V,   -10 V
```

## Etapa RX con TL082CP

Usar solo TL082CP como si fuera comparador final no es seguro para la Pico.
La forma defendible es usarlo como front-end analogico de alta impedancia:

```text
U2A: RX_A_ANALOG = 0,3 * (LINE_A - LINE_B)
U2B: RX_B_ANALOG = 0,3 * (LINE_B - LINE_A)
```

Valores base:

```text
Rin = 100 k
Rf  = 30 k
Ganancia = 0,3
```

Con la linea valida:

```text
A-B = +10 V -> RX_A_ANALOG ~= +3 V, RX_B_ANALOG ~= -3 V
A-B =   0 V -> RX_A_ANALOG ~=  0 V, RX_B_ANALOG ~=  0 V
A-B = -10 V -> RX_A_ANALOG ~= -3 V, RX_B_ANALOG ~= +3 V
```

Esto demuestra por que falta una etapa de decision/proteccion: aparece una
salida negativa en el canal opuesto. La Pico no acepta tension negativa.

## Impedancias del RX analogico

Cada linea entra a dos redes de alta impedancia. Con `100 k / 30 k`:

```text
Zin por camino inversor ~= 100 k
Zin por camino no inversor ~= 130 k
Zin equivalente aproximada por linea ~= 100 k || 130 k ~= 56,5 k
```

La carga equivalente diferencial queda muy por encima del minimo de `8 k` de un
receptor ARINC. Por lo tanto, como sniffer/receptor de banco, no carga
apreciablemente la linea.

## Sobre masa, referencia e histeresis

Referenciar un detector a masa significa decidir alrededor de `0 V`. Eso sirve
para detectar signo, pero deja al sistema sensible al ruido cerca del NULL. La
histeresis se puede agregar con realimentacion positiva, pero en un TL082
alimentado con `+/-12 V` el umbral depende del swing de salida, que no es rail
to rail ni perfectamente repetible.

Referenciar contra un valor interno, por ejemplo `VREF = 1,65 V`, permite crear
umbrales mas claros en un dominio positivo. Pero para llegar ahi hay que
desplazar y escalar la senal, y luego usar una etapa no lineal. Esa etapa puede
ser:

- comparador push-pull a `3,3 V`;
- Schmitt trigger alimentado a `3,3 V`;
- transistor/MOSFET de traduccion de nivel;
- clamps externos mas resistencia serie.

Sin esa etapa no hay una salida digital segura y limpia para `GP4/GP5`.

## Tiempos

El TL082CP tiene slew rate tipico `13 V/us`. Para un cambio de `5 V` por
conductor:

```text
t_slew tipico ~= 5 V / 13 V/us = 0,38 us
t_slew conservador ~= 5 V / 8 V/us = 0,63 us
```

A `100 kbps`:

```text
bit = 10 us
media celda activa = 5 us
subida+bajada tipica ~= 0,76 us
zona no plana ~= 15,2 % de la media celda
meseta util ~= 4,24 us
```

A `12,5 kbps`:

```text
bit = 80 us
media celda activa = 40 us
subida+bajada tipica ~= 0,76 us
zona no plana ~= 1,9 % de la media celda
meseta util ~= 39,24 us
```

El TL082CP es suficientemente rapido para el banco. De hecho, sus flancos son
mas rapidos que los esperados en ARINC real, por lo que podria requerirse red RC
para aproximar el rise/fall especificado.

## Rango util de componentes

TX:

- `Rin` diferencial: `10 k` a `33 k`;
- `Rf`: `1,5 * Rin`;
- recomendacion inicial: `20 k / 30 k`;
- `Rserie`: `37,4 ohm` a `39 ohm` por rama.

RX analogico:

- `Rin`: `68 k` a `150 k`;
- `Rf`: `0,3 * Rin`;
- recomendacion inicial: `100 k / 30 k`;
- si se baja demasiado `Rin`, el RX carga mas la linea.

Desacople:

- `100 nF` ceramico por rail y por integrado;
- `10 uF` por rail cerca de cada bloque;
- fuente analogica recomendada: `+12 V / -12 V`.

## Conclusiones

La etapa TX con TL082CP queda definida y es viable para banco.

La etapa RX con solo TL082CP no reemplaza correctamente a comparadores o a un
receptor ARINC. Puede servir como acondicionador analogico de alta impedancia,
pero falta una etapa final que entregue `0..3,3 V` seguro a la Pico.

Si el objetivo es conservar el firmware actual `GP4/GP5`, la opcion tecnica mas
limpia sigue siendo usar comparadores o Schmitt trigger despues del front-end
analogico.
