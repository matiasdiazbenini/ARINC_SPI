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

- `GP6`: transmision `FWD_A`
- `GP7`: transmision `FWD_B`
- `GP4 / GP5`
  - recepcion `REV`
- masa comun

Para el banco actual se deben usar los targets terminados en `gp6_gp7`.

## Estado dentro del repo

En la rama `ARINC`, este firmware fue movido a subcarpeta propia para que el repositorio represente al sistema completo y no solo al transmisor.

## Build historico

Se conservaron UF2 versionados en:

- `ARINC-TX/build/`

como referencia de esta etapa del proyecto.

## Targets principales

- `arinc_tx_arinc429_logic_stream_gp6_gp7`
  - firmware vigente a `100 kbps`
  - usa `GP6=FWD_A` y `GP7=FWD_B`
- `arinc_tx_arinc429_logic_stream_12k5_gp6_gp7`
  - firmware vigente a `12.5 kbps`
  - usa `GP6=FWD_A` y `GP7=FWD_B`

UF2 para cargar en la Pico TX:

- `build/arinc_tx_arinc429_logic_stream_gp6_gp7.uf2`: `100 kbps`.
- `build/arinc_tx_arinc429_logic_stream_12k5_gp6_gp7.uf2`: `12.5 kbps`.

Ambas variantes fueron verificadas en banco el 20 de agosto de 2026 con el
front-end analogico basado en TL062.
