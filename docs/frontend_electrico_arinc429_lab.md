# Frente electrico ARINC 429 para banco de pruebas

Este documento resume la propuesta para pasar de la etapa logica actual, basada en GPIO de 0 V a 3,3 V, a una etapa electrica de laboratorio compatible con la forma de senal ARINC 429.

El objetivo inmediato no es conectar el sistema a una aeronave ni a una linea de campo certificada. El objetivo es construir un banco controlado con una Pico independiente, donde se pueda generar, acondicionar, medir y recuperar una senal bipolar tipo ARINC.

## Niveles electricos objetivo

En ARINC 429 real la linea se interpreta como un par diferencial A/B.

| Estado | Linea A vs GND | Linea B vs GND | Diferencial A-B |
| --- | ---: | ---: | ---: |
| HIGH | +5 V | -5 V | +10 V |
| NULL | 0 V | 0 V | 0 V |
| LOW | -5 V | +5 V | -10 V |

El sistema actual ya genera la logica correcta, pero a nivel GPIO:

| Estado actual | GP TX_A | GP TX_B | Diferencial logico |
| --- | ---: | ---: | ---: |
| HIGH logico | 3,3 V | 0 V | +3,3 V |
| NULL | 0 V | 0 V | 0 V |
| LOW logico | 0 V | 3,3 V | -3,3 V |

Por lo tanto, no conviene pensar el problema como "subir una senal a 5 V e invertirla". Conviene pensarlo como un transmisor diferencial bipolar:

- si TX_A esta alto y TX_B bajo, la linea debe quedar A = +5 V y B = -5 V;
- si TX_A esta bajo y TX_B alto, la linea debe quedar A = -5 V y B = +5 V;
- si ambos estan en 0 V, la linea debe quedar en NULL.

## Arquitectura propuesta

```text
Pico de prueba
  GP2/GP3 TX logico 0..3,3 V
        |
        v
Front-end TX bipolar
  genera A/B: +5/-5, 0/0, -5/+5
        |
        v
Linea diferencial A/B de banco
        |
        v
Front-end RX protegido
  atenua, centra y compara
        |
        v
GP4/GP5 RX logico 0..3,3 V
```

## Front-end TX recomendado

La opcion recomendada para el prototipo de banco es usar amplificadores operacionales con fuente bipolar.

La idea funcional es:

```text
LINE_A = 1,5 * (TX_A - TX_B)
LINE_B = 1,5 * (TX_B - TX_A)
```

Con GPIO de 3,3 V, esa ganancia lleva el diferencial logico a niveles cercanos a +-5 V por conductor:

- TX_A = 3,3 V y TX_B = 0 V -> LINE_A ~= +5 V, LINE_B ~= -5 V.
- TX_A = 0 V y TX_B = 3,3 V -> LINE_A ~= -5 V, LINE_B ~= +5 V.
- TX_A = 0 V y TX_B = 0 V -> LINE_A ~= 0 V, LINE_B ~= 0 V.

Para que esto funcione con margen, los operacionales deben poder alimentarse con una fuente bipolar, por ejemplo +-6 V o +-7 V, y deben tener velocidad suficiente para no deformar los pulsos de 100 kbps.

## Front-end RX recomendado

La Pico nunca debe recibir directamente una senal bipolar. El RX debe hacer tres cosas:

1. Proteger la entrada.
2. Reducir la senal diferencial a un rango seguro.
3. Reconstruir dos salidas logicas compatibles con GPIO de 3,3 V.

Una forma simple de pensarlo es generar una tension interna:

```text
Vsense = 1,65 V + 0,1 * (LINE_A - LINE_B)
```

Entonces:

| Diferencial A-B | Vsense aproximado | Estado |
| ---: | ---: | --- |
| +10 V | 2,65 V | HIGH |
| 0 V | 1,65 V | NULL |
| -10 V | 0,65 V | LOW |

Luego se usan dos comparadores:

- comparador alto: si Vsense > 2,30 V, genera RX_A = 1;
- comparador bajo: si Vsense < 1,00 V, genera RX_B = 1;
- si Vsense queda entre ambos umbrales, RX_A = 0 y RX_B = 0.

Esa salida reproduce el formato logico que ya entiende el firmware.

## Componentes recomendados

Los precios son rangos orientativos en pesos argentinos a julio de 2026. Deben verificarse al momento de compra, porque el stock local de integrados analogicos cambia mucho.

