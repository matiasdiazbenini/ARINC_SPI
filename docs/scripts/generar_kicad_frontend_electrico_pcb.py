from __future__ import annotations

import uuid
from dataclasses import dataclass
from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]
PROJECT_DIR = ROOT / "ELECTRICO" / "frontend_arinc429_lab"
PCB_PATH = PROJECT_DIR / "frontend_arinc429_lab.kicad_pcb"
SVG_PATH = PROJECT_DIR / "exports" / "frontend_arinc429_lab_pcb.svg"

NAMESPACE = uuid.UUID("93cb29ea-eac2-45f0-b9fb-0f20b84c16b2")

NETS = [
    "",
    "GND_COMUN",
    "+3V3_LOGIC",
    "+6V_TX",
    "-6V_TX",
    "TX_A_LOGIC",
    "TX_B_LOGIC",
    "RX_A_LOGIC",
    "RX_B_LOGIC",
    "LINE_A",
    "LINE_B",
    "A_SENSE",
    "B_SENSE",
    "VTH_COMP",
    "U1A_POS",
    "U1A_NEG",
    "U1B_NEG",
]

NET_ID = {name: i for i, name in enumerate(NETS)}


def uid(name: str) -> str:
    return str(uuid.uuid5(NAMESPACE, name))


def q(text: str) -> str:
    return text.replace("\\", "\\\\").replace('"', '\\"')


def layer_block() -> str:
    return """
	(layers
		(0 "F.Cu" signal)
		(31 "B.Cu" signal)
		(32 "B.Adhes" user)
		(33 "F.Adhes" user)
		(34 "B.Paste" user)
		(35 "F.Paste" user)
		(36 "B.SilkS" user)
		(37 "F.SilkS" user)
		(38 "B.Mask" user)
		(39 "F.Mask" user)
		(40 "Dwgs.User" user)
		(41 "Cmts.User" user)
		(42 "Eco1.User" user)
		(43 "Eco2.User" user)
		(44 "Edge.Cuts" user)
		(45 "Margin" user)
		(46 "B.CrtYd" user)
		(47 "F.CrtYd" user)
		(48 "B.Fab" user)
		(49 "F.Fab" user)
	)
"""


def property_line(key: str, value: str, x: float, y: float, hide: bool = False) -> str:
    hide_line = "\n\t\t(hide yes)" if hide else ""
    return f"""
		(property "{q(key)}" "{q(value)}"
			(at {x:.2f} {y:.2f} 0)
			(layer "F.SilkS"){hide_line}
			(uuid "{uid(key + value + str(x) + str(y))}")
			(effects (font (size 1 1) (thickness 0.15)))
		)"""


def fp_text(kind: str, value: str, x: float, y: float, layer: str = "F.SilkS") -> str:
    return f"""
		(fp_text {kind} "{q(value)}"
			(at {x:.2f} {y:.2f} 0)
			(layer "{layer}")
			(effects (font (size 1 1) (thickness 0.15)))
		)"""


def fp_line(x1: float, y1: float, x2: float, y2: float, layer: str = "F.SilkS", width: float = 0.12) -> str:
    return f"""
		(fp_line
			(start {x1:.2f} {y1:.2f})
			(end {x2:.2f} {y2:.2f})
			(stroke (width {width}) (type solid))
			(layer "{layer}")
			(uuid "{uid('fp_line' + str((x1, y1, x2, y2, layer, width)))}")
		)"""


def pad(num: str, x: float, y: float, net: str, shape: str = "circle", size: float = 1.7, drill: float = 0.85) -> str:
    net_expr = ""
    if net:
        net_expr = f' (net {NET_ID[net]} "{q(net)}")'
    return f"""
		(pad "{num}" thru_hole {shape}
			(at {x:.2f} {y:.2f})
			(size {size:.2f} {size:.2f})
			(drill {drill:.2f})
			(layers "*.Cu" "*.Mask"){net_expr}
		)"""


