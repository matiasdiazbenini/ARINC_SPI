# Procedimiento de prueba autorate 100 kbps / 12.5 kbps

Objetivo: verificar que `ARINC-SNIFFER` detecta de forma autonoma la velocidad
del canal `FWD` sin que `ARINC-TX` ni `ARINC-RX` se la informen por software.

## Firmware a cargar

- TX:
  - `arinc_tx_arinc429_logic_stream_autorate.uf2`
  - alternativa para forzar velocidad baja: `arinc_tx_arinc429_logic_stream_12k5.uf2`
- RX:
  - `arinc_rx_arinc429_logic_stream.uf2`
- SNIFFER:
  - `sniffer_arinc429_logic_pio_frame.uf2`

La sniffer debe mostrar por USB:

```text
Build: SPI-PIOFRAME-ARINC429-STRICTPARITY-DRDY-AUTORATE-RECOVERY-V5
```

El TX autorate elige una velocidad al arrancar entre:

- `100000 bps`
- `12500 bps`

El RX stream no necesita recompilarse: su PIO recibe por flancos/nivel activo y
no por un muestreo fijo dependiente de la velocidad.

## Reinicio limpio en la Raspberry Pi 3B+

```bash
sudo systemctl restart arinc-sniffer-bridge arinc-dashboard-flask
```

Verificar configuracion activa:

```bash
PID="$(systemctl show -p MainPID --value arinc-sniffer-bridge)"
tr '\0' '\n' < "/proc/$PID/environ" | grep '^ARINC_SPI_'
```

Valores esperados:

```text
ARINC_SPI_TRANSFER_MODE=pio-frame
ARINC_SPI_REQUEST_MODE=drdy
ARINC_SPI_HZ=8000000
ARINC_SPI_MANUAL_CS=1
ARINC_SPI_DRDY_GPIO=25
```

## Verificacion por stats

Con las Pico encendidas:

```bash
curl http://127.0.0.1:5100/stats
```

Campos clave:

- `connected`: debe ser `true`
- `spi_errors`: debe mantenerse en `0`
- `detected_bit_rate_bps`: debe estabilizarse en `100000` o `12500`
- `detected_bit_rate_txt`: debe mostrar `100 kbps` o `12.5 kbps`
- `accepted_words`: debe incrementar
- `parity_errors_sniffer`: debe mantenerse en `0`

La deteccion usa una ventana de tasa de palabras, por lo que al arrancar puede
aparecer como `unknown` durante los primeros segundos.

## Confirmacion con osciloscopio

Si el TX eligio `100 kbps`:

- bit completo esperado: `10 us`
- media celda activa: `5 us`

Si el TX eligio `12.5 kbps`:

- bit completo esperado: `80 us`
- media celda activa: `40 us`

En ambos casos debe mantenerse el retorno a cero en la segunda mitad del bit.
