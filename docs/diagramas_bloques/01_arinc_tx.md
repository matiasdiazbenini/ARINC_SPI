# Diagrama interno: ARINC-TX

## 1. Funcion de la placa

ARINC-TX genera palabras ARINC de laboratorio, las codifica como una senal
bipolar con retorno a cero y las transmite por:

- `GP6`: FWD_A.
- `GP7`: FWD_B.

En el modo operativo validado, `arinc_tx_arinc429_logic_stream_gp6_gp7`, transmite
rafagas aleatorias de 1 a 2000 palabras con pausas aleatorias de 0 a 25 ms a
`100 kbps`. El target `arinc_tx_arinc429_logic_stream_12k5_gp6_gp7` fuerza
`12.5 kbps`. No espera ACK y no depende del canal REV.

## 2. Diagrama general

```mermaid
flowchart LR
    subgraph CPU["RP2040 - codigo C"]
        RNG["PRNG LCG<br/>estado rng"]
        INDEX["global_word_index<br/>selector ciclico"]
        STATE["Estado de variables<br/>temperatura<br/>velocidad<br/>altitud"]
        PROFILE["Tabla de perfiles baseline<br/>3 labels utiles<br/>7 labels de ruido"]
        ENCODE["Codificador de magnitud<br/>valor fisico a DATA de 19 bits"]
        SSM["Selector SSM<br/>NORMAL / NCD<br/>FUNCTIONAL_TEST / FAILURE"]
        WORD["Constructor de palabra<br/>label + SDI + DATA + SSM + paridad"]
        STREAM["Planificador stream<br/>rafaga 1..2000<br/>gap 0..25 ms"]

        RNG --> STATE
        RNG --> SSM
        RNG --> STREAM
        INDEX --> PROFILE
        STATE --> ENCODE
        PROFILE --> ENCODE
        ENCODE --> WORD
        PROFILE --> WORD
        SSM --> WORD
        STREAM --> WORD
    end

    WORD -->|"pio_sm_put_blocking(word)"| TXFIFO["PIO0 SM0 TX FIFO<br/>8 palabras de 32 bits"]

    subgraph PIO["PIO0 SM0 - arinc429_logic_tx"]
        TXFIFO -->|"pull block"| OSR["OSR<br/>palabra de 32 bits"]
        OSR -->|"out x, 1<br/>LSB primero"| X["Registro X<br/>bit actual"]
        Y["Registro Y<br/>contador 31..0"] --> LOOP["Bucle de 32 bits"]
        X --> MAP["1 -> HI = 10<br/>0 -> LO = 01"]
        MAP --> RZ["Primera mitad activa: 5 us<br/>segunda mitad NULL: 5 us"]
        RZ --> GAP["Gap entre palabras<br/>8 medias celdas NULL"]
    end

    GAP --> PINS["GP6/GP7<br/>FWD_A/FWD_B"]
```

## 3. Construccion de la palabra

La palabra se arma en un `uint32_t`. La numeracion siguiente es la posicion
interna usada por el firmware:

| Bits en `uint32_t` | Longitud | Campo | Operacion |
|---|---:|---|---|
| 0..7 | 8 | Label en orden de cable | `reverse_u8(label)` |
| 8..9 | 2 | SDI | `sdi & 0x03` |
| 10..28 | 19 | DATA | `data & 0x7FFFF` |
| 29..30 | 2 | SSM | `ssm & 0x03` |
| 31 | 1 | Paridad impar | Calculada sobre bits 0..30 |

```mermaid
flowchart LR
    LABEL["Label legible<br/>ej. B1"] --> REV["Inversion de 8 bits<br/>B1 -> 8D"]
    SDI["SDI 2 bits"] --> PACK["OR y desplazamientos"]
    DATA["DATA 19 bits"] --> PACK
    SSM["SSM 2 bits"] --> PACK
    REV --> PACK
    PACK --> W31["Palabra sin paridad<br/>bits 0..30"]
    W31 --> COUNT["Contar unos"]
    COUNT --> ODD["Elegir bit 31<br/>total de unos impar"]
    ODD --> WORD["Palabra final de 32 bits"]
```

El label se invierte antes de colocarlo en el byte bajo porque el PIO extrae
la palabra LSB primero. Al recibirla, el firmware vuelve a invertir ese byte
para recuperar el label legible.

Ejemplos:

| Label logico | Byte transmitido primero |
|---|---|
| `0xA5` | `0xA5` |
| `0xB1` | `0x8D` |
| `0xC2` | `0x43` |

## 4. Generacion de magnitudes

| Label | SDI | Nombre | Codificacion TX |
|---|---:|---|---|
| `0xA5` | 0 | TEMPERATURA | `raw = (temperatura_C + 50) / 0.25` |
| `0xB1` | 1 | VELOCIDAD | `raw = velocidad_kt` |
| `0xC2` | 2 | ALTITUD | `raw = altitud_ft / 10` |

