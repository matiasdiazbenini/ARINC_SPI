# Entrega al tutor - cierre de fase logica ARINC 429

Este directorio concentra el material recomendado para presentar el estado
validado del proyecto PAMPA / ARINC 429. La fase cerrada reproduce la logica y
temporizacion ARINC sobre GPIO, implementa captura pasiva, transporte SPI por
tramas completas y observabilidad en Raspberry Pi 3B+.

## Resultado ejecutivo

Configuracion final:

```text
ARINC-TX       arinc_tx_arinc429_logic_stream_autorate
ARINC-RX       arinc_rx_arinc429_logic_stream
ARINC-SNIFFER  sniffer_arinc429_logic_pio_frame
SPI            PIO-frame, 8 MHz, CS manual
Bridge         127.0.0.1:5100
Flask          0.0.0.0:5000
```

Validaciones principales:

| Prueba | Resultado |
| --- | --- |
| Enlace bipolar RZ logico | 100 kbps y 12.5 kbps |
| SPI por trama completa | PIO-frame estable a 8 MHz |
| Barrido SPI | 8 y 9 MHz PASS; 10 MHz parcial; 16 y 50 MHz FAIL |
| Paridad estricta | 1 error cada 100 palabras detectado y descartado |
| Corridas finales autorate | 8 horas a 100 kbps y 8 horas a 12.5 kbps, cero errores operativos |
| Stream aleatorio | Sin lotes fijos y sin ACK por REV |
| SNIFFER | Pasivo sobre FWD/REV; solo transmite hacia la 3B+ por SPI |

Corridas finales de ocho horas:

```text
12.5 kbps:
  received_words                = 9876638
  accepted_words                = 2962992
  filtered_words                = 6913646
  spi_errors                    = 0
  parity_errors_sniffer         = 0
  overflow_events               = 0
  spi_drop_events               = 0
  fwd_operational_resync_events = 0
  verdict                       = PASS

100 kbps:
  received_words                = 76707606
  accepted_words                = 23012283
  filtered_words                = 53695323
  spi_errors                    = 0
  parity_errors_sniffer         = 0
  overflow_events               = 0
  spi_drop_events               = 0
  fwd_operational_resync_events = 0
  verdict                       = PASS
```

## Informes incluidos

- [Guia didactica para explicar el sistema al tutor](../pdf/guia_didactica_tutor_arinc429.pdf)
- [Trabajo final en formato tesis](../pdf/trabajo_final_arinc429.pdf)
- [Fuente LaTeX del trabajo final](../tex/trabajo_final_arinc429.tex)
- [Informe descriptivo integral del sistema](../pdf/informe_descriptivo_sistema_arinc429.pdf)
- [Comandos de arranque y rescate](../pdf/comandos_servicios_3b_autoarranque.pdf)
- [Notas de version arinc-logic-v1.1.0](notas_version_arinc_logic_v1.1.0.md)
- [Notas de version arinc-logic-v1.0.0](notas_version_arinc_logic_v1.0.0.md)
- [Informe estadistico de corrida 8 h a 12.5 kbps](reportes/reporte_corrida8h_12k5.pdf)
- [Informe estadistico de corrida 8 h a 100 kbps](reportes/reporte_corrida8h_100k.pdf)
- [Resumen JSON de corrida 8 h a 12.5 kbps](datos/summary_corrida8h_12k5.json)
- [Resumen JSON de corrida 8 h a 100 kbps](datos/summary_corrida8h_100k.json)
- [Informe estadistico de la corrida final de 8 horas](reportes/reporte_corrida_final_stream_8mhz_8h.pdf)
- [Resumen JSON de la corrida final](datos/summary_corrida_final_stream_8mhz_8h.json)
- [Informe de la prueba de paridad estricta](reportes/reporte_prueba_paridad_estricta_5m.pdf)
- [Resumen JSON de la prueba de paridad](datos/summary_prueba_paridad_estricta_5m.json)
- [Preguntas y respuestas para el tutor](../pdf/preguntas_respuestas_tutor_arinc429.pdf)
- [Resumen general del proyecto](../pdf/resumen_proyecto_arinc429.pdf)
- [Instructivo y evidencia de osciloscopio](../pdf/instructivo_osciloscopio_arinc429_logic.pdf)
- [Evidencia de osciloscopio ARINC logico 2026-06-19](../pdf/evidencia_osciloscopio_arinc429_20260619.pdf)

Los CSV completos permanecen en `ARINC_RESULTS` fuera del repositorio. Se
conservan fuera de Git para evitar versionar cientos de miles de muestras. Los
JSON y PDF incluidos permiten auditar configuracion, contadores y veredicto.

## Evidencias tecnicas

- [Corrida final stream a 8 MHz](../evidencia_corrida_final_stream_8mhz_8h.md)
- [Corridas finales autorate de 8 h](../evidencia_corridas_finales_autorate_8h.md)
- [Autorate 100 kbps / 12.5 kbps](../evidencia_autorate_100k_12k5.md)
- [Prueba de paridad estricta](../evidencia_prueba_paridad_estricta_20260605.md)
- [Barrido SPI hasta 50 MHz](../evidencia_barrido_frecuencia_spi.md)
- [SPI PIO-frame post tutor](../evidencia_spi_pio_frame_post_tutor.md)
- [Stream aleatorio sin lote fijo](../evidencia_stream_aleatorio_post_tutor.md)
- [Errores SPI operativos en cero](../evidencia_spi_errors_cero_post_tutor.md)
- [Osciloscopio ARINC logico 2026-06-19](../evidencia_osciloscopio_arinc429_20260619.md)

## Diagramas internos

- [ARINC-TX](../diagramas_bloques/01_arinc_tx.md)
- [ARINC-RX](../diagramas_bloques/02_arinc_rx.md)
- [ARINC-SNIFFER](../diagramas_bloques/03_arinc_sniffer.md)
- [Raspberry Pi 3B+ y visualizacion](../diagramas_bloques/04_raspberry_pi_3bplus.md)

## Respuestas a las observaciones principales

### Red y comunicacion HTTP

- El bridge escucha en `127.0.0.1:5100`; no se expone a la red.
- Flask consulta al bridge mediante TCP/IP sobre loopback.
- Solo Flask se publica en `0.0.0.0:5000` para la notebook.
- Prometheus consulta `/metrics` de Flask por Ethernet.

### Transporte SPI

- La transaccion es una trama completa de 32 bytes.
- El slave SPI del SNIFFER esta implementado con PIO.
- El perfil operativo es 8 MHz.
- 50 MHz fue ensayado y fallo experimentalmente; no es un requisito de caudal
  para un flujo ARINC de 100 kbps.

### ARINC y canal REV

- Un canal ARINC real es simplex.
- En el modo stream actual no se requiere ACK ni transmision REV.
- REV queda como entrada pasiva del SNIFFER para observar un segundo canal
  fisico si existe.

### Integridad

- Las palabras con paridad incorrecta incrementan el contador de error.
- No ingresan a `accepted_words`.
- No actualizan el snapshot publicado.
- Los errores SPI de arranque se separan de los errores operativos.

## Limite de la fase

La validacion actual es logica y temporal, con niveles GPIO de 3,3 V. No
constituye todavia una interfaz electrica ARINC 429 de campo. La siguiente fase
requiere proteccion de linea, entrada de alta impedancia y un receptor ARINC
429 dedicado antes de la Pico SNIFFER.

## Demostracion

El orden recomendado para la presentacion practica esta en
[guion_demostracion.md](guion_demostracion.md).
