# Guion breve de demostracion

## Preparacion

Firmware:

```text
TX       arinc_tx_arinc429_logic_stream
RX       arinc_rx_arinc429_logic_stream
SNIFFER  sniffer_arinc429_logic_pio_frame
```

La consola USB del SNIFFER debe identificar:

```text
Build: SPI-PIOFRAME-ARINC429-STRICTPARITY-V2
```

Servicios:

- bridge local en `127.0.0.1:5100`
- Flask en `192.168.50.2:5000`
- Prometheus en la notebook

## Secuencia sugerida

1. Mostrar el diagrama general y explicar que FWD es simplex.
2. Mostrar que TX transmite rafagas variables y RX no espera lotes de 1000.
3. Aclarar que el SNIFFER nunca transmite sobre FWD ni REV.
4. Abrir el dashboard y verificar que temperatura, velocidad y altitud cambian.
5. Ejecutar en la 3B+:

```bash
curl http://127.0.0.1:5100/stats
```

6. Señalar:
   - `spi_transfer_mode = pio-frame`
   - `spi_requested_hz = 8000000`
   - `spi_errors = 0`
   - `overflow_events = 0`
   - `spi_drop_events = 0`
7. Mostrar el informe de ocho horas.
8. Mostrar el informe de paridad estricta y la igualdad:

```text
received = accepted + filtered + parity + overflow
```

9. Mostrar el barrido SPI y explicar por que 8 MHz es el perfil recomendado:
   - 9 MHz paso la prueba corta;
   - 10 MHz fue parcial;
   - 16 y 50 MHz fallaron;
   - 8 MHz conserva margen y paso ocho horas.
10. Cerrar indicando el limite actual: falta la interfaz electrica ARINC real.

## Respuestas cortas

**¿Por que el bridge no tiene IP publica?**

Porque solo Flask necesita ser visible. Bridge y Flask se comunican por TCP
sobre `127.0.0.1`.

**¿SPI compite con Flask?**

El bridge es el unico propietario de SPI. Flask consume datos ya publicados
por HTTP y no accede a `spidev`.

**¿Por que no usar 50 MHz?**

Fue probado y no funciona con la implementacion PIO y el montaje actual. El
caudal requerido es mucho menor y 8 MHz posee evidencia prolongada sin errores.

**¿Las palabras con mala paridad llegan al dashboard?**

No. Se contabilizan como error, pero no se aceptan ni actualizan el snapshot.

**¿Por que no hay ACK en REV?**

Porque ARINC 429 es simplex y el modo final genera un flujo continuo
independiente de respuestas. REV se conserva como posible segundo canal de
escucha.