Los demas perfiles generan trafico de ruido para demostrar que el receptor y
el sniffer no aceptan cualquier palabra solo porque su paridad sea correcta.

## 5. Movimiento por registros y FIFO PIO

```mermaid
sequenceDiagram
    participant C as CPU / codigo C
    participant F as TX FIFO PIO
    participant O as OSR
    participant X as registro X
    participant P as GP6/GP7

    C->>F: pio_sm_put_blocking(word)
    Note over C,F: Si el FIFO esta lleno, la CPU espera
    F->>O: pull block
    loop 32 bits
        O->>X: out x, 1
        alt X = 1
            X->>P: HI 10 durante 5 us
        else X = 0
            X->>P: LO 01 durante 5 us
        end
        X->>P: NULL 00 durante 5 us
    end
    P->>P: NULL durante el gap de palabra
```

Detalles:

- La union `PIO_FIFO_JOIN_TX` entrega a SM0 un FIFO TX de 8 palabras.
- `pull block` evita que el PIO transmita datos inexistentes.
- `OSR` conserva la palabra y la desplaza bit por bit.
- `X` contiene solamente el bit que se esta transmitiendo.
- `Y` comienza en 31 y controla las 32 iteraciones.
- El divisor PIO se calcula con la velocidad activa del target.
- A `100 kbps`, cada bit ocupa 10 us: 5 us activo y 5 us en NULL.
- A `12.5 kbps`, cada bit ocupa 80 us: 40 us activo y 40 us en NULL.
- El PIO mantiene la temporizacion aunque la CPU este preparando la palabra
  siguiente.

## 6. Simbolos electricos logicos

El valor escrito por `set pins` se interpreta como `GP7:GP6`:

| Bit | Valor PIO | GP6 | GP7 | Simbolo | `GP7 - GP6` ideal |
|---:|---:|---:|---:|---|---:|
| 1 | `10` | 0 | 1 | HI | +3.3 V |
| 0 | `01` | 1 | 0 | LO | -3.3 V |
| reposo | `00` | 0 | 0 | NULL | 0 V |
| invalido | `11` | 1 | 1 | INVALID | 0 V, no generado |

Si el osciloscopio calcula `GP6 - GP7`, los signos positivo y negativo quedan
invertidos. La codificacion no cambia; cambia el orden de la resta.

Estos son niveles logicos de laboratorio. Una interfaz ARINC 429 de campo
requiere un line driver que produzca los niveles y la impedancia definidos por
la norma; no se conecta el RP2040 directamente a una linea aeronautica real.

## 7. Modo stream actual

```mermaid
flowchart TD
    START["Inicio"] --> BURST["Elegir longitud aleatoria<br/>1..2000 palabras"]
    BURST --> GEN["Actualizar variables<br/>elegir perfil y SSM"]
    GEN --> BUILD["Construir palabra"]
    BUILD --> PUT["Enviar al FIFO TX"]
    PUT --> MORE{"Quedan palabras<br/>en la rafaga?"}
    MORE -->|si| GEN
    MORE -->|no| STATS["Actualizar estadisticas"]
    STATS --> GAP["Elegir pausa aleatoria<br/>0..25 ms"]
    GAP --> BURST
```

Este modo demuestra que el enlace no depende de lotes fijos de 1000 palabras.
Las rafagas son ventanas de generacion, no unidades de protocolo ni bloques que
el receptor deba completar.

## 8. Modo batch historico

El target de laboratorio conserva el mecanismo anterior:

1. Limpia el receptor REV de TX.
2. Transmite 1000 palabras por FWD.
3. Espera una palabra `0xAC/SDI 3` por GP4/GP5.
4. Comprueba paridad, SSM y numero de lote.
5. Inicia el lote siguiente.

```mermaid
flowchart LR
    B["1000 palabras FWD"] --> WAIT["Esperar ACK REV"]
    WAIT --> CHECK["Validar label AC<br/>SDI 3 + paridad + SSM"]
    CHECK --> NEXT["Siguiente lote"]
```

Este modo sirve para ensayos de ida y vuelta, pero no representa un unico bus
ARINC bidireccional. FWD y REV son dos enlaces simplex independientes.

## 9. Recursos internos principales

| Recurso | Uso |
|---|---|
| CPU RP2040 | Generacion de datos, labels, SSM, paridad y estadisticas |
| PIO0 SM0 | Transmisor FWD bipolar RZ |
| PIO0 SM1 | Receptor REV en targets que lo habilitan |
| TX FIFO SM0 | Desacopla la CPU de la temporizacion de cada bit |
| OSR | Registro de desplazamiento de la palabra transmitida |
| X | Bit actual |
| Y | Contador de 32 bits |

## 10. Fuentes de implementacion

- [`ARINC-TX/arinc_tx.c`](../../ARINC-TX/arinc_tx.c)
- [`ARINC-TX/arinc429_logic.h`](../../ARINC-TX/arinc429_logic.h)
- [`ARINC-TX/arinc429_logic.pio`](../../ARINC-TX/arinc429_logic.pio)