def footprint(ref: str, value: str, x: float, y: float, body: str, footprint_name: str = "PAMPA:GENERIC") -> str:
    return f"""
	(footprint "{footprint_name}"
		(layer "F.Cu")
		(uuid "{uid('fp-' + ref)}")
		(at {x:.2f} {y:.2f})
		(property "Reference" "{q(ref)}"
			(at 0 -3.2 0)
			(layer "F.SilkS")
			(uuid "{uid('ref-' + ref)}")
			(effects (font (size 1 1) (thickness 0.15)))
		)
		(property "Value" "{q(value)}"
			(at 0 3.2 0)
			(layer "F.Fab")
			(uuid "{uid('val-' + ref)}")
			(effects (font (size 1 1) (thickness 0.15)))
		)
		(property "Footprint" "{q(footprint_name)}"
			(at 0 0 0)
			(layer "F.Fab")
			(hide yes)
			(uuid "{uid('footprint-' + ref)}")
			(effects (font (size 1 1) (thickness 0.15)))
		)
{body}
	)"""


def dip8(ref: str, value: str, x: float, y: float, nets: dict[str, str]) -> str:
    pads = [
        ("1", -3.81, -3.81),
        ("2", -3.81, -1.27),
        ("3", -3.81, 1.27),
        ("4", -3.81, 3.81),
        ("5", 3.81, 3.81),
        ("6", 3.81, 1.27),
        ("7", 3.81, -1.27),
        ("8", 3.81, -3.81),
    ]
    body = [
        fp_text("user", value, 0, 0, "F.SilkS"),
        fp_line(-5.1, -5.2, 5.1, -5.2),
        fp_line(5.1, -5.2, 5.1, 5.2),
        fp_line(5.1, 5.2, -5.1, 5.2),
        fp_line(-5.1, 5.2, -5.1, -5.2),
        fp_line(-4.0, -5.2, -2.5, -6.2),
    ]
    body.extend(pad(num, px, py, nets.get(num, ""), "rect" if num == "1" else "circle") for num, px, py in pads)
    return footprint(ref, value, x, y, "".join(body), "Package_DIP:DIP-8_W7.62mm")


def resistor(ref: str, value: str, x: float, y: float, net1: str, net2: str, vertical: bool = False) -> str:
    if vertical:
        pads = pad("1", 0, -3.81, net1, "rect") + pad("2", 0, 3.81, net2)
        box = [fp_line(-1.2, -2.2, 1.2, -2.2), fp_line(1.2, -2.2, 1.2, 2.2), fp_line(1.2, 2.2, -1.2, 2.2), fp_line(-1.2, 2.2, -1.2, -2.2)]
    else:
        pads = pad("1", -3.81, 0, net1, "rect") + pad("2", 3.81, 0, net2)
        box = [fp_line(-2.2, -1.2, 2.2, -1.2), fp_line(2.2, -1.2, 2.2, 1.2), fp_line(2.2, 1.2, -2.2, 1.2), fp_line(-2.2, 1.2, -2.2, -1.2)]
    return footprint(ref, value, x, y, fp_text("user", value, 0, -1.8) + "".join(box) + pads, "Resistor_THT:R_Axial_DIN0207_L6.3mm_D2.5mm_P7.62mm_Horizontal")


def capacitor(ref: str, value: str, x: float, y: float, net1: str, net2: str) -> str:
    body = [
        fp_line(-1.0, -2.0, -1.0, 2.0),
        fp_line(1.0, -2.0, 1.0, 2.0),
        pad("1", -1.27, 0, net1, "rect"),
        pad("2", 1.27, 0, net2),
    ]
    return footprint(ref, value, x, y, fp_text("user", value, 0, -2.8) + "".join(body), "Capacitor_THT:C_Disc_D3.0mm_W1.6mm_P2.50mm")


