from __future__ import annotations

import json
import uuid
from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]
PROJECT_DIR = ROOT / "ELECTRICO" / "frontend_arinc429_lab"
SCH_PATH = PROJECT_DIR / "frontend_arinc429_lab.kicad_sch"
PRO_PATH = PROJECT_DIR / "frontend_arinc429_lab.kicad_pro"
README_PATH = PROJECT_DIR / "README.md"

NAMESPACE = uuid.UUID("b986c4cf-4c6b-4e11-bd63-843c88be9b9e")

COLOR_SIGNAL = (0, 130, 0, 255)
COLOR_POWER = (190, 20, 20, 255)
COLOR_GND = (30, 70, 180, 255)
COLOR_VTH = (155, 80, 200, 255)
COLOR_RETURN = (210, 110, 0, 255)
COLOR_FRAME = (90, 90, 90, 255)


def uid(name: str) -> str:
    return str(uuid.uuid5(NAMESPACE, name))


def stroke(width: float = 0.15, kind: str = "solid", color: tuple[int, int, int, int] | None = None) -> str:
    color_line = ""
    if color is not None:
        color_line = f"""
			(color {color[0]} {color[1]} {color[2]} {color[3]})"""
    return f"""
		(stroke
			(width {width})
			(type {kind}){color_line}
		)"""


def effects(size: float = 1.27, justify: str = "left bottom", bold: bool = False) -> str:
    bold_line = "\n				(bold yes)" if bold else ""
    return f"""
		(effects
			(font
				(size {size} {size}){bold_line}
			)
			(justify {justify})
		)"""


def text(item_id: str, value: str, x: float, y: float, size: float = 1.27, justify: str = "left bottom", bold: bool = False) -> str:
    escaped = value.replace("\\", "\\\\").replace('"', '\\"').replace("\n", "\\n")
    return f"""
	(text "{escaped}"
		(exclude_from_sim no)
		(at {x:.2f} {y:.2f} 0){effects(size, justify, bold)}
		(uuid "{uid('text-' + item_id)}")
	)"""


def rect(item_id: str, x1: float, y1: float, x2: float, y2: float, kind: str = "solid") -> str:
    # KiCad schematic top-level rectangles are not accepted consistently across
    # versions. Use ordinary wires for review boxes; they open and export
    # reliably and remain easy to edit in the schematic editor.
    return "".join(
        [
            wire(f"{item_id}_top", x1, y1, x2, y1, 0.12, "solid", COLOR_FRAME),
            wire(f"{item_id}_right", x2, y1, x2, y2, 0.12, "solid", COLOR_FRAME),
            wire(f"{item_id}_bottom", x2, y2, x1, y2, 0.12, "solid", COLOR_FRAME),
            wire(f"{item_id}_left", x1, y2, x1, y1, 0.12, "solid", COLOR_FRAME),
        ]
    )


def wire(
    item_id: str,
    x1: float,
    y1: float,
    x2: float,
    y2: float,
    width: float = 0.0,
    kind: str = "solid",
    color: tuple[int, int, int, int] | None = COLOR_SIGNAL,
) -> str:
    return f"""
	(wire
		(pts
			(xy {x1:.2f} {y1:.2f}) (xy {x2:.2f} {y2:.2f})
		){stroke(width, kind, color)}
		(uuid "{uid('wire-' + item_id)}")
	)"""


def label(item_id: str, value: str, x: float, y: float, size: float = 1.10) -> str:
    escaped = value.replace('"', '\\"')
    return f"""
	(label "{escaped}"
		(at {x:.2f} {y:.2f} 0){effects(size, "left bottom", False)}
		(uuid "{uid('label-' + item_id)}")
	)"""


