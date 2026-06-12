# Procedimiento de barrido de frecuencia SPI

Fecha de referencia: 2026-06-06.

Este procedimiento caracteriza el enlace SPI entre `ARINC-SNIFFER` y la Raspberry Pi 3B+ usando el transporte `pio-frame`. El objetivo es determinar experimentalmente hasta que frecuencia funciona la implementacion actual y responder la solicitud de probar hasta `50 MHz`.

## Alcance

La prueba modifica solamente la frecuencia solicitada al controlador SPI de la 3B+.

No requiere:

- recompilar las Pico
- cambiar el firmware PIO-frame
- modificar el enlace ARINC de `100 kbps`
- reactivar ACK por `REV`

Configuracion recomendada durante el ensayo:

- TX: `arinc_tx_arinc429_logic_stream`
- RX: `arinc_rx_arinc429_logic_stream`
- Sniffer: `sniffer_arinc429_logic_pio_frame`
- las tres Pico encendidas y transmitiendo normalmente
- bridge detenido para que un solo proceso use `/dev/spidev0.0`

## Por que se detiene el bridge

El runner accede directamente a SPI. Si el bridge permanece activo, ambos procesos pueden intercalar transacciones y producir errores artificiales. Flask puede quedar levantado, aunque durante el barrido no recibira datos nuevos.

## Preparacion en la 3B+

Actualizar primero `HOST_3b+/spi_frequency_sweep.py` y los archivos auxiliares desde el repo.

Luego:

```bash
cd ~/PAMPA/HOST_3b+
source .venv/bin/activate

pkill -f '[s]pi_sniffer_bridge.py'
pgrep -af spi_sniffer_bridge.py
```

El ultimo comando no debe mostrar ningun proceso del bridge.

## Barrido estricto

```bash
python3 spi_frequency_sweep.py \
  --hz-list 100000,200000,400000,800000,1000000,2000000,4000000,8000000,16000000,32000000,50000000 \
  --attempts 24 \
  --warmup-attempts 3 \
  --retries 1 \
  --delay-ms 2 \
  --manual-cs \
  --cs-setup-us 100 \
  --cs-hold-us 100
```

El runner:

- comprueba que el bridge no siga escuchando en `127.0.0.1:5100`
- prueba las frecuencias en orden ascendente
- usa transacciones completas de `32 bytes`
- no oculta fallos mediante reintentos
- verifica `PING` y `GET_STATS`
- guarda un reporte JSON con fecha y hora
- se detiene en el primer `PARTIAL` o `FAIL` para no contaminar puntos posteriores

## Interpretacion

Un punto se clasifica como:

- `PASS`
  - `24/24` PING correctos
  - `GET_STATS` correcto
- `PARTIAL`
  - al menos un PING correcto, pero no todos, o `GET_STATS` fallo
- `FAIL`
  - ningun PING correcto

El reporte entrega:

- mayor frecuencia que obtuvo `PASS`
- techo continuo de frecuencias `PASS` antes del primer fallo

La frecuencia recomendada no se elige solo por un `PASS` aislado. Debe existir margen respecto del primer punto inestable y luego superar una corrida real del bridge.

## Limitacion importante

Los valores son frecuencias solicitadas a `spidev`. El divisor interno del controlador de la Raspberry Pi puede redondear el clock efectivo. Para afirmar la frecuencia fisica exacta hay que medir `SCK` con osciloscopio o analizador logico.

El PIO actual detecta los flancos externos de `SCK` mediante instrucciones `wait` y trabaja con `clkdiv=1`. Por eso, `50 MHz` es una prueba de caracterizacion, no una capacidad garantizada por el diseño actual.

Si el PIO pierde un flanco dentro de una trama, puede quedar esperando clocks de la transaccion anterior. Por eso no se considera valido continuar automaticamente con frecuencias mayores despues de un fallo.

## Prueba aislada solicitada de 50 MHz

Si el barrido se detiene antes de llegar a `50 MHz`:

1. apagar y volver a encender solamente la Pico sniffer
2. esperar a que muestre el build tag `SPI-PIOFRAME-ARINC429`
3. ejecutar el punto de `50 MHz` de manera aislada

```bash
python3 spi_frequency_sweep.py \
  --hz-list 50000000 \
  --attempts 24 \
  --warmup-attempts 3 \
  --retries 1 \
  --delay-ms 2 \
  --manual-cs \
  --cs-setup-us 100 \
  --cs-hold-us 100
```

Si este punto falla, reiniciar nuevamente la sniffer antes de volver al bridge operativo. El fallo sigue siendo un resultado util: demuestra experimentalmente que la implementacion PIO actual no soporta de manera confiable esa frecuencia.

## Despues del barrido

No iniciar inmediatamente una prueba larga a la frecuencia maxima. Primero:

1. revisar el JSON
2. identificar el techo `PASS` continuo
3. seleccionar una frecuencia candidata con margen
4. levantar el bridge a esa frecuencia
5. hacer una corrida corta
6. solo si permanece en cero errores, iniciar una corrida prolongada

El bridge ahora expone en `/stats`:

- `spi_requested_hz`
- `spi_manual_cs`
- `spi_cs_gpio`
- `spi_cs_setup_us`
- `spi_cs_hold_us`

Esto permite conservar evidencia de la configuracion usada durante la corrida.

## Corrida candidata a 8 MHz

El refinamiento experimental dio:

```text
8 MHz  = PASS
9 MHz  = PASS
10 MHz = PARTIAL
```

Por margen se selecciona `8 MHz` para la primera corrida operativa.

Antes de iniciarla:

1. reiniciar la Pico sniffer, porque el ultimo punto inestable puede dejar el PIO desalineado
2. encender/verificar TX y RX en modo stream
3. confirmar que no quede otro bridge activo

Comando:

```bash
cd ~/PAMPA/HOST_3b+
source .venv/bin/activate

pkill -f '[s]pi_sniffer_bridge.py'

nohup env \
  ARINC_SPI_TRANSFER_MODE=pio-frame \
  ARINC_SPI_MANUAL_CS=1 \
  ARINC_SPI_CS_SETUP_US=100 \
  ARINC_SPI_CS_HOLD_US=100 \
  ARINC_SPI_HZ=8000000 \
  ARINC_SPI_BYTE_DELAY_US=0 \
  ARINC_SPI_FRAME_RESPONSE_DELAY_SEC=0.002 \
  ARINC_SPI_POLL_SEC=0.006 \
  ARINC_SPI_STATS_SEC=0.06 \
  python3 spi_sniffer_bridge.py > bridge_8mhz.log 2>&1 &
```

Verificacion:

```bash
sleep 5
curl http://127.0.0.1:5100/stats
tail -n 40 bridge_8mhz.log
```

La respuesta debe incluir:

```text
spi_requested_hz = 8000000
spi_transfer_mode = pio-frame
spi_manual_cs = true
```

Primera etapa:

- `15` minutos
- palabras A5/B1/C2 incrementando
- `spi_errors = 0`
- `spi_drop_events = 0`
- `overflow_events = 0`
- `parity_errors_sniffer = 0`

Si pasa, extender a varias horas sin reiniciar contadores.
