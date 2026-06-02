# slave_spi

Firmware del nodo slave de la fase `arinc429_logic`.

## Rol

- recibe palabras del master en `FWD`
- valida integridad logica
- reconoce batches
- emite `ACK` por `REV`

## Cableado logico

- `GP2 / GP3`
  - recepcion `FWD`
- `GP4 / GP5`
  - transmision `REV`
- masa comun

## Estado dentro del proyecto

Este snapshot corresponde al estado usado durante la validacion funcional de:

- enlace master/slave
- integracion con sniffer
- integracion con 3B+ via SPI en la sniffer

## Nota

Esta carpeta fue incorporada a este repo para centralizar el proyecto completo, pero el layout historico original del workspace mantenia este firmware en una carpeta hermana separada.
