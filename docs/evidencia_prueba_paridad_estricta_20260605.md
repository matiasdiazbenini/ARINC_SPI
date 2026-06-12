# Evidencia de prueba de paridad estricta - 2026-06-05

## Resultado original

Sesion:

```text
20260605_165302_prueba_paridad_15m
```

Configuracion:

- duracion: 900.123 s
- SPI: PIO-frame a 8 MHz
- TX declarado: `arinc_tx_arinc429_logic_parity_test`
- RX declarado: `arinc_rx_arinc429_logic_stream`
- sniffer declarado: `sniffer_arinc429_logic_pio_frame`

Contadores relativos:

```text
received_words          = 547722
accepted_words          = 547722
filtered_words          = 1278020
parity_errors_sniffer   = 18258
spi_errors              = 0
overflow_events         = 0
spi_drop_events         = 0
fwd_operational_resync  = 0
```

El registrador original emitio `PASS` porque detecto errores de paridad y no
observo errores SPI, overflow ni drops.

## Analisis

La deteccion de paridad funciono y el transporte SPI fue estable. Sin embargo,
la prueba no demuestra rechazo estricto:

```text
accepted_words == received_words
```

La clasificacion reconstruida es:

```text
accepted + filtered + parity + overflow
= 547722 + 1278020 + 18258 + 0
= 1844000
```

Por lo tanto:

```text
received - classified = 547722 - 1844000 = -1296278
```

La diferencia excede ampliamente cualquier cantidad pendiente en la cola de
captura. Los datos son compatibles con una version anterior donde
`received_words` y `accepted_words` contabilizaban el mismo conjunto y los
errores de paridad se registraban sin retirarlos de las palabras aceptadas.

La tasa de inyeccion observada sobre la clasificacion reconstruida fue:

```text
18258 / 1844000 = 0.9901 %
```

Esto confirma aproximadamente una inyeccion cada 100 palabras, pero no el
descarte antes de capas superiores.

## Conclusion de la primera corrida

- transporte SPI PIO-frame a 8 MHz: validado
- deteccion de errores de paridad: validada
- ausencia de overflow, drops y resync operativos: validada
- rechazo estricto antes de `accepted_words` y snapshot: no validado

Se debe recompilar y cargar el target actual
`sniffer_arinc429_logic_pio_frame`, actualizar `stats_recorder.py` en la 3B+ y
repetir la prueba corta.

## Repeticion con firmware estricto

Sesion:

```text
20260606_015741_prueba_paridad_estricta_5m
```

La repeticion utilizo la build identificada como:

```text
SPI-PIOFRAME-ARINC429-STRICTPARITY-V2
```

Resultados relativos durante 300.061 s:

```text
received_words              = 610905
accepted_words              = 177163
filtered_words              = 427633
parity_errors_sniffer       = 6109
overflow_events             = 0
spi_errors                  = 0
spi_startup_errors          = 0
spi_drop_events             = 0
fwd_operational_resync      = 0
```

La conservacion fue exacta:

```text
177163 + 427633 + 6109 + 0 = 610905
received - classified = 0
```

Distribucion:

```text
accepted_words / received_words        = 29.00009 %
filtered_words / received_words        = 69.99992 %
parity_errors / received_words         = 0.99999 %
```

El 1 % rechazado coincide con la inyeccion programada de una palabra con
paridad invertida cada 100 palabras. El 29 % aceptado tambien coincide con el
perfil de diez palabras: tres labels validos, menos la palabra corrupta que
aparece cada cien transmisiones.

El registrador produjo:

```text
verdict = PASS
counter_balance_words = 0
```

## Conclusion final

- deteccion de paridad incorrecta: validada
- descarte antes de `accepted_words`: validado
- snapshot limitado a palabras con paridad correcta: validado por arquitectura
  y por los registros publicados con `parity=OK`
- transporte SPI PIO-frame a 8 MHz durante la prueba: sin errores
- overflow, drops y resync FWD operativos: cero

La aceptacion estricta por paridad queda validada experimentalmente.