def diode(ref: str, value: str, x: float, y: float, net1: str, net2: str, vertical: bool = False) -> str:
    if vertical:
        pads = pad("1", 0, -2.54, net1, "rect") + pad("2", 0, 2.54, net2)
        line = fp_line(-1.2, 0, 1.2, 0)
    else:
        pads = pad("1", -2.54, 0, net1, "rect") + pad("2", 2.54, 0, net2)
        line = fp_line(0, -1.2, 0, 1.2)
    return footprint(ref, value, x, y, fp_text("user", value, 0, -2.3) + line + pads, "Diode_THT:D_DO-35_SOD27_P7.62mm_Horizontal")


def pin_header(ref: str, value: str, x: float, y: float, pin_nets: list[tuple[str, str]], pitch: float = 2.54) -> str:
    body = [fp_text("user", value, 0, -3.0)]
    height = (len(pin_nets) - 1) * pitch
    body.extend([
        fp_line(-1.4, -1.4, 1.4, -1.4),
        fp_line(1.4, -1.4, 1.4, height + 1.4),
        fp_line(1.4, height + 1.4, -1.4, height + 1.4),
        fp_line(-1.4, height + 1.4, -1.4, -1.4),
    ])
    for idx, (num, net) in enumerate(pin_nets):
        body.append(pad(num, 0, idx * pitch, net, "rect" if idx == 0 else "circle"))
    return footprint(ref, value, x, y, "".join(body), f"Connector_PinHeader_2.54mm:PinHeader_1x{len(pin_nets):02d}_P2.54mm_Vertical")


def terminal_block(ref: str, value: str, x: float, y: float, pin_nets: list[tuple[str, str]]) -> str:
    pitch = 5.08
    body = [fp_text("user", value, 0, -5.0)]
    width = (len(pin_nets) - 1) * pitch + 5.0
    body.extend([
        fp_line(-2.5, -3.2, width - 2.5, -3.2),
        fp_line(width - 2.5, -3.2, width - 2.5, 3.2),
        fp_line(width - 2.5, 3.2, -2.5, 3.2),
        fp_line(-2.5, 3.2, -2.5, -3.2),
    ])
    for idx, (num, net) in enumerate(pin_nets):
        body.append(pad(num, idx * pitch, 0, net, "rect" if idx == 0 else "circle", 2.2, 1.1))
    return footprint(ref, value, x, y, "".join(body), f"TerminalBlock:TerminalBlock_1x{len(pin_nets):02d}_P5.08mm")


def testpoint(ref: str, value: str, x: float, y: float, net: str) -> str:
    body = fp_text("user", value, 0, -2.2) + pad("1", 0, 0, net, "circle", 2.0, 1.0)
    return footprint(ref, value, x, y, body, "TestPoint:TestPoint_THTPad_D2.0mm_Drill1.0mm")


def gr_text(text: str, x: float, y: float, size: float = 1.3, layer: str = "F.SilkS") -> str:
    return f"""
	(gr_text "{q(text)}"
		(at {x:.2f} {y:.2f} 0)
		(layer "{layer}")
		(uuid "{uid('gr-' + text + str(x) + str(y))}")
		(effects (font (size {size:.2f} {size:.2f}) (thickness 0.15)) (justify left))
	)"""


def gr_line(x1: float, y1: float, x2: float, y2: float, layer: str = "Edge.Cuts", width: float = 0.10) -> str:
    return f"""
	(gr_line
		(start {x1:.2f} {y1:.2f})
		(end {x2:.2f} {y2:.2f})
		(stroke (width {width}) (type solid))
		(layer "{layer}")
		(uuid "{uid('gr-line' + str((x1, y1, x2, y2, layer)))}")
	)"""


def segment(net: str, x1: float, y1: float, x2: float, y2: float, width: float = 0.45, layer: str = "F.Cu") -> str:
    return f"""
	(segment
		(start {x1:.2f} {y1:.2f})
		(end {x2:.2f} {y2:.2f})
		(width {width:.2f})
		(layer "{layer}")
		(net {NET_ID[net]})
		(uuid "{uid('seg' + net + str((x1, y1, x2, y2, width, layer)))}")
	)"""


