# Procedimiento de registro estadistico ARINC

## Objetivo

`HOST_3b+/stats_recorder.py` registra una prueba sin acceder directamente al
SPI. Consulta `http://127.0.0.1:5100/stats`, por lo que el bridge sigue siendo
el unico propietario de `spidev0.0`.

Cada sesion genera:

```text
~/PAMPA/ARINC_RESULTS/
└── AAAAMMDD_HHMMSS_nombre/
    ├── metadata.json
    ├── samples.csv
    ├── events.csv
    ├── summary.json
    └── report.pdf
```

La carpeta de resultados queda fuera del repositorio para no agregar archivos
de medicion a Git.

## Inicio por duracion

Ejemplo de prueba de 12 horas:

```bash
cd ~/PAMPA/HOST_3b+
source .venv/bin/activate

nohup python3 stats_recorder.py start \
  --name stream_8mhz_12h \
  --duration 12h \
  --interval 5 \
  --notes "PIO-frame 8 MHz, TX/RX stream" \
  > stats_recorder.log 2>&1 &

echo $!
```

El proceso queda en segundo plano. Por defecto espera que el bridge responda
con `connected=true` y que `accepted_words` comience a crecer. Recien entonces
inicia el tiempo efectivo de la prueba.

## Inicio por cantidad de palabras

Ejemplo para finalizar al llegar a 20 millones de palabras aceptadas:

```bash
nohup python3 stats_recorder.py start \
  --name stream_8mhz_20m \
  --words 20000000 \
  --interval 5 \
  > stats_recorder.log 2>&1 &
```

Se pueden especificar simultaneamente `--duration` y `--words`. La prueba
finaliza cuando se cumple primero cualquiera de los dos objetivos.

## Prueba manual

Sin objetivo automatico:

```bash
nohup python3 stats_recorder.py start \
  --name prueba_manual \
  --interval 5 \
  > stats_recorder.log 2>&1 &
```

Consultar estado:

```bash
python3 stats_recorder.py status
```

Finalizar limpiamente:

```bash
python3 stats_recorder.py stop
```

`stop` envia `SIGTERM`. El registrador cierra los CSV, genera `summary.json` y
construye `report.pdf`.

## Desconexion

Una desconexion breve se registra en `events.csv` como `link_down` y `link_up`.
Por defecto, una desconexion continua de 30 segundos finaliza la prueba con:

```text
termination_reason = link_timeout
```

Puede modificarse con `--disconnect-timeout 60`.

El registrador no depende de detectar el apagado del sniffer para cerrar una
prueba normal. La duracion, la cantidad de palabras o `stop` son mecanismos
deterministas.

## Proteccion ante cortes

- `samples.csv` se vacia despues de cada muestra.
- Cada 12 muestras se fuerza `fsync` al almacenamiento.
- El periodo se cambia con `--fsync-every`.
- Si la Raspberry se apaga abruptamente, los CSV parciales permanecen.
- En la siguiente ejecucion, la sesion anterior se marca en `metadata.json`
  como `unclean_shutdown`.

## Contenido de los archivos

### `samples.csv`

Una fila cada cinco segundos por defecto:

- contadores recibidos, aceptados y filtrados;
- tasa de palabras por segundo;
- errores SPI, paridad, overflow y drops;
- resync FWD de arranque y operativos;
- temperatura, velocidad, altitud y SSM;
- estado de conexion.

Los contadores son relativos al inicio de la sesion. El script no necesita
reiniciar manualmente los contadores del dashboard.

### `events.csv`

Registra inicio, fin, perdida y recuperacion del bridge e incrementos de
contadores de error o resincronizacion.

### `summary.json`

Contiene configuracion, baseline, contadores finales, tasas, motivo de cierre
y resultado automatico.

### `report.pdf`

Incluye configuracion, resumen de contadores, veredicto, palabras acumuladas,
tasa de palabras y errores/resync operativos en el tiempo. El generador usa
solamente la biblioteca estandar de Python.

## Criterio automatico

`FAIL`:

- `spi_errors > 0`;
- `parity_errors_sniffer > 0`;
- `overflow_events > 0`;
- `spi_drop_events > 0`;
- timeout de arranque o de enlace.

`WARN`:

- no hay errores anteriores, pero existen resync FWD operativos.

`PASS`:

- sin errores operativos, paridad invalida, overflow, drops ni resync FWD
  operativos.

Los resync de arranque se informan, pero no cambian por si solos el resultado.

Para una prueba deliberada de paridad:

```bash
--expect-parity-errors
```

En ese modo se exige detectar al menos un error de paridad y esos errores
esperados no producen `FAIL`. Tambien se verifica que las palabras recibidas
sean coherentes con la suma de aceptadas, filtradas, rechazadas por paridad y
overflow. Si esa conservacion falla fuera de la tolerancia de la cola interna,
el resultado es `FAIL`. El procedimiento especifico esta en
`procedimiento_prueba_paridad_estricta.md`.

## Copia a la notebook

Desde PowerShell:

```powershell
scp -r mdiaz501@192.168.50.2:/home/mdiaz501/PAMPA/ARINC_RESULTS .
```

Para una sesion puntual:

```powershell
scp -r mdiaz501@192.168.50.2:/home/mdiaz501/PAMPA/ARINC_RESULTS/AAAAMMDD_HHMMSS_nombre .
```
