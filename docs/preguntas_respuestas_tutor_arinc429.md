# Preguntas y respuestas para defensa tecnica

Proyecto PAMPA / ARINC 429 logico-temporal

Fecha de preparacion: 29 de junio de 2026

Este documento reune veinte preguntas posibles de un tutor o evaluador tecnico sobre el estado actual del proyecto. Las respuestas estan pensadas para una explicacion oral: tecnicas, directas y honestas sobre lo que ya fue validado y lo que todavia pertenece a una fase futura.

## 1. Cual es el objetivo concreto del proyecto hasta esta fase?

El objetivo fue construir y validar una arquitectura de laboratorio inspirada en ARINC 429, separando la generacion, recepcion, sniffing, transporte hacia host y visualizacion. En la fase actual no se declara cumplimiento electrico ARINC 429 real; se declara una recreacion logica y temporal a 100 kbps y 12.5 kbps con codificacion bipolar logica con retorno a cero, captura pasiva, bridge SPI/HTTP y dashboard operativo.

La importancia del resultado es que el sistema completo ya funciona como cadena extremo a extremo: ARINC-TX transmite, ARINC-RX recibe y responde, ARINC-SNIFFER observa ambas direcciones, la Raspberry Pi 3B+ publica el estado y la notebook visualiza y guarda historico con Prometheus/Grafana.

## 2. Que significa que la senal sea bipolar con retorno a cero?

En ARINC 429 real, la informacion se transmite con tres estados electricos diferenciales: positivo, negativo y nulo. En este proyecto se recreo esa idea en forma logica usando dos pines por canal. Para un bit alto se activa una linea, para un bit bajo se activa la otra, y luego ambas vuelven a cero durante la segunda mitad del bit.

Por eso se habla de retorno a cero: cada bit tiene una mitad activa y una mitad en estado nulo. A 100 kbps, cada bit dura 10 us: 5 us activos y 5 us en nulo. A 12.5 kbps, cada bit dura 80 us: 40 us activos y 40 us en nulo.

## 3. Esto ya es ARINC 429 real o todavia es una emulacion?

Es una recreacion logica/temporal de ARINC 429, no una interfaz electrica ARINC 429 real. Lo validado es la forma temporal, la semantica de palabras, labels, SDI, SSM, paridad, ACK y observabilidad del sistema.

Lo que falta para hablar de interfaz ARINC 429 real es la capa electrica: drivers o transceptores adecuados, niveles de linea, protecciones, adaptacion de impedancias, conectores, criterios de aislamiento si correspondiera y validacion instrumental sobre esa nueva etapa. Esa es una fase futura, no una limitacion del cierre actual.

## 4. Por que los canales se llaman FWD y REV?

FWD significa forward y representa el sentido principal de comunicacion, desde ARINC-TX hacia ARINC-RX. REV significa reverse y representa el sentido inverso, usado por ARINC-RX para enviar ACK hacia ARINC-TX.

El nombre no intenta copiar una nomenclatura oficial de ARINC 429, sino describir la topologia logica del laboratorio. Es util porque evita ambiguedad: FWD es el flujo de datos principal y REV es el retorno de control o confirmacion.

## 5. Por que se uso un ACK si ARINC 429 normalmente es un bus unidireccional?

El ACK no busca copiar literalmente ARINC 429 avionico clasico. Se agrego como mecanismo de laboratorio para cerrar el lazo funcional, medir recepcion por lotes y validar el canal inverso. En otras palabras, permite comprobar que el receptor no solo recibe palabras, sino que procesa lotes y responde de manera observable.

En la arquitectura queda separado: FWD transporta datos utiles y REV transporta confirmacion. Esto ayudo a validar recepcion, sincronizacion, sniffing bidireccional y robustez de la visualizacion.

## 6. Que valida exactamente el osciloscopio?

El osciloscopio valida que la forma temporal generada por las Pico coincide con lo esperado para la fase logica: pulsos de 5 us de media celda activa a 100 kbps, bit time de 10 us, palabra completa de 320 us y retorno a cero. En modo 12.5 kbps se midieron 40 us de media celda activa y 80 us de bit completo.

Tambien se valido la reconstruccion diferencial conceptual mediante MATH = CH1 - CH2, observando tres estados: positivo, cero y negativo. La medicion final registro gap visible de 44 us a 100 kbps y 360 us a 12.5 kbps; la relacion 360/44 = 8.18 es coherente con la relacion teorica 8:1.