def build_schematic() -> str:
    parts: list[str] = []

    # Title and scope.
    parts.append(text("title", "FRONT-END ELECTRICO ARINC 429 - BANCO DE LABORATORIO", 18, 18, 3.2, "left bottom", True))
    parts.append(text("subtitle", "Esquema funcional inicial: Pico 0..3,3 V -> TX bipolar +/-5 V por conductor -> RX protegido -> Pico 0..3,3 V", 18, 25, 1.35))
    parts.append(text("warning", "NO conectar a una linea ARINC real de campo. Este circuito es para banco controlado.", 18, 32, 1.35, "left bottom", True))
    parts.append(rect("legend", 272, 14, 407, 40))
    parts.append(text("legend_title", "Convencion visual", 285, 20, 1.05, "left bottom", True))
    parts.extend(
        [
            wire("legend_signal", 285, 26, 302, 26, 0.12, "solid", COLOR_SIGNAL),
            text("legend_signal_text", "senal / linea", 304, 27, 0.85),
            wire("legend_power", 285, 31, 302, 31, 0.30, "solid", COLOR_POWER),
            text("legend_power_text", "alimentacion", 304, 32, 0.85),
            wire("legend_return", 285, 36, 302, 36, 0.20, "dash_dot", COLOR_RETURN),
            text("legend_return_text", "retorno RX", 304, 37, 0.85),
            wire("legend_gnd", 345, 26, 362, 26, 0.30, "dash", COLOR_GND),
            text("legend_gnd_text", "GND/ref", 364, 27, 0.85),
            wire("legend_vth", 345, 31, 362, 31, 0.22, "dot", COLOR_VTH),
            text("legend_vth_text", "umbral", 364, 32, 0.85),
        ]
    )

    # Main blocks.
    parts.append(rect("pico", 18, 45, 70, 128))
    parts.append(text("pico_title", "J1 - Raspberry Pi Pico de prueba", 22, 52, 1.35, "left bottom", True))
    pico_lines = [
        "GP2  TX_A_LOGIC",
        "GP3  TX_B_LOGIC",
        "GP4  RX_A_LOGIC",
        "GP5  RX_B_LOGIC",
        "3V3  logica",
        "GND  comun banco",
    ]
    for i, line in enumerate(pico_lines):
        parts.append(text(f"pico_{i}", line, 23, 62 + i * 8, 1.15))

    parts.append(rect("tx", 88, 45, 183, 128))
    parts.append(text("tx_title", "TX bipolar", 93, 52, 1.55, "left bottom", True))
    parts.append(rect("u1a", 98, 62, 168, 90))
    parts.append(text("u1a_title", "U1A op-amp diferencial", 102, 69, 1.25, "left bottom", True))
    parts.append(text("u1a_notes", "Rin=20k / Rf=30k / G=1,5\nLINE_A = 1,5*(TX_A-TX_B)", 102, 76, 1.05))
    parts.append(rect("u1b", 98, 96, 168, 119))
    parts.append(text("u1b_title", "U1B inversor", 102, 103, 1.25, "left bottom", True))
    parts.append(text("u1b_notes", "R=10k / Rf=10k / G=-1\nLINE_B = -LINE_A", 102, 110, 1.05))

    parts.append(rect("line", 205, 60, 257, 112))
    parts.append(text("line_title", "J2 - Linea A/B", 211, 68, 1.45, "left bottom", True))
    for i, line in enumerate(["1 LINE_A", "2 LINE_B", "3 GND_REF"]):
        parts.append(text(f"line_{i}", line, 213, 80 + i * 8, 1.15))
    parts.append(text("line_note", "Medir MATH = A-B\nesperado: +10 / 0 / -10 V", 210, 118, 1.05))

    parts.append(rect("rx", 280, 45, 407, 158))
    parts.append(text("rx_title", "RX protegido y comparadores", 286, 52, 1.45, "left bottom", True))
    parts.append(rect("rx_a_div", 288, 64, 344, 88))
    parts.append(text("rx_a_div_title", "A_SENSE", 292, 71, 1.20, "left bottom", True))
    parts.append(text("rx_a_div_note", "LINE_A -> 22k -> nodo\n33k a GND + clamps", 292, 78, 1.00))
    parts.append(rect("rx_b_div", 288, 112, 344, 136))
    parts.append(text("rx_b_div_title", "B_SENSE", 292, 119, 1.20, "left bottom", True))
    parts.append(text("rx_b_div_note", "LINE_B -> 22k -> nodo\n33k a GND + clamps", 292, 126, 1.00))
    parts.append(rect("u2a", 355, 64, 398, 88))
    parts.append(text("u2a_title", "U2A MCP6562", 359, 71, 1.10, "left bottom", True))
    parts.append(text("u2a_note", "A_SENSE > VTH\n-> RX_A_LOGIC / GP4", 359, 78, 1.00))
    parts.append(rect("u2b", 355, 112, 398, 136))
    parts.append(text("u2b_title", "U2B MCP6562", 359, 119, 1.10, "left bottom", True))
    parts.append(text("u2b_note", "B_SENSE > VTH\n-> RX_B_LOGIC / GP5", 359, 126, 1.00))
    parts.append(text("rx_lm393", "U2A/U2B: VDD=+3V3_LOGIC, VSS=GND_COMUN. Si U2=LM393: pull-up 4k7 a 3V3.", 288, 150, 0.95))

    parts.append(text("return_to_pico_note", "Misma etiqueta = misma conexion electrica. RX_A/RX_B salen de U2 y vuelven a GP4/GP5 como 0..3,3 V.", 92, 140, 1.08, "left bottom", True))

    # Signal wires and labels.
    parts.extend(
        [
            wire("tx_a_to_u1a", 70, 66, 98, 66),
            label("tx_a", "TX_A_LOGIC", 72, 64),
            wire("tx_b_to_u1a", 70, 74, 98, 74),
            label("tx_b", "TX_B_LOGIC", 72, 72),
            wire("u1a_to_line_a", 168, 74, 205, 74),
            label("line_a_1", "LINE_A", 172, 72),
            wire("u1a_to_u1b", 168, 82, 184, 82),
            wire("u1a_to_u1b_down", 184, 82, 184, 104),
            wire("u1a_to_u1b_in", 184, 104, 168, 104),
            label("line_a_internal", "LINE_A", 170, 86),
            wire("u1b_to_line_b_a", 168, 108, 188, 108),
            wire("u1b_to_line_b_b", 188, 108, 188, 92),
            wire("u1b_to_line_b_c", 188, 92, 205, 92),
            label("line_b_1", "LINE_B", 174, 104),
            wire("line_a_to_rx", 257, 74, 288, 74),
            label("line_a_2", "LINE_A", 261, 72),
            wire("line_b_to_rx_a", 257, 92, 273, 92),
            wire("line_b_to_rx_b", 273, 92, 273, 122),
            wire("line_b_to_rx_c", 273, 122, 288, 122),
            label("line_b_2", "LINE_B", 261, 91),
            wire("a_sense_to_comp", 344, 76, 355, 76),
            label("a_sense", "A_SENSE", 346, 74),
            wire("b_sense_to_comp", 344, 124, 355, 124),
            label("b_sense", "B_SENSE", 346, 122),
            wire("rx_a_out_stub", 398, 76, 406, 76, 0.20, "dash_dot", COLOR_RETURN),
            text("rx_a_out_text", "salida: RX_A_LOGIC -> GP4", 360, 92, 0.82),
            wire("rx_b_out_stub", 398, 124, 406, 124, 0.20, "dash_dot", COLOR_RETURN),
            text("rx_b_out_text", "salida: RX_B_LOGIC -> GP5", 360, 140, 0.82),
            wire("rx_a_gp4_stub", 70, 78, 84, 78, 0.20, "dash_dot", COLOR_RETURN),
            wire("rx_b_gp5_stub", 70, 86, 84, 86, 0.20, "dash_dot", COLOR_RETURN),
        ]
    )

    # Explicit power/reference nets. They are intentionally drawn as local
    # labelled stubs to avoid long rails crossing the signal path.
    parts.extend(
        [
            wire("pico_3v3_stub", 70, 94, 84, 94, 0.30, "solid", COLOR_POWER),
            label("pico_3v3_stub", "+3V3_LOGIC", 86, 92),
            wire("pico_gnd_stub", 70, 102, 84, 102, 0.30, "dash", COLOR_GND),
            label("pico_gnd_stub", "GND_COMUN", 86, 100),
            wire("j2_gndref_stub", 257, 96, 266, 96, 0.30, "dash", COLOR_GND),
            label("j2_gndref", "GND_REF = GND_COMUN", 260, 101),
            wire("u1_p6_stub", 133, 62, 133, 54, 0.30, "solid", COLOR_POWER),
            label("u1_p6", "U1 V+  +6V_TX", 135, 55),
            wire("u1_n6_stub", 133, 119, 133, 125, 0.30, "solid", COLOR_POWER),
            label("u1_n6", "U1 V-  -6V_TX", 135, 123),
            wire("u2a_vdd_stub", 376, 64, 376, 55, 0.30, "solid", COLOR_POWER),
            label("u2a_vdd", "U2A VDD +3V3", 378, 57),
            wire("u2a_gnd_stub", 376, 88, 376, 96, 0.30, "dash", COLOR_GND),
            label("u2a_gnd", "U2A VSS GND", 378, 94),
            wire("u2b_vdd_stub", 376, 112, 376, 104, 0.30, "solid", COLOR_POWER),
            label("u2b_vdd", "U2B VDD +3V3", 378, 110),
            wire("u2b_gnd_stub", 376, 136, 376, 144, 0.30, "dash", COLOR_GND),
            label("u2b_gnd", "U2B VSS GND", 378, 142),
            wire("a_sense_gnd_stub", 316, 88, 316, 96, 0.30, "dash", COLOR_GND),
            label("a_sense_gnd_label", "R_A2 33k -> GND", 318, 94),
            wire("b_sense_gnd_stub", 316, 136, 316, 144, 0.30, "dash", COLOR_GND),
            label("b_sense_gnd_label", "R_B2 33k -> GND", 318, 142),
        ]
    )

    # Power and threshold section.
    parts.append(rect("power", 18, 174, 170, 248))
    parts.append(text("power_title", "J3 - Fuentes y referencias", 24, 182, 1.45, "left bottom", True))
    power_lines = [
        "+3V3: logica Pico y pull-up comparadores",
        "+5V: opcional para comparadores si aplica",
        "+6V / -6V: alimentacion U1 TX bipolar",
        "GND: referencia comun de banco",
        "C1..C6: 100 nF cerca de U1/U2",
    ]
    for i, line in enumerate(power_lines):
        parts.append(text(f"power_{i}", line, 24, 193 + i * 8, 1.12))

    parts.append(rect("threshold", 190, 174, 305, 248))
    parts.append(text("threshold_title", "Umbral RX", 196, 182, 1.45, "left bottom", True))
    parts.append(rect("r11", 213, 195, 240, 205))
    parts.append(text("r11_text", "R11 12k", 216, 202, 0.95, "left bottom", True))
    parts.append(rect("r12", 213, 222, 240, 232))
    parts.append(text("r12_text", "R12 10k", 216, 229, 0.95, "left bottom", True))
    parts.extend(
        [
            wire("vth_3v3_to_r11", 226, 189, 226, 195, 0.30, "solid", COLOR_POWER),
            label("vth_r11_top", "+3V3_LOGIC", 229, 191),
            wire("r11_to_vth", 226, 205, 226, 214, 0.22, "dot", COLOR_VTH),
            label("vth_node_local", "nodo medio: VTH_COMP ~= 1,50V", 231, 212),
            wire("vth_to_r12", 226, 214, 226, 222, 0.22, "dot", COLOR_VTH),
            wire("r12_to_gnd_down", 226, 232, 226, 239, 0.30, "dash", COLOR_GND),
            wire("r12_to_gnd_bus", 226, 239, 260, 239, 0.30, "dash", COLOR_GND),
            label("vth_r12_gnd", "GND_COMUN", 262, 237),
            wire("vth_local_out", 240, 214, 272, 214, 0.22, "dot", COLOR_VTH),
            label("vth_to_u2_label", "misma red que VTH en U2A/U2B", 244, 220),
            wire("vth_u2a_stub", 348, 88, 355, 88, 0.22, "dot", COLOR_VTH),
            label("vth_u2a_label", "VTH_COMP", 329, 86),
            wire("vth_u2b_stub", 348, 136, 355, 136, 0.22, "dot", COLOR_VTH),
            label("vth_u2b_label", "VTH_COMP", 329, 134),
        ]
    )
    parts.append(text("threshold_note", "Agregar histeresis despues de medir ruido real.", 196, 242, 1.00))

    parts.append(rect("truth", 325, 174, 407, 248))
    parts.append(text("truth_title", "Tabla funcional", 331, 182, 1.45, "left bottom", True))
    truth_lines = [
        "TX_A=1 TX_B=0 -> A=+5 B=-5 -> RX_A=1 RX_B=0",
        "TX_A=0 TX_B=1 -> A=-5 B=+5 -> RX_A=0 RX_B=1",
        "TX_A=0 TX_B=0 -> A=0 B=0 -> RX_A=0 RX_B=0",
        "A-B esperado: +10 V / 0 V / -10 V",
    ]
    for i, line in enumerate(truth_lines):
        parts.append(text(f"truth_{i}", line, 331, 195 + i * 10, 0.95))

    parts.append(text("note_parts", "Valores iniciales para prototipo. Validar flancos, niveles y protecciones con osciloscopio antes de conectar a firmware.", 18, 276, 1.15, "left bottom", True))

    return f"""(kicad_sch
	(version 20250610)
	(generator "eeschema")
	(generator_version "10.0")
	(uuid "{uid('schematic-root')}")
	(paper "A3")
	(title_block
		(title "Front-end electrico ARINC 429 - banco")
		(date "2026-07-01")
		(rev "A")
		(company "Proyecto PAMPA")
	)
	(lib_symbols)
{''.join(parts)}
	(sheet_instances
		(path "/"
			(page "1")
		)
	)
	(symbol_instances)
)"""


