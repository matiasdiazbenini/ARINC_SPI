# ARINC_SNIFFER

Firmware del tercer nodo Pico encargado de observar el enlace entre `master_spi` y `slave_spi` sin intervenir en la comunicacion principal.

## Rol

- escucha `FWD` en `GP2 / GP3`
- escucha `REV` en `GP4 / GP5`
- filtra por `label + SDI`
- reconoce `ACK_BATCH`
- mantiene snapshot y estadisticas
- exporta al host por SPI

## Enlace SPI hacia la Raspberry Pi 3B+

- `GP16` = MOSI desde la Pi
- `GP17` = CSn desde la Pi
- `GP18` = SCK desde la Pi
- `GP19` = MISO hacia la Pi
- `GP20` = `DRDY` opcional

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
- logs de rechazo logico desactivados por defecto para operacion normal
