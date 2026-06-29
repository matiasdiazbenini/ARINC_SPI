# Evidencia de osciloscopio autorate - 2026-06-29

## Objetivo

Registrar la validacion instrumental final de la primera parte logica del
proyecto, con el enlace ARINC recreado operando en las dos velocidades
soportadas:

- `100 kbps`
- `12.5 kbps`

Las mediciones corresponden a niveles GPIO de laboratorio, no a niveles
electricos ARINC 429 reales de campo. El objetivo es confirmar temporizacion,
retorno a cero, longitud de palabra y separacion visible entre palabras.

## Capturas seleccionadas

| Captura | Configuracion | Lectura principal |
|---|---|---|
| `TEK0014_100k_palabra_320us.JPG` | 100 kbps, MATH | palabra completa de `320 us` |
| `TEK0015_100k_palabra_gap_364us.JPG` | 100 kbps, MATH | palabra mas gap de `364 us` |
| `TEK0016_100k_gap_44us.JPG` | 100 kbps, MATH | gap visible de `44 us` |
| `TEK0011_12k5_media_celda_40us.JPG` | 12.5 kbps, canales | media celda activa de `40 us` |
| `TEK0012_12k5_bit_80us.JPG` | 12.5 kbps, canales | bit completo de `80 us` |
| `TEK0009_12k5_gap_360us.JPG` | 12.5 kbps, MATH | gap visible de `360 us` |

Carpeta de imagenes:

```text
imagenes/validacion_2026-06-29/
```

## Medicion a 100 kbps

A `100 kbps`, cada bit ocupa:

```text
1 / 100000 = 10 us
```

Como la codificacion implementada usa retorno a cero, cada bit queda dividido
en:

```text
5 us activo + 5 us NULL = 10 us
```

La palabra ARINC completa contiene 32 bits:

```text
32 bits * 10 us = 320 us
```

La captura `TEK0014_100k_palabra_320us.JPG` confirma esa duracion de palabra.
La captura `TEK0015_100k_palabra_gap_364us.JPG` mide palabra mas reposo entre
palabras:

```text
palabra + gap = 364 us
```

Por diferencia, la captura `TEK0016_100k_gap_44us.JPG` deja registrado el gap
visible entre actividad de palabras:

```text
364 us - 320 us = 44 us
```

Ese valor es coherente con el gap nominal minimo de 4 tiempos de bit:

```text
4 bits * 10 us = 40 us
```

La diferencia de algunos microsegundos es aceptable para una medicion manual
con cursores y para la forma discreta en que se observa el comienzo y fin de
actividad en pantalla.

## Medicion a 12.5 kbps

A `12.5 kbps`, cada bit ocupa:

```text
1 / 12500 = 80 us
```

Con retorno a cero:

```text
40 us activo + 40 us NULL = 80 us
```

Esto queda confirmado por:

- `TEK0011_12k5_media_celda_40us.JPG`
- `TEK0012_12k5_bit_80us.JPG`

La palabra ARINC completa ocupa:

```text
32 bits * 80 us = 2560 us = 2.56 ms
```

La captura `TEK0009_12k5_gap_360us.JPG` registra un gap visible entre actividad
de palabras de aproximadamente:

```text
360 us
```

## Comparacion entre velocidades

La relacion teorica entre `100 kbps` y `12.5 kbps` es:

```text
100000 / 12500 = 8
```

Las mediciones de gap visible dan:

```text
360 us / 44 us = 8.18
```

La relacion observada queda muy cerca de la relacion esperada de `8:1`. La
diferencia surge de la colocacion manual de cursores, redondeo de lectura y del
hecho de que se esta midiendo el gap visible entre actividad de palabras, no
una marca digital interna exportada por el firmware.

## Lectura de la senal diferencial

En las capturas con MATH, el osciloscopio reconstruye la diferencia entre los
dos hilos del par logico:

```text
Vdiff = CH1 - CH2
```

o, segun configuracion:

```text
Vdiff = CH2 - CH1
```

Por eso se observan tres estados:

- pulso positivo: una polaridad logica;
- pulso negativo: polaridad opuesta;
- cero: estado NULL o retorno a cero.

El valor diferencial observado se mantiene alrededor de `+3.3 V`, `0 V` y
`-3.3 V`, coherente con la recreacion logica sobre GPIO.

## Conclusion

Las capturas del 2026-06-29 cierran la validacion instrumental de la primera
parte logica:

- a `100 kbps` se confirma palabra de `320 us` y gap visible de `44 us`;
- a `12.5 kbps` se confirma media celda de `40 us`, bit de `80 us` y gap
  visible de `360 us`;
- la relacion temporal observada entre ambas velocidades es aproximadamente
  `8:1`;
- la forma de onda mantiene retorno a cero y comportamiento bipolar logico.

Esta evidencia complementa las corridas largas de 8 horas y confirma que la
deteccion autorate no solo queda reflejada por software, sino tambien por
medicion directa en el osciloscopio.
