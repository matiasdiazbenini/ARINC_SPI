# ARINC-SNIFFER

Firmware del tercer nodo Pico encargado de observar el enlace entre `ARINC-TX` y `ARINC-RX` sin intervenir en la comunicacion principal.

## Rol

- escucha `FWD` en `GP2 / GP3`
- escucha `REV` en `GP4 / GP5`
- no transmite nunca sobre las lineas ARINC `FWD` ni `REV`
- filtra por `label + SDI`
- reconoce `ACK_BATCH`
- mantiene snapshot y estadisticas
- exporta al host por SPI
- descarta palabras con paridad incorrecta antes del filtro y snapshot
- separa resync FWD de arranque y operativos
- detecta de forma autonoma si el canal `FWD` trabaja a `100 kbps` o
  `12.5 kbps` y lo exporta por SPI al bridge

## Enlace SPI hacia la Raspberry Pi 3B+

- `GP16` = MOSI desde la Pi
- `GP17` = CSn desde la Pi
- `GP18` = SCK desde la Pi
- `GP19` = MISO hacia la Pi
- `GP20` = `DRDY` hacia Raspberry Pi GPIO25

En la build PIO-frame actual, `GP20` queda en modo `DRDY` puro:

- sube cuando el snapshot del sniffer recibe una palabra nueva aceptada
- baja cuando la 3B+ consulta `GET_LATEST_META`
- no mezcla heartbeat ni pulsos de diagnostico SPI

Esto permite que el bridge consulte el snapshot por demanda en vez de hacerlo
por polling continuo. El polling queda solo como respaldo por timeout.

## Limites conocidos

- snapshot:
  - `16` slots maximos
- whitelist SPI:
  - `10` entradas maximas

## Estado funcional

Esta version corresponde al estado validado en corridas largas, con:

- filtrado funcionando
- `ACK` detectado
- exportacion por SPI estable
- `filtered_words` corregido
- `received_words` cuenta todas las palabras extraidas del FIFO PIO
- `accepted_words` solo cuenta paridad correcta, filtro valido y dato plausible
- logs de rechazo logico desactivados por defecto para operacion normal

La build PIO-frame con rechazo estricto debe mostrar por USB:

```text
Build: SPI-PIOFRAME-ARINC429-STRICTPARITY-V2
```

La build con `DRDY` puro debe mostrar:

```text
Build: SPI-PIOFRAME-ARINC429-STRICTPARITY-DRDY-AUTORATE-RECOVERY-V5
```
