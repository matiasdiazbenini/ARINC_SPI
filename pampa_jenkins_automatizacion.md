# Propuesta de CI/CD para el proyecto PAMPA con Jenkins, Raspberry Pi y Raspberry Pi Pico

## Objetivo

Automatizar el flujo completo del proyecto PAMPA:

1. Detectar cambios en GitHub.
2. Compilar y probar el software.
3. Generar los firmware `.uf2` o `.elf`.
4. Programar automáticamente las Raspberry Pi Pico.
5. Ejecutar ensayos físicos sobre SPI / ARINC-429.
6. Recolectar métricas.
7. Publicar resultados en Flask, Grafana o Jenkins.

## Arquitectura propuesta

```text
GitHub
   │ webhook
   ▼
Jenkins Controller
   │
   ├── Agente Docker
   │     ├── lint
   │     ├── pruebas unitarias
   │     ├── compilación
   │     └── construcción de imágenes
   │
   └── Agente Raspberry Pi de laboratorio
         ├── programa Pico transmisora
         ├── programa Pico receptora
         ├── programa Pico monitor
         ├── ejecuta ensayos físicos
         ├── recolecta métricas
         └── publica resultados
```

El **controller** coordina el pipeline. Los **agents** ejecutan tareas según sus capacidades.

## Aplicación concreta en PAMPA

Un cambio en el código podría disparar este flujo:

```text
git push
→ Jenkins
→ pruebas automáticas
→ compilación del firmware
→ programación de las Pico
→ ensayo físico SPI
→ medición de latencia, jitter y errores
→ generación de reportes
→ actualización de dashboard
```

Ejemplos de métricas:

- palabras transmitidas;
- palabras recibidas;
- tasa de éxito;
- latencia media;
- jitter máximo;
- errores de paridad;
- frecuencia efectiva;
- uso de buffers;
- etiquetas ARINC detectadas;
- estado de Flask, Prometheus o Grafana.

## Problema actual: BOOTSEL manual

La carga tradicional de un archivo `.uf2` exige colocar manualmente cada Pico en modo BOOTSEL presionando un botón. Eso impide una automatización real.

## Soluciones posibles

### 1. SWD + Debug Probe — opción recomendada

Usar un Raspberry Pi Debug Probe o una segunda Pico configurada como probe.

Ventajas:

- no requiere pulsar BOOTSEL;
- permite programar, verificar y reiniciar;
- funciona incluso si el firmware anterior quedó bloqueado;
- permite depuración con GDB;
- es la opción más robusta para un banco automático.

Ejemplo conceptual:

```bash
openocd \
  -f interface/cmsis-dap.cfg \
  -f target/rp2350.cfg \
  -c "program build/pampa.elf verify reset exit"
```

Para varias Pico, conviene usar un probe por placa o identificarlas por el número de serie de cada probe.

### 2. `picotool load -f` — opción simple por USB

Permite intentar cargar el firmware sin presionar BOOTSEL:

```bash
picotool load -f build/pampa.uf2
picotool verify -f build/pampa.uf2
```

Ventajas:

- simple;
- no requiere hardware adicional;
- útil para desarrollo diario.

Limitación:

- depende de que el firmware actual siga respondiendo por USB;
- puede fallar si el firmware se cuelga, deshabilita USB o queda bloqueado.

### 3. Reinicio a BOOTSEL desde el firmware

Agregar un comando de mantenimiento por USB serial, UART o red.

Ejemplo con Pico SDK:

```c
#include "pico/bootrom.h"

void enter_bootloader(void) {
    reset_usb_boot(0, 0);
}
```

El agente podría enviar un comando como:

```bash
echo "ENTER_BOOTSEL" > /dev/pampa-transmitter
```

y luego programar con `picotool`.

Limitación: también depende de que el firmware siga vivo y procese el comando.

## Estrategia recomendada

```text
Método principal:
picotool load -f

Fallback:
SWD + OpenOCD

Después:
reset
→ ensayo físico
→ captura de métricas
→ publicación de resultados
```

Para un banco confiable y repetible, SWD debería ser el mecanismo definitivo.

## Identificación de cada placa

No conviene confiar en nombres variables como:

```text
/dev/ttyACM0
/dev/ttyACM1
/dev/ttyACM2
```

Se recomienda usar:

- números de serie USB;
- reglas `udev`;
- seriales de los Debug Probe;
- enlaces simbólicos estables.

Ejemplo:

```text
/dev/pampa-transmitter
/dev/pampa-receiver
/dev/pampa-monitor
```

Así Jenkins puede saber exactamente qué dispositivo debe programar y probar.

## Pipeline conceptual

```groovy
pipeline {
    agent none

    stages {
        stage('Tests unitarios') {
            agent { label 'docker-agent' }

            steps {
                sh '''
                    python3 -m venv .venv
                    . .venv/bin/activate
                    pip install -r requirements.txt
                    pytest tests/unit
                '''
            }
        }

        stage('Compilar firmware') {
            agent { label 'raspberry-lab' }

            steps {
                sh '''
                    cmake -S . -B build
                    cmake --build build -j
                '''
            }
        }

        stage('Programar Pico') {
            agent { label 'raspberry-lab' }

            steps {
                sh '''
                    if ! picotool load -f build/pampa.uf2; then
                        openocd \
                          -f interface/cmsis-dap.cfg \
                          -f target/rp2350.cfg \
                          -c "program build/pampa.elf verify reset exit"
                    fi
                '''
            }
        }

        stage('Ensayo físico') {
            agent { label 'raspberry-lab' }

            steps {
                sh '''
                    python3 tests/run_spi_test.py
                    python3 tests/validate_arinc_capture.py
                '''
            }
        }

        stage('Publicar métricas') {
            agent { label 'raspberry-lab' }

            steps {
                sh '''
                    python3 tools/publish_metrics.py results.json
                '''
            }
        }
    }

    post {
        always {
            archiveArtifacts artifacts: 'reports/**/*, results/**/*',
                             allowEmptyArchive: true
        }
    }
}
```

## Integración con Flask y Grafana

La Raspberry Pi de laboratorio podría ejecutar:

- una API Flask para estado y control;
- Prometheus o InfluxDB para métricas;
- Grafana para visualización histórica.

Endpoints posibles:

```text
GET /health
GET /metrics
POST /test/start
POST /firmware/deploy
GET /results/latest
```

Métricas sugeridas:

```text
pampa_words_received_total
pampa_words_invalid_total
pampa_spi_latency_ms
pampa_spi_jitter_ms
pampa_label_frequency_hz
pampa_buffer_usage_percent
```

Jenkins podría validar automáticamente:

```bash
curl --fail http://raspberry-pampa:5000/health
curl --fail http://raspberry-pampa:3000/api/health
```

## Resultado esperado

```text
Cambio de código
→ pruebas
→ compilación
→ programación automática
→ ensayo con hardware
→ métricas
→ reportes
→ dashboard
```

Esto convierte el proyecto PAMPA en un banco de validación reproducible, automatizado y trazable.
