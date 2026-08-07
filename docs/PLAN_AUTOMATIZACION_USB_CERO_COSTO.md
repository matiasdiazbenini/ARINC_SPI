# Plan de automatizacion USB de costo cero

## 1. Proposito de este documento

Este archivo es un handoff para continuar la automatizacion del banco PAMPA /
ARINC 429 en otro chat o sesion de trabajo.

El objetivo final es que los tres firmwares puedan compilarse, validarse,
transferirse y ejecutarse en las Raspberry Pi Pico 2 W sin tocar fisicamente el
boton BOOTSEL durante el funcionamiento normal.

La solucion debe utilizar exclusivamente el hardware que ya existe:

- Pico 2 W `ARINC-TX`;
- Pico 2 W `ARINC-RX`;
- Pico 2 W `ARINC-SNIFFER`;
- Raspberry Pi 3B+;
- notebook;
- cables USB de datos ya disponibles;
- red Ethernet existente.

No se compraran Debug Probes, cables SWD, transistores, multiplexores, hubs ni
otros componentes especificamente para esta automatizacion.

## 2. Alcance cerrado

El camino elegido es la actualizacion cooperativa por USB mediante `picotool`.

La limitacion aceptada es la siguiente:

> Si una Pico deja de ejecutar por completo su firmware, deja de enumerar por
> USB o no responde a la interfaz de reinicio, la recuperacion podra requerir
> intervencion presencial con BOOTSEL.

Esta limitacion forma parte del alcance aprobado. No intentar eliminarla con
SWD, Debug Probe, control de `RUN`, corte remoto de alimentacion, OTA, un
bootloader personalizado ni hardware adicional.

## 3. Estado favorable ya comprobado en el repositorio

Los targets principales ya habilitan USB stdio mediante
`pico_enable_stdio_usb(... 1)` y sus programas llaman a `stdio_init_all()`.
Esto los deja, en principio, dentro del modelo de firmware cooperativo que
`picotool` puede reiniciar por USB sin presionar BOOTSEL.

Targets operativos que deben preservarse:

- TX: `arinc_tx_arinc429_logic_stream_autorate`;
- RX: `arinc_rx_arinc429_logic_stream`;
- Sniffer: `sniffer_arinc429_logic_pio_frame`.

Servicios existentes en la Raspberry Pi 3B+:

- `arinc-sniffer-bridge`;
- `arinc-dashboard-flask`;
- `arinc-supervisor`.

Herramientas existentes que deben reutilizarse y ampliarse:

- `tools/raspi_deploy.sh`;
- `tools/raspi_healthcheck.sh`;
- `tools/raspi_services.sh`;
- `HOST_3b+/stats_recorder.py`;
- endpoints `/health` y `/stats` del bridge y dashboard.

Antes de modificar archivos, la nueva sesion debe leer:

- `PROJECT_CONTEXT.md`;
- `README.md`;
- `docs/README.md`;
- `docs/operacion_raspberry_3b_deploy.md`;
- este documento.

Tambien debe ejecutar `git status` y revisar la rama actual. No borrar,
sobrescribir ni revertir cambios locales existentes.

## 4. Arquitectura objetivo

El flujo final buscado es:

```text
push o pull request
        |
        v
GitHub Actions: CI sin hardware
        |
        +-- lint y analisis estatico
        +-- pruebas unitarias
        +-- compilacion TX, RX y Sniffer
        +-- artefactos .uf2, .elf, .map y checksums
        |
        v
ejecucion manual "Deploy to lab"
        |
        v
runner self-hosted en Raspberry Pi 3B+
        |
        +-- descarga artefactos aprobados
        +-- detiene servicios ARINC
        +-- identifica cada Pico por USB
        +-- programa TX, RX y Sniffer con picotool
        +-- verifica cada escritura
        +-- vuelve a ejecutar los firmwares
        +-- inicia servicios ARINC
        +-- ejecuta healthcheck y prueba fisica corta
        +-- guarda logs, stats y reporte
        |
        v
GitHub Actions publica el resultado de la prueba
```

Los trabajos normales de CI pueden correr con cada `push`. El despliegue sobre
placas no debe dispararse automaticamente con cada cambio: debe comenzar con
`workflow_dispatch`, una seleccion deliberada desde GitHub Actions.

## 5. Regla de avance

El desarrollo sera incremental. No comenzar una fase hasta que la anterior
tenga pruebas repetibles y un resultado aprobado.

Cada cambio debe:

1. ser pequeno y facil de revisar;
2. incluir o actualizar pruebas;
3. conservar los targets y perfiles operativos validados;
4. generar evidencia de exito o fracaso;
5. poder deshabilitarse sin romper el banco actual.

## 6. Fase 1 - Inventario y linea de base

Objetivo: conocer exactamente que puede probarse sin hardware antes de agregar
funcionalidades.

Tareas:

