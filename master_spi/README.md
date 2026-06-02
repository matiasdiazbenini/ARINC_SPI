# master_spi

Firmware del nodo master de la fase `arinc429_logic`.

## Rol

- genera palabras sobre el canal `FWD`
- arma batches de transmision
- intercala palabras utiles y trafico de relleno
- espera y valida `ACK` del slave por `REV`

## Cableado logico

- `GP2 / GP3`
  - transmision `FWD`
- `GP4 / GP5`
  - recepcion `REV`
- masa comun

## Estado dentro del repo

En la rama `ARINC`, este firmware fue movido a subcarpeta propia para que el repositorio represente al sistema completo y no solo al maestro.

## Build historico

Se conservaron UF2 versionados en:

- `master_spi/build/`

como referencia de esta etapa del proyecto.
