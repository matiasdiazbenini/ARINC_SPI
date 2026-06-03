# Resumen Corto: Sniffer Pico 2 WH <-> Raspberry Pi 3B+ por SPI

## Objetivo
Conectar la Pico `sniffer` a la `Raspberry Pi 3B+` por `SPI` para que la Pi reciba eventos filtrados y desde ahi alimente `Flask/Grafana`.

## Arquitectura buscada
- `master` Pico transmite tramas ARINC-like por `FWD`
- `slave` Pico recibe `FWD` y responde ACK por `REV`
- `sniffer` Pico escucha ambos canales:
  - `FWD`: `GP2/GP3`
  - `REV`: `GP4/GP5`
- Enlace buscado:
  - `Pi 3B+` = `SPI master`
  - `Pico sniffer` = `SPI slave`
  - `GP16=MOSI`, `GP17=CSn`, `GP18=SCK`, `GP19=MISO`, `GND comun`

## Lo que quedo comprobado
- El filtrado primario ARINC esta en la `Pico sniffer`, no en Flask.
- `master`, `slave` y `sniffer` funcionan bien del lado ARINC-like.
- Flask y el bridge HTTP en la `Pi 3B+` ya quedaron operativos.
- El cableado SPI principal y la continuidad GPIO quedaron validados.

## Pruebas importantes realizadas

| Prueba | Herramienta/Firmware | Resultado |
|---|---|---|
| Continuidad GPIO Pi->Pico | `gpio_spi_continuity_test.py` + `ARINC-SNIFFER_GPIO_TEST` | OK en `CS`, `MOSI`, `SCLK` |
| Continuidad GPIO Pico->Pi | `gpio_spi_continuity_test.py` + `ARINC-SNIFFER_GPIO_TEST` | OK en `MISO` y `GP20/GPIO25` |
| Heartbeat GP20 | `gpio25_probe.py` | OK, la Pi ve transiciones |
| Observador GPIO de SPI | `ARINC-SNIFFER_SPI_SCOPE` | `CS` unico, `256` clocks, `MOSI` correcto |
| SPI slave minimo por frame | `ARINC-SNIFFER_SPI_MIN` + `spi_slave_min_test.py` | La Pico ve solo `len=1`, la respuesta sale de a un byte por intento |
| Pico master -> Pico slave | `ARINC-SNIFFER_SPI_MASTER_MIN` | Reproduce el mismo patron, no es exclusivo de la 3B+ |
| SPI hardware byte a byte | `ARINC-SNIFFER_SPI_BYTE_SLAVE` + `spi_byte_proto_test.py` | OK, la firma `A4 29 53 50 49 4D 49 4E` sale completa |
| Sniffer real sobre transporte byte a byte | `ARINC-SNIFFER` + `spi_link_diagnostic.py` | No quedo estable aun; el bridge recibe `EE 00 00 ...` |

## Lectura tecnica actual
La evidencia ya no apunta a:
- cableado principal
- pinout
- Flask / bridge
- complejidad del ARINC-like en si

La evidencia si apunta a una limitacion practica del uso de:
- `Pico 2 WH` como `SPI slave hardware`
- cuando se quiere transportar un protocolo mas rico que un simple byte por transaccion

## Veredicto sobre la opcion 1
La opcion 1 **no esta descartada en teoria**, porque se logro una comunicacion SPI hardware **byte a byte** estable.

Pero la opcion 1 **ya no conviene como camino principal** para el sistema real, porque:
- el protocolo de frame completo no quedo estable
- la adaptacion del sniffer real al transporte byte a byte tampoco quedo cerrada
- seguir por ahi ya implica mas reingenieria que ajuste fino

## Opciones desde aca

### Opcion 1: seguir con SPI slave hardware de la Pico
Profundizar con:
- registros crudos del bloque SSI/SPI
- manejo mas bajo nivel
- mas reingenieria del protocolo

**Ventaja:** mantiene SPI hardware puro.  
**Desventaja:** mucho mas trabajo y cierre incierto.

### Opcion 2: implementar SPI slave por PIO
Mantener SPI, pero implementar el slave con `PIO`.

**Ventaja:** control total del framing, `CS`, `SCK`, `MOSI`, `MISO` y timing.  
**Desventaja:** mas desarrollo inicial.

### Opcion 3: cambiar la interfaz Pico <-> Pi
Ejemplo: `UART`.

**Ventaja:** probablemente mas rapido de estabilizar.  
**Desventaja:** se aleja del camino SPI pedido para esta etapa.

## Conclusion corta
Con lo obtenido hasta ahora, la recomendacion tecnica es:
- **cerrar la opcion 1 como exploracion suficientemente investigada**
- y pasar a **opcion 2** como siguiente paso principal si se quiere conservar SPI.