## 7. Por que se eligio 100 kbps?

100 kbps es una velocidad clasica asociada a ARINC 429 de alta velocidad y permite una referencia clara para la validacion temporal. A esa tasa, el bit time es 10 us, una escala que se puede medir con osciloscopio y que es razonable para generar con PIO en Raspberry Pi Pico. Tambien se agrego soporte para 12.5 kbps, que corresponde a baja velocidad ARINC y permite probar autorate.

El TX puede elegir la velocidad al arrancar y el sniffer la detecta sin que TX o RX se la informen por software. Eso demuestra que la captura no depende de una constante fija cargada manualmente en el host.

## 8. Que hace cada placa Pico en la arquitectura actual?

ARINC-TX genera las palabras y las transmite por el canal FWD. ARINC-RX escucha FWD, decodifica las palabras y emite ACK por el canal REV. ARINC-SNIFFER observa pasivamente ambos canales, filtra por label y SDI, mantiene snapshots y estadisticas, y expone esa informacion a la Raspberry Pi 3B+ por SPI.

La separacion en tres nodos es importante porque reproduce roles de sistema: emisor, receptor y observador pasivo. Esto hace que el sniffer no sea parte activa del enlace TX/RX y pueda evaluarse como herramienta de monitoreo.

## 9. Por que el sniffer filtra por label y SDI?

ARINC 429 transporta informacion identificada principalmente por label, y el SDI permite separar fuentes o destinos logicos. Filtrar por label + SDI permite que la Pico sniffer reduzca la informacion a variables utiles antes de enviarla al host.

Esto evita que la Raspberry Pi 3B+ y el dashboard tengan que procesar todo el flujo crudo. El filtrado en la Pico baja carga, simplifica el bridge y hace mas estable la visualizacion.

## 10. Hay algun cuello de botella actual?

El cuello principal no esta en Flask ni en el dashboard. Lo que mas condicionaba el sistema era el intercambio sniffer -> 3B+ por SPI y el patron de polling del bridge. Esa parte se mejoro usando SPI `pio-frame`, tramas completas de 32 bytes, `DRDY` como aviso de datos nuevos y un supervisor de recuperacion.

El perfil final recomendado usa SPI a 8 MHz, CS manual, `DRDY` y fallback por timeout. En corridas de ocho horas a 100 kbps y 12.5 kbps cerro con cero errores SPI operativos, cero errores de paridad, cero overflow y cero drops.

## 11. Si TX y RX trabajan a 100 kbps, por que SPI puede ir a 800 kHz?

Son enlaces distintos y cumplen funciones distintas. El enlace TX/RX a 100 kbps o 12.5 kbps representa la comunicacion logica tipo ARINC. El enlace SPI entre sniffer y 3B+ no retransmite la forma de onda bit a bit; transporta snapshots, contadores y datos filtrados.

SPI necesita margen para consultar al sniffer, leer estructuras, actualizar estadisticas y responder al dashboard sin atrasarse. Por eso trabaja a 8 MHz aunque el enlace observado sea mucho mas lento. No hay contradiccion: ARINC es el enlace observado y SPI es el canal de telemetria del observador.

## 12. Por que no usar siempre el perfil SPI de stress a 1.2 MHz?

Porque mas rapido no significo automaticamente mejor. Se hizo un barrido hasta 50 MHz: 8 y 9 MHz pasaron, 10 MHz fue parcial, y 16/50 MHz fallaron. El perfil elegido fue 8 MHz porque dio margen operativo real y cero errores en corridas largas.

El criterio de ingenieria elegido fue estabilidad operativa, no velocidad maxima. Para una demostracion y para uso continuo conviene un sistema que mantenga conectividad, sin overflow, sin drops y con errores despreciables.

## 13. SPI es la mejor opcion entre sniffer y 3B+? Que pasa con UART o I2C?

SPI es una buena opcion porque tiene buena velocidad, baja latencia y una separacion clara entre master y slave. En la Pico como SPI slave hardware aparecieron desafios practicos de framing, tiempos y lectura estable. Por eso se migro el slave SPI del sniffer a PIO y a transacciones completas de 32 bytes.

UART podria ser mas simple para una version basica, pero SPI permite mas margen y un protocolo host-sniffer mas estructurado. I2C no es conveniente para esta etapa por overhead, pull-ups, direccionamiento y posibles esperas. La solucion final actual es SPI por PIO, que da control de `CS`, `SCK`, `MOSI` y `MISO`.