| Item | Funcion | Cantidad | Precio ARS aprox. |
| --- | --- | ---: | ---: |
| Raspberry Pi Pico o Pico H | Placa de prueba aislada del proyecto principal | 1 | 12.000 a 35.000 |
| MCP6562 | Comparador dual rapido para RX | 1 a 2 | 8.000 a 30.000 c/u |
| LM393B / LM393 | Comparador dual alternativo, mas lento y open collector | 1 a 2 | 800 a 4.000 c/u |
| TLV3702 | Comparador dual de muy bajo consumo, no recomendado para 100 kbps | 1 | 6.000 a 20.000 |
| Op-amp dual rapido, tipo TLV9352 / OPA197 / OPA2197 | Generacion bipolar TX y acondicionamiento RX | 2 a 3 | 8.000 a 35.000 c/u |
| ICL7660 / TC7660 | Generador simple de tension negativa para pruebas de baja corriente | 1 a 2 | 2.000 a 8.000 c/u |
| Modulo DC/DC +-5 V o +-6 V | Fuente bipolar mas prolija para el front-end | 1 | 8.000 a 35.000 |
| Resistencias 1 % | Ganancias, divisores, umbrales, pull-up | kit | 5.000 a 18.000 |
| Capacitores 100 nF, 1 uF, 10 uF | Desacople y estabilidad de fuentes | kit | 5.000 a 15.000 |
| Diodos TVS/ESD y Schottky | Proteccion de entrada RX | kit | 3.000 a 15.000 |
| Protoboard o placa perforada | Armado inicial | 1 a 2 | 6.000 a 20.000 c/u |
| Cables Dupont, pin headers, borneras | Conexionado | kit | 5.000 a 20.000 |

## Herramientas necesarias

| Herramienta | Uso | Precio ARS aprox. |
| --- | --- | ---: |
| Osciloscopio 2 canales o mas, con MATH CH1-CH2 | Validar A, B y diferencial | ya disponible / 600.000 a 1.800.000 si se compra |
| Puntas de osciloscopio x10 | Medicion de A y B respecto de GND | 20.000 a 80.000 |
| Multimetro | Verificacion de fuentes, continuidad y niveles DC | 20.000 a 120.000 |
| Fuente de banco dual o dos fuentes aisladas | Alimentar +-5 V o +-6 V y 3,3/5 V | 150.000 a 500.000 |
| Analizador logico USB | Verificacion digital en GP4/GP5 | 10.000 a 40.000 |
| Soldador, estanio, flux, pinzas | Armado estable fuera de protoboard | 30.000 a 150.000 |

## Presupuesto orientativo

- Prototipo minimo, usando instrumentos del laboratorio: 70.000 a 180.000 ARS.
- Prototipo recomendado, con mejores comparadores, op-amps y protecciones: 160.000 a 380.000 ARS.
- Si ademas se compran instrumentos de medicion, el costo total puede superar 800.000 ARS.

## Ensayos de aceptacion

1. Sin conectar la Pico RX, verificar con osciloscopio que LINE_A y LINE_B tomen +5 V, 0 V y -5 V respecto de masa.
2. Medir MATH = LINE_A - LINE_B y confirmar niveles cercanos a +10 V, 0 V y -10 V.
3. Validar 100 kbps: bit de 10 us, media celda activa de 5 us y retorno a cero.
4. Validar 12,5 kbps: bit de 80 us, media celda activa de 40 us y retorno a cero.
5. Conectar el front-end RX y confirmar que GP4/GP5 vuelven a entregar 0..3,3 V.
6. Reutilizar el decodificador actual para verificar label, SDI, SSM, dato y paridad.

## Decision tecnica

La ruta recomendada es implementar primero un front-end analogico de banco con op-amps y comparadores. Es mas controlable que intentar resolver todo con MOSFET discretos desde el primer dia, permite medir cada nodo con claridad y deja una base concreta para migrar despues a PCB.

Para el receptor, el comparador MCP6562 es una buena opcion tecnica por velocidad y salida push-pull. El LM393/LM393B puede servir como respaldo local si no se consigue otra cosa, pero requiere pull-up y tiene menos margen temporal. El TLV3702 no conviene para 100 kbps porque su retardo de propagacion es demasiado alto frente a una media celda de 5 us.

## Referencias abiertas

- Raspberry Pi Pico: https://www.raspberrypi.com/products/raspberry-pi-pico/
- TI LM393: https://www.ti.com/product/LM393
- TI TLV3702: https://www.ti.com/product/TLV3702
- Microchip MCP6562: https://www.microchip.com/en-us/product/MCP6562
- Resumen abierto de niveles ARINC 429: https://fr.wikipedia.org/wiki/ARINC_429