@dataclass(frozen=True)
class SvgItem:
    ref: str
    x: float
    y: float
    w: float
    h: float
    color: str
    label: str


def build_pcb() -> str:
    nets = "\n".join(f'\t(net {i} "{q(name)}")' for i, name in enumerate(NETS))
    fps: list[str] = []

    # Revision B: placement deliberately spacious and unrouted. The previous
    # sketch included rough tracks that crossed footprints and made the board
    # hard to review. For this stage the useful deliverable is a clean KiCad
    # placement with correct nets/footprints; final routing must be done after
    # confirming real packages and mechanical constraints.
    fps.append(pin_header("J1", "Pico logic I/O", 35, 55, [
        ("1", "TX_A_LOGIC"), ("2", "TX_B_LOGIC"), ("3", "RX_A_LOGIC"),
        ("4", "RX_B_LOGIC"), ("5", "+3V3_LOGIC"), ("6", "GND_COMUN"),
    ]))
    fps.append(terminal_block("J3", "+3V3/+6/-6/GND", 72, 30, [
        ("1", "+3V3_LOGIC"), ("2", "+6V_TX"), ("3", "-6V_TX"), ("4", "GND_COMUN"),
    ]))
    fps.append(dip8("U1", "TL072/TL082/dual op-amp", 84, 70, {
        "1": "LINE_A", "2": "U1A_NEG", "3": "U1A_POS", "4": "-6V_TX",
        "5": "GND_COMUN", "6": "U1B_NEG", "7": "LINE_B", "8": "+6V_TX",
    }))
    fps.append(dip8("U2", "MCP6562/LM393 dual comp", 140, 72, {
        "1": "RX_A_LOGIC", "2": "VTH_COMP", "3": "A_SENSE", "4": "GND_COMUN",
        "5": "B_SENSE", "6": "VTH_COMP", "7": "RX_B_LOGIC", "8": "+3V3_LOGIC",
    }))
    fps.append(terminal_block("J2", "LINE_A/LINE_B/GND", 174, 54, [
        ("1", "LINE_A"), ("2", "LINE_B"), ("3", "GND_COMUN"),
    ]))

    fps.extend([
        resistor("R1", "20k", 55, 54, "TX_A_LOGIC", "U1A_POS"),
        resistor("R2", "30k", 55, 67, "U1A_POS", "GND_COMUN"),
        resistor("R3", "20k", 55, 80, "TX_B_LOGIC", "U1A_NEG"),
        resistor("R4", "30k", 84, 50, "LINE_A", "U1A_NEG"),
        resistor("R5", "10k", 108, 63, "LINE_A", "U1B_NEG"),
        resistor("R6", "10k", 108, 78, "LINE_B", "U1B_NEG"),
        resistor("R7", "22k", 154, 45, "LINE_A", "A_SENSE"),
        resistor("R8", "33k", 128, 45, "A_SENSE", "GND_COMUN", vertical=True),
        resistor("R9", "22k", 154, 94, "LINE_B", "B_SENSE"),
        resistor("R10", "33k", 128, 94, "B_SENSE", "GND_COMUN", vertical=True),
        resistor("R11", "12k", 124, 108, "+3V3_LOGIC", "VTH_COMP", vertical=True),
        resistor("R12", "10k", 136, 108, "VTH_COMP", "GND_COMUN", vertical=True),
        resistor("R13", "4k7/DNP", 118, 56, "+3V3_LOGIC", "RX_A_LOGIC"),
        resistor("R14", "4k7/DNP", 118, 64, "+3V3_LOGIC", "RX_B_LOGIC"),
        capacitor("C1", "100nF U1+", 74, 48, "+6V_TX", "GND_COMUN"),
        capacitor("C2", "100nF U1-", 74, 54, "-6V_TX", "GND_COMUN"),
        capacitor("C3", "100nF U2", 132, 52, "+3V3_LOGIC", "GND_COMUN"),
        capacitor("C4", "10uF 3V3", 100, 32, "+3V3_LOGIC", "GND_COMUN"),
        capacitor("C5", "10uF +6V", 100, 38, "+6V_TX", "GND_COMUN"),
        capacitor("C6", "10uF -6V", 100, 44, "-6V_TX", "GND_COMUN"),
        diode("D1", "A clamp hi", 166, 66, "A_SENSE", "+3V3_LOGIC"),
        diode("D2", "A clamp lo", 166, 73, "GND_COMUN", "A_SENSE"),
        diode("D3", "B clamp hi", 166, 86, "B_SENSE", "+3V3_LOGIC"),
        diode("D4", "B clamp lo", 166, 93, "GND_COMUN", "B_SENSE"),
        diode("D5", "TVS A-B", 184, 76, "LINE_A", "LINE_B", vertical=True),
        testpoint("TP1", "TP_LINE_A", 194, 44, "LINE_A"),
        testpoint("TP2", "TP_LINE_B", 194, 54, "LINE_B"),
        testpoint("TP3", "TP_GND", 194, 64, "GND_COMUN"),
        testpoint("TP4", "TP_VTH", 148, 108, "VTH_COMP"),
    ])

    graphics = [
        gr_line(20, 20, 205, 20),
        gr_line(205, 20, 205, 125),
        gr_line(205, 125, 20, 125),
        gr_line(20, 125, 20, 20),
        gr_line(42, 38, 118, 38, "F.SilkS", 0.12),
        gr_line(118, 38, 118, 98, "F.SilkS", 0.12),
        gr_line(118, 98, 42, 98, "F.SilkS", 0.12),
        gr_line(42, 98, 42, 38, "F.SilkS", 0.12),
        gr_line(120, 38, 198, 38, "F.SilkS", 0.12),
        gr_line(198, 38, 198, 101, "F.SilkS", 0.12),
        gr_line(198, 101, 120, 101, "F.SilkS", 0.12),
        gr_line(120, 101, 120, 38, "F.SilkS", 0.12),
        gr_text("PAMPA ARINC 429 - front-end electrico preliminar Rev B", 24, 116, 1.6),
        gr_text("Placement limpio sin ruteo final: revisar footprints reales, DRC y routing manual.", 24, 121, 1.05),
        gr_text("TX bipolar", 46, 43, 1.2),
        gr_text("RX protegido / comparadores", 124, 43, 1.2),
        gr_text("J2 = LINE_A / LINE_B / GND_REF", 166, 38, 1.0),
        gr_text("No conectar a ARINC real de campo", 24, 112, 1.0),
    ]

    tracks: list[str] = []

    return f"""(kicad_pcb
	(version 20240108)
	(generator "codex")
	(generator_version "1.0")
	(general
		(thickness 1.6)
	)
	(paper "A4")
{layer_block()}
	(setup
		(pad_to_mask_clearance 0)
		(allow_soldermask_bridges_in_footprints no)
	)
{nets}
{"".join(footprint_text for footprint_text in fps)}
{"".join(graphics)}
{"".join(tracks)}
)
"""