def build_project_file() -> str:
    data = {
        "meta": {
            "filename": "frontend_arinc429_lab.kicad_pro",
            "version": 1,
        },
        "project": {
            "files": [],
            "text_variables": {},
        },
        "schematic": {
            "legacy_lib_dir": "",
            "legacy_lib_list": [],
            "meta": {"version": 1},
            "page_layout_descr_file": "",
            "plot_directory": "../../docs/pdf",
        },
        "sheets": [[uid("schematic-root"), "frontend_arinc429_lab"]],
    }
    return json.dumps(data, indent=2)


def build_readme() -> str:
    return """# Front-end electrico ARINC 429 - banco

Proyecto KiCad inicial para revisar el acondicionamiento electrico de laboratorio.

## Archivos

- `frontend_arinc429_lab.kicad_pro`: proyecto KiCad.
- `frontend_arinc429_lab.kicad_sch`: esquematico funcional.

## Alcance

Este esquema es una base de revision y prototipo. No es todavia una PCB final ni un
transceptor certificable para campo.

## Conexion funcional

- `GP2`: TX_A_LOGIC.
- `GP3`: TX_B_LOGIC.
- `GP4`: RX_A_LOGIC.
- `GP5`: RX_B_LOGIC.
- `LINE_A/LINE_B`: par diferencial de banco.
- `+6V/-6V`: alimentacion del driver bipolar.
- `+3V3`: logica Pico y salidas de comparadores.
- `GND`: referencia comun del banco.

## Conexiones explicitas del esquematico

| Net | Desde | Hasta | Funcion |
| --- | --- | --- | --- |
| `TX_A_LOGIC` | Pico `GP2` | entrada `U1A` | dato logico lado A antes de acondicionar |
| `TX_B_LOGIC` | Pico `GP3` | entrada `U1A` | dato logico lado B antes de acondicionar |
| `LINE_A` | salida `U1A` | `J2 pin 1` y entrada `A_SENSE` | conductor A bipolar |
| `LINE_B` | salida `U1B` | `J2 pin 2` y entrada `B_SENSE` | conductor B bipolar |
| `GND_REF` | `J2 pin 3` | `GND_COMUN` | referencia de banco para medicion |
| `A_SENSE` | divisor/proteccion de `LINE_A` | entrada de `U2A` | senal reducida para comparar |
| `B_SENSE` | divisor/proteccion de `LINE_B` | entrada de `U2B` | senal reducida para comparar |
| `VTH_COMP` | divisor `R11/R12` | entradas de referencia de `U2A/U2B` | umbral de decision RX |
| `RX_A_LOGIC` | salida `U2A` | Pico `GP4` | senal recuperada A, ya en 0..3,3 V |
| `RX_B_LOGIC` | salida `U2B` | Pico `GP5` | senal recuperada B, ya en 0..3,3 V |
| `+3V3_LOGIC` | Pico/fuente 3,3 V | U2, pull-ups, divisor de umbral | alimentacion logica |
| `+6V_TX` | fuente bipolar | `U1 V+` | alimentacion positiva del TX analogico |
| `-6V_TX` | fuente bipolar | `U1 V-` | alimentacion negativa del TX analogico |
| `GND_COMUN` | Pico/fuente | J2, RX, divisor de umbral | referencia comun |

## Convencion visual

| Tipo de linea | Convencion |
| --- | --- |
| Senales y linea A/B | verde, trazo continuo |
| Alimentacion | rojo, trazo continuo mas grueso |
| GND y referencias | azul, trazo punteado |
| Umbral `VTH_COMP` | violeta, trazo punteado fino |
| Retorno RX hacia Pico | naranja, trazo punto-raya |

`VTH_COMP` no va a `J2`. `J2` solo expone la linea de banco `LINE_A`,
`LINE_B` y `GND_REF`. El umbral `VTH_COMP` es interno del receptor RX y entra a
los comparadores `U2A/U2B`.

`R11` y `R12` estan conectadas en serie como divisor resistivo:

```text
+3V3_LOGIC -> R11 -> VTH_COMP -> R12 -> GND_COMUN
```

La linea violeta entre `R11` y `R12` es el nodo medio del divisor. Las lineas
violetas que aparecen en `U2A` y `U2B` son esa misma red `VTH_COMP`; representan
la referencia de comparacion, no una conexion hacia la linea ARINC.

Las alimentaciones y referencias se muestran como etiquetas de red locales para
evitar cruces innecesarios. En KiCad, dos puntos con la misma etiqueta pertenecen
a la misma conexion electrica aunque no exista un cable dibujado entre ambos.

## Revision de conexiones criticas

- `U2A VDD` queda en `+3V3_LOGIC`.
- `U2A VSS` queda en `GND_COMUN`.
- `U2B VDD` queda en `+3V3_LOGIC`.
- `U2B VSS` queda en `GND_COMUN`.
- `J2 pin 3 GND_REF` queda unido a `GND_COMUN`.
- Los divisores `A_SENSE` y `B_SENSE` cierran por `GND_COMUN`.
- El divisor `R11/R12` genera `VTH_COMP` desde `+3V3_LOGIC` hacia `GND_COMUN`.
- `VTH_COMP` entra como referencia a `U2A/U2B`; no sale al conector de linea.
- `RX_A_LOGIC` vuelve a `GP4` y `RX_B_LOGIC` vuelve a `GP5`.
- `U1` usa alimentacion bipolar separada: `+6V_TX` y `-6V_TX`.

## Validacion minima

1. Medir `LINE_A` y `LINE_B` contra GND.
2. Medir `MATH = LINE_A - LINE_B`.
3. Confirmar aproximadamente `+10 V`, `0 V`, `-10 V` diferencial.
4. Confirmar que `RX_A_LOGIC` y `RX_B_LOGIC` nunca excedan `0..3,3 V`.

## Explicacion tecnica

El esquema representa un acondicionador electrico entre la logica de una
Raspberry Pi Pico y una linea diferencial tipo ARINC 429 de laboratorio.

La Pico entrega dos senales logicas: `TX_A_LOGIC` por `GP2` y `TX_B_LOGIC` por
`GP3`. Esas senales entran al bloque `TX bipolar`. La primera etapa, `U1A`,
esta planteada como un amplificador diferencial con ganancia 1,5:

```text
LINE_A = 1,5 * (TX_A_LOGIC - TX_B_LOGIC)
```

La segunda etapa, `U1B`, invierte `LINE_A`:

```text
LINE_B = -LINE_A
```

De esa manera, cuando `GP2` esta alto y `GP3` bajo, la linea queda
aproximadamente `LINE_A=+5 V` y `LINE_B=-5 V`. Cuando se invierten los GPIO, la
polaridad diferencial tambien se invierte. Cuando ambos GPIO estan en cero, el
resultado esperado es `LINE_A=0 V` y `LINE_B=0 V`.

El conector `J2` expone el par `LINE_A/LINE_B`. En ese punto se debe medir con
osciloscopio tanto cada linea contra masa como la resta `MATH = LINE_A - LINE_B`.
El diferencial esperado es `+10 V`, `0 V` y `-10 V`.

El bloque `RX protegido y comparadores` toma `LINE_A` y `LINE_B` y no las lleva
directamente a la Pico. Primero cada linea pasa por un divisor/proteccion
(`A_SENSE` y `B_SENSE`). Luego `U2A` y `U2B` comparan esos nodos contra
`VTH_COMP`, aproximadamente `1,50 V`.

Las salidas de los comparadores vuelven a la Pico como senales logicas seguras:

```text
U2A -> RX_A_LOGIC -> GP4
U2B -> RX_B_LOGIC -> GP5
```

Esas salidas deben estar siempre entre `0 V` y `3,3 V`. Si el comparador elegido
es `LM393/LM393B`, la salida es open collector y necesita pull-up a `3V3`, no a
`5V`, para no danar la Pico.

## Explicacion simple

La Pico sola no puede sacar una senal ARINC real porque sus pines solo manejan
`0 V` y `3,3 V`. Entonces el circuito hace de traductor.

Primero la Pico dice que quiere transmitir usando dos pines:

- `GP2` representa el lado A.
- `GP3` representa el lado B.

Despues el bloque TX agranda esa senal y la convierte en una senal bipolar:

- si A esta activo, el cable A queda en `+5 V` y el cable B en `-5 V`;
- si B esta activo, el cable A queda en `-5 V` y el cable B en `+5 V`;
- si no hay pulso, ambos cables quedan cerca de `0 V`.

Eso es lo que se quiere ver en el osciloscopio como ARINC de laboratorio.

Luego viene el bloque RX. Ese bloque hace el trabajo inverso: mira cual de los
dos cables esta positivo, baja la senal a un valor seguro y la vuelve a entregar
a la Pico como `0 V` o `3,3 V`.

En criollo: el TX convierte la senal chica de la Pico en una senal grande tipo
ARINC, y el RX convierte esa senal grande otra vez en una senal chica que la Pico
puede leer sin quemarse.

La parte importante que debe quedar clara es esta:

```text
Linea ARINC-like -> RX -> comparadores -> RX_A_LOGIC/RX_B_LOGIC -> GP4/GP5
```

Ese es el regreso hacia la Pico.
"""


def main() -> None:
    PROJECT_DIR.mkdir(parents=True, exist_ok=True)
    SCH_PATH.write_text(build_schematic(), encoding="utf-8", newline="\n")
    PRO_PATH.write_text(build_project_file(), encoding="utf-8", newline="\n")
    README_PATH.write_text(build_readme(), encoding="utf-8", newline="\n")
    print(SCH_PATH)
    print(PRO_PATH)
    print(README_PATH)


if __name__ == "__main__":
    main()