1. Revisar los workflows actuales de `.github/workflows/`.
2. Enumerar los modulos C de logica pura y los modulos Python del host.
3. Identificar dependencias directas de GPIO, PIO, SPI, tiempo y USB.
4. Registrar los comandos actuales para compilar los tres targets.
5. Confirmar que CI puede producir `.uf2` y `.elf` sin placas conectadas.
6. Ejecutar localmente los linters y pruebas que ya existan.

Criterio de aprobacion:

- existe una lista concreta de funciones probables;
- se conocen los tres comandos de compilacion;
- el estado inicial de CI queda registrado;
- no se modifico el comportamiento del firmware.

## 7. Fase 2 - Pruebas unitarias sin hardware

Esta fase tiene prioridad absoluta. Primero se prueba la logica; despues se
automatiza la programacion de placas.

### 7.1. Logica C

Separar solamente cuando sea necesario las funciones puras que puedan probarse
en la notebook o en un runner Linux, sin Pico SDK ni registros de hardware.

Casos minimos:

- construccion y lectura de una palabra ARINC de 32 bits;
- orden y extraccion de `label`;
- extraccion de `SDI`;
- generacion y verificacion de paridad impar;
- interpretacion de `SSM`;
- aceptacion y rechazo por whitelist `label + SDI`;
- rechazo estricto de palabras con mala paridad;
- codificacion y decodificacion de los valores usados por temperatura,
  velocidad y altitud;
- clasificacion de temporizaciones sinteticas como `100 kbps`, `12.5 kbps` o
  velocidad desconocida;
- limites de los umbrales de autodeteccion.

El codigo dependiente de PIO, IRQ, GPIO y FIFO no se simulara de manera
artificial en esta primera etapa. Se validara luego mediante pruebas de
integracion sobre las placas.

### 7.2. Software Python de la 3B+

Usar `pytest` y mocks acotados para cubrir, como minimo:

- construccion y validacion de tramas SPI;
- magic, version, comando, longitud y CRC;
- rechazo de una respuesta incompleta o corrupta;
- decodificacion de snapshots y estadisticas;
- separacion entre errores de arranque y errores operativos;
- comportamiento de DRDY y fallback por timeout;
- endpoints Flask usando el cliente de pruebas;
- respuestas cuando el bridge esta desconectado;
- generacion de CSV, JSON y PDF del recorder usando un directorio temporal;
- validacion de argumentos de los scripts de operacion.

### 7.3. Criterio de aprobacion

- todas las pruebas pasan localmente;
- todas las pruebas pasan en GitHub Actions;
- un fallo real introducido a proposito hace fallar la prueba correspondiente;
- no hace falta ninguna Pico para ejecutar la suite;
- la suite no depende de horarios, red del laboratorio ni archivos externos.

## 8. Fase 3 - CI reproducible de los tres firmwares

Objetivo: convertir cada commit aprobado en artefactos identificables.

Tareas:

1. Instalar o cachear Pico SDK y toolchain en GitHub Actions.
2. Compilar los tres targets operativos en directorios separados.
3. Publicar `.uf2`, `.elf`, `.map` y `SHA-256`.
4. Incluir commit, rama, fecha, target y version del SDK en un manifiesto JSON.
5. Hacer que una falla de compilacion o de pruebas impida publicar artefactos.

Criterio de aprobacion:

- una ejecucion limpia produce los tres paquetes;
- repetirla desde el mismo commit produce el mismo conjunto identificable;
- los artefactos pueden descargarse manualmente y cargarse con BOOTSEL como
  control de referencia.

## 9. Fase 4 - Prueba USB local con una sola Pico

Esta es la primera fase con hardware.

Condiciones:

- usar una sola placa;
- detener previamente el sistema ARINC;
- conectar la Pico por un cable USB de datos a la 3B+;
- instalar una version compatible de `picotool` en la 3B+;
- conservar un firmware conocido como bueno.

Secuencia a validar:

1. Identificar la Pico y registrar su identificador unico.
2. Consultar informacion del firmware actual.
3. Forzar el ingreso cooperativo a USB BOOTSEL.
4. Cargar un `.uf2` conocido.
5. Verificar la escritura.
6. Reiniciar y comprobar que la aplicacion vuelve a enumerar.
7. Repetir el ciclo varias veces sin tocar BOOTSEL.

Comando de referencia, sujeto a confirmar con la version instalada:

```bash
picotool load -f -v -x firmware.uf2 --ser IDENTIFICADOR
```

Criterio de aprobacion:

- al menos 10 ciclos consecutivos correctos;
- cero intervenciones con BOOTSEL;
- seleccion inequivoca de la placa;
- firmware y version comprobables despues de reiniciar.

Si falla esta fase, no avanzar al despliegue de las tres placas.

## 10. Fase 5 - Script de despliegue por USB

Crear un script ejecutado en la 3B+ que reciba:

- rol: `tx`, `rx`, `sniffer` o `all`;
- ruta de artefactos;
- identificador esperado de cada Pico;
- opcion `--dry-run`;
- opcion de verificacion sin escritura.

El mapa rol-dispositivo debe estar en un archivo de configuracion versionado,
sin secretos. Un ejemplo conceptual:

