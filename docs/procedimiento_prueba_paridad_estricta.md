# Procedimiento de prueba de paridad estricta

## Objetivo

Demostrar que RX y SNIFFER detectan una palabra con paridad incorrecta, pero no
la contabilizan como aceptada ni actualizan el snapshot con ella.

## Firmware

Compilar y cargar:

- TX:
  - `arinc_tx_arinc429_logic_parity_test`
- RX:
  - `arinc_rx_arinc429_logic_stream`
- SNIFFER:
  - `sniffer_arinc429_logic_pio_frame`

Antes de iniciar la medicion, la consola USB del sniffer debe mostrar:

```text
Build: SPI-PIOFRAME-ARINC429-STRICTPARITY-V2
```

Si muestra solamente `SPI-PIOFRAME-ARINC429`, el UF2 corresponde a la version
anterior y la prueba no valida el rechazo estricto.

El target TX de prueba invierte el bit 31 de la primera palabra de cada grupo
de 100. En el perfil baseline esa posicion corresponde a `TEMPERATURA`.

El target normal `arinc_tx_arinc429_logic_stream` no inyecta errores.

## Preparacion

1. Actualizar en la 3B+:
   - `sniffer_spi_protocol.py`
   - `spi_sniffer_bridge.py`
   - `stats_recorder.py`
2. Levantar el bridge en modo `pio-frame` a 8 MHz.
3. Levantar Flask si se desea observar el dashboard.
4. Encender RX, SNIFFER y finalmente TX parity-test.

## Registro

```bash
cd ~/PAMPA/HOST_3b+
source .venv/bin/activate

nohup python3 stats_recorder.py start \
  --name prueba_paridad_estricta \
  --duration 10m \
  --interval 2 \
  --expect-parity-errors \
  --tx-firmware arinc_tx_arinc429_logic_parity_test \
  --notes "Una paridad invertida cada 100 palabras TX" \
  > stats_recorder.log 2>&1 &
```

## Resultado esperado

- `received_words` crece con todo el trafico reconstruido.
- `parity_errors_sniffer` crece aproximadamente una vez cada 100 palabras
  recibidas.
- `accepted_words` no incluye las palabras corruptas.
- el snapshot de `TEMPERATURA` solo contiene palabras con `parity=OK`.
- `spi_errors = 0`.
- `overflow_events = 0`.
- `spi_drop_events = 0`.

El valor exacto observado dentro de la sesion puede variar levemente porque el
registrador comienza cuando detecta trafico y no necesariamente coincide con
el contador global del TX. La relacion debe ser aproximadamente:

```text
parity_errors_sniffer / received_words = 1 / 100
```

Ademas debe conservarse la clasificacion de las palabras:

```text
received_words ~= accepted_words
                + filtered_words
                + parity_errors_sniffer
                + overflow_events
```

La diferencia permitida queda acotada por las palabras pendientes en la cola
interna al tomar el baseline y la muestra final. El registrador usa una
tolerancia de 16384 palabras. Un valor clasificado muy superior a
`received_words` indica firmware anterior, contadores con semantica
incompatible o que las palabras con mala paridad siguen entrando en
`accepted_words`.

Con `--expect-parity-errors`, el informe:

- devuelve `FAIL` si no detecta ningun error de paridad;
- devuelve `FAIL` si los contadores no demuestran el rechazo estricto;
- permite los errores inyectados;
- sigue devolviendo `FAIL` ante errores SPI, overflow, drops o timeout.

## Cierre

Despues de la prueba, volver a cargar en TX:

```text
arinc_tx_arinc429_logic_stream
```

La corrida larga final debe realizarse con el target normal, sin inyeccion, y
debe terminar con `parity_errors_sniffer = 0`.
