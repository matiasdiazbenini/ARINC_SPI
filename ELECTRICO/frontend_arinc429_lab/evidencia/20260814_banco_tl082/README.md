# Evidencia de banco TL082 - 2026-08-14

## Montaje observado

- Raspberry Pi Pico TX con la variante de laboratorio `GP2=FWD_A` y
  `GP0=FWD_B`; `GP1` queda libre.
- Front-end TX con TL082CP alimentado en forma bipolar.
- Linea diferencial de banco medida con osciloscopio.
- Recuperacion logica con comparadores LM393 hacia niveles `0..3,3 V`.
- Sniffer, Raspberry Pi 3B+ y dashboard conectados para verificar la cadena
  completa.

## Resultado

La cadena completa funciono correctamente a `12,5 kbps`. Durante la corrida
observada no se registraron errores y se verificaron tanto la forma de onda
bipolar como la recuperacion de niveles logicos.

A `100 kbps`, los TL082CP ensayados mostraron rampas del orden de `8..12 us`,
equivalentes a aproximadamente `0,4 V/us`. Ese tiempo consume practicamente
todo el periodo de bit de `10 us`, por lo que no queda una meseta estable y la
recepcion no resulta confiable. A `12,5 kbps`, el periodo de bit es `80 us` y
la misma respuesta dinamica deja un margen util suficiente.

Este resultado valida el prototipo de laboratorio a baja velocidad y aisla la
limitacion de `100 kbps` en la respuesta dinamica de la etapa analogica
ensayada. No constituye una certificacion ARINC 429 de campo.

## Firmware TX utilizado

- `arinc_tx_arinc429_logic_stream_gp2_gp0`: variante de `100 kbps`.
- `arinc_tx_arinc429_logic_stream_12k5_gp2_gp0`: variante forzada a
  `12,5 kbps`.

Los targets originales con `GP2/GP3` permanecen sin cambios.

## Capturas

Las imagenes `TEK0000.JPG` a `TEK0008.JPG` conservan los nombres generados por
el osciloscopio. Incluyen vistas de la linea diferencial y de las senales
recuperadas durante los ensayos de ambas velocidades.
