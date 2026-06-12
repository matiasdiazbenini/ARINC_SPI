# ARINC-TX

Firmware del nodo transmisor de la fase `arinc429_logic`.

## Rol

- genera palabras sobre el canal `FWD`
- arma batches de transmision en modo laboratorio
- puede transmitir en modo stream aleatorio sin depender de batches fijos
- intercala palabras utiles y trafico de relleno
- en modo laboratorio espera y valida `ACK` del slave por `REV`
- en modo stream no espera ACK; solamente monitorea `REV` si aparece actividad

## Cableado logico

- `GP2 / GP3`
  - transmision `FWD`
- `GP4 / GP5`
  - recepcion `REV`
- masa comun

## Estado dentro del repo

En la rama `ARINC`, este firmware fue movido a subcarpeta propia para que el repositorio represente al sistema completo y no solo al transmisor.

## Build historico

Se conservaron UF2 versionados en:

- `ARINC-TX/build/`

como referencia de esta etapa del proyecto.

## Targets principales

- `arinc_tx_arinc429_logic`
  - modo laboratorio validado
  - transmite batches de `1000` palabras y espera ACK por `REV`
- `arinc_tx_arinc429_logic_stream`
  - modo post tutor para prueba de flujo
  - transmite rafagas de longitud variable
  - no bloquea esperando ACK
  - mantiene la codificacion `arinc429_logic` a `100 kbps`
- `arinc_tx_arinc429_logic_parity_test`
  - usa el mismo modo stream
  - invierte la paridad de una palabra cada `100`
  - sirve solamente para validar rechazo estricto en RX y SNIFFER
  - no debe usarse en la corrida larga normal
