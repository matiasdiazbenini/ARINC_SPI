# ARINC-RX

Firmware del nodo receptor de la fase `arinc429_logic`.

## Rol

- recibe palabras del transmisor en `FWD`
- valida integridad logica
- reconoce batches y emite `ACK` por `REV` en modo laboratorio
- puede operar en modo stream, procesando palabras a medida que llegan sin esperar un lote fijo
- en modo stream sin ACK deja `GP4/GP5` sin emision ARINC
- descarta palabras con paridad incorrecta antes de incrementar
  `accepted_words`
- en modo batch no emite ACK si el lote contiene una palabra con paridad
  incorrecta

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

## Targets principales

- `arinc_rx_arinc429_logic`
  - modo laboratorio validado
  - espera bloques de `1000` palabras utiles y responde ACK por `REV`
- `arinc_rx_arinc429_logic_stream`
  - modo post tutor para prueba de flujo
  - procesa palabras sin depender de bloques fijos
  - no transmite ACK por `REV`
  - mantiene `GP4/GP5` como entradas con pull-down