```text
tx       -> identificador USB de la Pico TX
rx       -> identificador USB de la Pico RX
sniffer  -> identificador USB de la Pico Sniffer
```

El script debe abortar antes de escribir si:

- falta una placa;
- hay mas de una coincidencia;
- el identificador no corresponde al rol;
- falta el firmware;
- el checksum no coincide;
- hay otro despliegue en curso.

Criterio de aprobacion:

- primero funciona con una placa;
- luego con dos;
- finalmente con las tres, siempre en secuencia;
- nunca programa una placa diferente a la solicitada;
- conserva logs completos de cada operacion.

## 11. Fase 6 - Integracion con servicios de la 3B+

El despliegue completo debe coordinarse con el banco:

1. adquirir un lock exclusivo de despliegue;
2. guardar `/stats` iniciales;
3. detener supervisor, dashboard y bridge en orden controlado;
4. programar las placas;
5. esperar la enumeracion USB y el arranque del sniffer;
6. iniciar bridge, dashboard y supervisor;
7. ejecutar `tools/raspi_healthcheck.sh` o su equivalente local;
8. liberar el lock incluso ante errores;
9. dejar los servicios en un estado conocido.

Criterio de aprobacion:

- los servicios vuelven a `active (running)`;
- `/health` responde correctamente;
- `/stats` muestra el transporte esperado;
- un fallo intermedio produce un diagnostico claro y no otro flasheo en
  paralelo.

## 12. Fase 7 - Runner self-hosted y despliegue manual

Instalar el runner de GitHub Actions como servicio en la Raspberry Pi 3B+.

El workflow de hardware debe usar un runner con etiquetas dedicadas, por
ejemplo:

```yaml
runs-on: [self-hosted, linux, arm, arinc-lab]
```

Politica inicial:

- CI de software: en `push` y `pull_request`;
- despliegue de hardware: solamente `workflow_dispatch`;
- permitir despliegue unicamente desde la rama `ARINC` o un tag aprobado;
- no ejecutar codigo de pull requests externos sobre la 3B+;
- usar `concurrency` para permitir un solo trabajo HIL simultaneo;
- permisos de GitHub minimos y secretos fuera de los logs.

Criterio de aprobacion:

- el boton manual inicia el trabajo en la 3B+;
- la revision desplegada coincide con la seleccionada;
- GitHub muestra claramente exito o fallo;
- los logs no contienen credenciales.

## 13. Fase 8 - Prueba fisica y evidencia automatica

Despues de una carga correcta, ejecutar una prueba corta y progresiva:

1. smoke test de 1 minuto;
2. prueba de 5 minutos;
3. prueba de 15 o 20 minutos;
4. corrida larga solo despues de aprobar las anteriores.

Comprobaciones minimas sin inyeccion deliberada de errores:

- `connected = true`;
- velocidad detectada valida: `100000` o `12500` bps;
- palabras aceptadas creciendo;
- labels esperados presentes;
- `parity_error = 0`;
- `spi_errors = 0` en regimen operativo;
- `overflow_events = 0`;
- `spi_drop_events = 0`;
- servicios informados como saludables por el supervisor.

Guardar como artefactos de GitHub Actions:

- salida del despliegue;
- manifiesto de firmware;
- checksums;
- `summary.json`;
- `samples.csv`;
- `events.csv`;
- `metadata.json`;
- `report.pdf`;
- logs del healthcheck y servicios.

## 14. Lo que no se implementara

Para evitar que el alcance vuelva a crecer, quedan explicitamente fuera:

- compra o construccion de Debug Probes;
- SWD para las tres placas;
- multiplexacion SWD;
- transistores sobre `RUN`;
- control remoto individual de alimentacion;
- OTA por Wi-Fi;
- bootloader dual o actualizacion A/B;
- recuperacion garantizada de una Pico cuyo firmware no responde;
- despliegue fisico automatico ante cada push;
- cambios en la parte electrica ARINC 429 por causa de esta automatizacion.

## 15. Definicion de terminado

El objetivo se considera alcanzado cuando:

1. un push ejecuta lint, analisis, pruebas unitarias y compilacion;
2. GitHub conserva artefactos identificables de TX, RX y Sniffer;
3. una accion manual despliega una revision elegida en las tres Pico por USB;
4. ninguna carga normal requiere tocar BOOTSEL;
5. la 3B+ reinicia los servicios y verifica el sistema;
6. una prueba fisica genera resultados descargables desde GitHub Actions;
7. los fallos quedan diagnosticados sin ocultarse ni contarse como exito;
8. la unica recuperacion presencial aceptada es el caso limite ya definido.

## 16. Primera tarea para la proxima sesion

No comenzar por el runner ni por `picotool`.

La primera tarea es realizar la Fase 1 y proponer una suite concreta de pruebas
unitarias para la Fase 2. Implementar primero una porcion pequena de esas
pruebas, ejecutarlas localmente y hacerlas pasar en CI. Recien despues de tener
esa base se debe avanzar hacia la programacion USB.