## 14. Por que Ethernet directo mejoro la estabilidad frente a Wi-Fi?

La Raspberry Pi 3B+ por Wi-Fi podia introducir jitter, carga variable o condiciones fisicas que afectaban indirectamente la estabilidad del sniffer y del polling. Al usar Ethernet directo entre notebook y 3B+, el trafico operativo queda mas predecible y separado de la red Wi-Fi.

La evidencia practica fue clara: con Ethernet directo, IP fija y perfil recomendado, el sistema se sostuvo durante corridas largas con conectividad estable, sin overflow y con muy pocos errores SPI.

## 15. Que significa DRDY en el enlace sniffer -> 3B+?

DRDY es una linea de data ready para avisar al host que hay informacion nueva
disponible. En la mejora posterior a la validacion, la Pico sniffer usa `GP20`
como DRDY y la Raspberry Pi lo lee en `GPIO25`.

Cuando el snapshot del sniffer recibe una palabra nueva aceptada, `GP20` sube.
La 3B+ detecta ese nivel, consulta `GET_LATEST_META` y luego los slots
necesarios por SPI. Al atender `GET_LATEST_META`, la sniffer baja DRDY. El
polling queda solo como fallback por timeout.

## 16. Como se interpreta la corrida larga con mas de 27 millones de palabras?

Las corridas finales autorate reportaron ocho horas a 12.5 kbps y ocho horas a 100 kbps, ambas con cero errores SPI operativos, cero errores de paridad, cero overflow y cero drops. A 100 kbps se recibieron 76.707.606 palabras; a 12.5 kbps se recibieron 9.876.638 palabras.

La lectura correcta es que el sistema ya no es una prueba breve: es una baseline estable, con datos exportados a JSON/PDF/CSV y con evidencia de osciloscopio.

## 17. Que limitaciones conocidas quedan en el sniffer?

Actualmente el snapshot del sniffer tiene 16 slots y el filtro SPI tiene 10 entradas. Para el conjunto actual de variables alcanza: temperatura, velocidad, altitud y ACK_BATCH ocupan solo 4 slots.

Si en el futuro se quiere monitorear muchas mas variables simultaneas, habra que aumentar LATEST_SLOT_CAPACITY y posiblemente SNIFFER_SPI_FILTER_CAPACITY. Esa es una mejora de escala, no un bloqueo de la demostracion actual.

## 18. Ya esta encarado el diseno de PCB?

Todavia no como diseno final. El proyecto esta en una etapa correcta para empezar a pensar la PCB, pero antes conviene cerrar la decision de la capa electrica real: transceptores o drivers ARINC 429, protecciones, conectores, alimentacion, referencia de masa, aislamiento si aplica y puntos de test.

Hacer una PCB antes de decidir esa interfaz podria fijar errores temprano. La fase actual sirve como baseline funcional; la PCB deberia venir despues de elegir y validar el front-end electrico.

## 19. Se puede automatizar parte del desarrollo de PCB con Codex?

Si, pero con limites claros. Codex puede ayudar a preparar requisitos, checklist de diseno, BOM preliminar, estructura de proyecto KiCad, scripts para revisar netlists, documentacion de pines, tablas de conectividad y comparacion de alternativas de transceptores.

No conviene delegar ciegamente el diseno fisico completo. La revision electrica, reglas de seguridad, layout de senales, protecciones y validacion instrumental deben pasar por software especializado como KiCad y por revision humana. Codex puede acelerar y ordenar, no reemplazar la validacion de ingenieria.

## 20. Que se puede afirmar con seguridad ante el tutor?

Se puede afirmar que la fase arinc429_logic quedo validada como recreacion logica/temporal estable a 100 kbps y 12.5 kbps, con sniffer pasivo, autorate, filtrado por label + SDI, paridad estricta, SPI por tramas completas, bridge HTTP, dashboard Flask, Prometheus/Grafana, supervisor y evidencia de osciloscopio.

Tambien se puede afirmar que la arquitectura modular fue correcta: se pudo cambiar la forma de generacion/captura de la senal sin redisenar la 3B+, el bridge ni el dashboard. Lo que sigue ya no es corregir esta fase logica, sino avanzar hacia la interfaz electrica ARINC 429 real y, despues, hacia una PCB.