def build_svg() -> str:
    width, height = 1400, 900
    scale = 6
    items = [
        SvgItem("J1", 34, 73, 18, 44, "#dbeafe", "Pico I/O"),
        SvgItem("J3", 76, 34, 36, 16, "#fee2e2", "Fuentes"),
        SvgItem("Pasivos TX", 65, 82, 28, 42, "#f8fafc", "Redes R/C TX"),
        SvgItem("U1", 100, 82, 34, 42, "#fff7ed", "U1 TX bipolar"),
        SvgItem("U2", 142, 82, 34, 42, "#eff6ff", "U2 RX comp."),
        SvgItem("Proteccion", 180, 82, 32, 42, "#f0fdf4", "Divisores y clamps"),
        SvgItem("J2", 180, 36, 34, 16, "#dcfce7", "Linea A/B/GND"),
        SvgItem("TP", 196, 116, 14, 14, "#fef9c3", "TP"),
    ]

    def sx(v: float) -> float:
        return v * scale

    def sy(v: float) -> float:
        return v * scale

    rects = []
    for it in items:
        rects.append(
            f'<rect x="{sx(it.x - it.w / 2):.1f}" y="{sy(it.y - it.h / 2):.1f}" '
            f'width="{sx(it.w):.1f}" height="{sy(it.h):.1f}" rx="6" fill="{it.color}" stroke="#334155" stroke-width="2"/>'
        )
        rects.append(
            f'<text x="{sx(it.x):.1f}" y="{sy(it.y):.1f}" text-anchor="middle" '
            f'font-family="Arial" font-size="14" font-weight="700" fill="#0f172a">{q(it.label)}</text>'
        )

    wires = [
        (34, 73, 65, 82, "#16a34a", "TX_A/TX_B"),
        (65, 82, 100, 82, "#16a34a", "red TX"),
        (100, 82, 180, 36, "#16a34a", "LINE_A/B"),
        (180, 36, 180, 82, "#16a34a", "sense"),
        (180, 82, 142, 82, "#16a34a", "A/B sense"),
        (142, 82, 34, 86, "#f97316", "RX_A/RX_B"),
        (76, 34, 100, 82, "#dc2626", "+/-6V TX"),
        (76, 34, 142, 82, "#dc2626", "+3V3/GND RX"),
    ]
    wire_svg = []
    for x1, y1, x2, y2, color, label in wires:
        wire_svg.append(f'<line x1="{sx(x1):.1f}" y1="{sy(y1):.1f}" x2="{sx(x2):.1f}" y2="{sy(y2):.1f}" stroke="{color}" stroke-width="4" marker-end="url(#arrow)"/>')
        wire_svg.append(f'<text x="{sx((x1+x2)/2):.1f}" y="{sy((y1+y2)/2)-6:.1f}" text-anchor="middle" font-family="Arial" font-size="12" fill="{color}">{q(label)}</text>')

    return f"""<svg xmlns="http://www.w3.org/2000/svg" width="{width}" height="{height}" viewBox="0 0 {width} {height}">
<defs>
  <marker id="arrow" markerWidth="10" markerHeight="10" refX="8" refY="3" orient="auto">
    <path d="M0,0 L0,6 L9,3 z" fill="#334155"/>
  </marker>
</defs>
<rect x="80" y="70" width="1240" height="760" fill="#ffffff" stroke="#111827" stroke-width="4"/>
<text x="110" y="110" font-family="Arial" font-size="26" font-weight="700" fill="#111827">PCB preliminar front-end electrico ARINC 429 de laboratorio - Rev B</text>
<text x="110" y="140" font-family="Arial" font-size="16" fill="#334155">Placement limpio y sin ruteo final: KiCad muestra airwires para ruteo manual despues de elegir encapsulados reales.</text>
{"".join(wire_svg)}
{"".join(rects)}
<text x="110" y="800" font-family="Arial" font-size="15" fill="#475569">Nota: no es PCB final de campo. Usa DIP8/THT para banco, validacion y reemplazo facil de componentes.</text>
</svg>
"""


def main() -> None:
    PROJECT_DIR.mkdir(parents=True, exist_ok=True)
    SVG_PATH.parent.mkdir(parents=True, exist_ok=True)
    PCB_PATH.write_text(build_pcb(), encoding="utf-8")
    SVG_PATH.write_text(build_svg(), encoding="utf-8")
    print(PCB_PATH)
    print(SVG_PATH)


if __name__ == "__main__":
    main()
