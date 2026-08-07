from __future__ import annotations

import json
import uuid
from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]
PROJECT_DIR = ROOT / "ELECTRICO" / "frontend_arinc429_lab"
SCH_PATH = PROJECT_DIR / "frontend_arinc429_tl082_circuit.kicad_sch"
PRO_PATH = PROJECT_DIR / "frontend_arinc429_tl082_circuit.kicad_pro"

NAMESPACE = uuid.UUID("d62fc466-cc43-4a85-a7c4-06f28a0c1c6e")

GREEN = (0, 120, 0, 255)
BLUE = (0, 90, 160, 255)
RED = (180, 20, 20, 255)
VIOLET = (140, 60, 180, 255)
GRAY = (80, 80, 80, 255)


def uid(name: str) -> str:
    return str(uuid.uuid5(NAMESPACE, name))


def stroke(width: float = 0.15, kind: str = "solid", color: tuple[int, int, int, int] = GREEN) -> str:
    return f"""
		(stroke
			(width {width})
			(type {kind})
			(color {color[0]} {color[1]} {color[2]} {color[3]})
		)"""


def effects(size: float = 1.0, bold: bool = False, justify: str = "left bottom") -> str:
    bold_line = "\n				(bold yes)" if bold else ""
    return f"""
		(effects
			(font
				(size {size} {size}){bold_line}
			)
			(justify {justify})
		)"""


def text(item_id: str, value: str, x: float, y: float, size: float = 1.0, bold: bool = False) -> str:
    escaped = value.replace("\\", "\\\\").replace('"', '\\"')
    return f"""
	(text "{escaped}"
		(exclude_from_sim no)
		(at {x:.2f} {y:.2f} 0){effects(size, bold)}
		(uuid "{uid('text-' + item_id)}")
	)"""


def label(
    item_id: str,
    value: str,
    x: float,
    y: float,
    size: float = 0.85,
    justify: str = "left bottom",
) -> str:
    escaped = value.replace('"', '\\"')
    return f"""
	(label "{escaped}"
		(at {x:.2f} {y:.2f} 0){effects(size, justify=justify)}
		(uuid "{uid('label-' + item_id)}")
	)"""


def wire(
    item_id: str,
    x1: float,
    y1: float,
    x2: float,
    y2: float,
    width: float = 0.15,
    kind: str = "solid",
    color: tuple[int, int, int, int] = GREEN,
) -> str:
    return f"""
	(wire
		(pts
			(xy {x1:.2f} {y1:.2f}) (xy {x2:.2f} {y2:.2f})
		){stroke(width, kind, color)}
		(uuid "{uid('wire-' + item_id)}")
	)"""


def junction(item_id: str, x: float, y: float, color: tuple[int, int, int, int] = GREEN) -> str:
    return f"""
	(junction
		(at {x:.2f} {y:.2f})
		(diameter 0)
		(color {color[0]} {color[1]} {color[2]} {color[3]})
		(uuid "{uid('junction-' + item_id)}")
	)"""


def resistor_h(item_id: str, name: str, value: str, x1: float, y: float, x2: float, color: tuple[int, int, int, int] = GREEN) -> str:
    body_l = x1 + 4
    body_r = x2 - 4
    mid = (x1 + x2) / 2
    return "".join(
        [
            wire(f"{item_id}-lead-l", x1, y, body_l, y, 0.15, "solid", color),
            wire(f"{item_id}-top", body_l, y - 2.5, body_r, y - 2.5, 0.15, "solid", color),
            wire(f"{item_id}-right", body_r, y - 2.5, body_r, y + 2.5, 0.15, "solid", color),
            wire(f"{item_id}-bottom", body_r, y + 2.5, body_l, y + 2.5, 0.15, "solid", color),
            wire(f"{item_id}-left", body_l, y + 2.5, body_l, y - 2.5, 0.15, "solid", color),
            wire(f"{item_id}-lead-r", body_r, y, x2, y, 0.15, "solid", color),
            text(f"{item_id}-name", name, mid - 4.0, y - 4.3, 0.75, True),
            text(f"{item_id}-val", value, mid - 4.0, y + 5.0, 0.72),
        ]
    )


def resistor_v(item_id: str, name: str, value: str, x: float, y1: float, y2: float, color: tuple[int, int, int, int] = GREEN) -> str:
    body_t = y1 + 4
    body_b = y2 - 4
    mid = (y1 + y2) / 2
    return "".join(
        [
            wire(f"{item_id}-lead-t", x, y1, x, body_t, 0.15, "solid", color),
            wire(f"{item_id}-left", x - 2.5, body_t, x - 2.5, body_b, 0.15, "solid", color),
            wire(f"{item_id}-bottom", x - 2.5, body_b, x + 2.5, body_b, 0.15, "solid", color),
            wire(f"{item_id}-right", x + 2.5, body_b, x + 2.5, body_t, 0.15, "solid", color),
            wire(f"{item_id}-top", x + 2.5, body_t, x - 2.5, body_t, 0.15, "solid", color),
            wire(f"{item_id}-lead-b", x, body_b, x, y2, 0.15, "solid", color),
            text(f"{item_id}-name", name, x + 3.3, mid - 1.2, 0.75, True),
            text(f"{item_id}-val", value, x + 3.3, mid + 2.8, 0.72),
        ]
    )


def capacitor_v(item_id: str, name: str, value: str, x: float, y1: float, y2: float, color: tuple[int, int, int, int] = RED) -> str:
    mid = (y1 + y2) / 2
    return "".join(
        [
            wire(f"{item_id}-lead-t", x, y1, x, mid - 2, 0.15, "solid", color),
            wire(f"{item_id}-plate-t", x - 2.5, mid - 2, x + 2.5, mid - 2, 0.18, "solid", color),
            wire(f"{item_id}-plate-b", x - 2.5, mid + 2, x + 2.5, mid + 2, 0.18, "solid", color),
            wire(f"{item_id}-lead-b", x, mid + 2, x, y2, 0.15, "solid", color),
            text(f"{item_id}-name", name, x + 3.5, mid - 1.4, 0.70, True),
            text(f"{item_id}-val", value, x + 3.5, mid + 2.4, 0.68),
        ]
    )


def opamp(item_id: str, name: str, x: float, y: float) -> str:
    return "".join(
        [
            wire(f"{item_id}-left", x, y, x, y + 40, 0.16, "solid", GRAY),
            wire(f"{item_id}-top", x, y, x + 34, y + 20, 0.16, "solid", GRAY),
            wire(f"{item_id}-bottom", x, y + 40, x + 34, y + 20, 0.16, "solid", GRAY),
            text(f"{item_id}-name", name, x + 7, y + 21, 0.78, True),
            text(f"{item_id}-minus", "-", x + 2.3, y + 11.5, 0.95, True),
            text(f"{item_id}-plus", "+", x + 2.1, y + 31.5, 0.95, True),
        ]
    )


def connector(item_id: str, name: str, x: float, y: float, pins: list[tuple[str, str]], color: tuple[int, int, int, int] = GREEN) -> str:
    parts = [text(f"{item_id}-name", name, x, y - 6, 0.85, True)]
    for i, (pin, net) in enumerate(pins):
        yy = y + i * 7
        parts.append(wire(f"{item_id}-{pin}-pad", x, yy - 2, x, yy + 2, 0.25, "solid", color))
        parts.append(wire(f"{item_id}-{pin}-wire", x, yy, x + 8, yy, 0.15, "solid", color))
        parts.append(text(f"{item_id}-{pin}-number", pin, x - 5, yy + 1.0, 0.68))
        parts.append(label(f"{item_id}-{pin}-net", net, x + 8, yy, 0.72))
    return "".join(parts)


def connector_explicit(
    item_id: str,
    name: str,
    x: float,
    pins: list[tuple[str, str, float]],
    color: tuple[int, int, int, int] = GREEN,
) -> str:
    top_y = min(pin_y for _, _, pin_y in pins)
    parts = [text(f"{item_id}-name", name, x, top_y - 7, 0.85, True)]
    for pin, net, pin_y in pins:
        parts.append(wire(f"{item_id}-{pin}-pad", x, pin_y - 2, x, pin_y + 2, 0.25, "solid", color))
        parts.append(wire(f"{item_id}-{pin}-wire", x, pin_y, x + 8, pin_y, 0.15, "solid", color))
        parts.append(text(f"{item_id}-{pin}-number", pin, x - 5, pin_y + 1.0, 0.68))
        parts.append(label(f"{item_id}-{pin}-net", net, x + 8, pin_y, 0.72))
    return "".join(parts)


def gnd(item_id: str, x: float, y: float) -> str:
    return "".join(
        [
            wire(f"{item_id}-lead", x, y, x, y + 2.3, 0.16, "solid", BLUE),
            wire(f"{item_id}-a", x - 3.0, y + 2.3, x + 3.0, y + 2.3, 0.16, "solid", BLUE),
            wire(f"{item_id}-b", x - 2.0, y + 3.7, x + 2.0, y + 3.7, 0.16, "solid", BLUE),
            wire(f"{item_id}-c", x - 1.0, y + 5.0, x + 1.0, y + 5.0, 0.16, "solid", BLUE),
        ]
    )


def dual_supply_at_opamp(
    prefix: str,
    ref: str,
    op_x: float,
    op_y: float,
    c_plus_100n: str,
    c_plus_bulk: str,
    c_minus_100n: str,
    c_minus_bulk: str,
) -> str:
    """Draw the package power pins and their local decoupling without a block."""
    pin_x = op_x + 17
    plus_y = op_y - 22
    minus_y = op_y + 62
    cap_a_x = pin_x + 17
    cap_b_x = pin_x + 37
    return "".join(
        [
            wire(f"{prefix}-vplus-pin", pin_x, op_y, pin_x, plus_y, 0.15, "solid", RED),
            wire(f"{prefix}-vplus-rail", pin_x, plus_y, cap_b_x + 4, plus_y, 0.15, "solid", RED),
            label(f"{prefix}-vplus-net", "+12VA", cap_b_x + 4, plus_y, 0.72),
            text(f"{prefix}-vplus-pin-txt", f"{ref}A/{ref}B V+ (pin 8)", pin_x + 2, op_y - 5, 0.68),
            capacitor_v(f"{prefix}-cp100", c_plus_100n, "100nF", cap_a_x, plus_y, plus_y + 14),
            capacitor_v(f"{prefix}-cpbulk", c_plus_bulk, "10uF", cap_b_x, plus_y, plus_y + 14),
            junction(f"{prefix}-cp100-top", cap_a_x, plus_y, RED),
            junction(f"{prefix}-cpbulk-top", cap_b_x, plus_y, RED),
            wire(f"{prefix}-vplus-gnd-bus", cap_a_x, plus_y + 14, cap_b_x, plus_y + 14, 0.15, "solid", BLUE),
            gnd(f"{prefix}-vplus-gnd", (cap_a_x + cap_b_x) / 2, plus_y + 14),
            wire(f"{prefix}-vminus-pin", pin_x, op_y + 40, pin_x, minus_y, 0.15, "solid", RED),
            wire(f"{prefix}-vminus-rail", pin_x, minus_y, cap_b_x + 4, minus_y, 0.15, "solid", RED),
            label(f"{prefix}-vminus-net", "-12VA", cap_b_x + 4, minus_y, 0.72),
            text(f"{prefix}-vminus-pin-txt", f"{ref}A/{ref}B V- (pin 4)", pin_x + 2, op_y + 47, 0.68),
            capacitor_v(f"{prefix}-cm100", c_minus_100n, "100nF", cap_a_x, minus_y, minus_y + 14),
            capacitor_v(f"{prefix}-cmbulk", c_minus_bulk, "10uF", cap_b_x, minus_y, minus_y + 14),
            junction(f"{prefix}-cm100-top", cap_a_x, minus_y, RED),
            junction(f"{prefix}-cmbulk-top", cap_b_x, minus_y, RED),
            wire(f"{prefix}-vminus-gnd-bus", cap_a_x, minus_y + 14, cap_b_x, minus_y + 14, 0.15, "solid", BLUE),
            gnd(f"{prefix}-vminus-gnd", (cap_a_x + cap_b_x) / 2, minus_y + 14),
        ]
    )


def single_supply_at_opamp(prefix: str, ref: str, op_x: float, op_y: float, cap: str) -> str:
    """Draw VDD/VSS and one local bypass capacitor for a dual comparator."""
    pin_x = op_x + 17
    rail_y = op_y - 22
    cap_x = pin_x + 24
    return "".join(
        [
            wire(f"{prefix}-vdd-pin", pin_x, op_y, pin_x, rail_y, 0.15, "solid", RED),
            wire(f"{prefix}-vdd-rail", pin_x, rail_y, cap_x + 10, rail_y, 0.15, "solid", RED),
            label(f"{prefix}-vdd-net", "+3V3_LOGIC", cap_x + 10, rail_y, 0.70),
            text(f"{prefix}-vdd-pin-txt", f"{ref}A/{ref}B VDD", pin_x + 2, op_y - 5, 0.66),
            capacitor_v(f"{prefix}-cap", cap, "100nF", cap_x, rail_y, rail_y + 14),
            junction(f"{prefix}-cap-top", cap_x, rail_y, RED),
            gnd(f"{prefix}-cap-gnd", cap_x, rail_y + 14),
            wire(f"{prefix}-vss-pin", pin_x, op_y + 40, pin_x, op_y + 48, 0.15, "solid", BLUE),
            text(f"{prefix}-vss-pin-txt", f"{ref}A/{ref}B VSS", pin_x + 2, op_y + 46, 0.66),
            gnd(f"{prefix}-vss-gnd", pin_x, op_y + 48),
        ]
    )


def reference_divider_v(
    prefix: str,
    x: float,
    y: float,
    node: str,
    r_top: str,
    r_top_value: str,
    r_bottom: str,
    r_bottom_value: str,
    cap: str,
) -> str:
    node_y = y + 24
    ground_y = y + 48
    cap_x = x + 20
    return "".join(
        [
            label(f"{prefix}-3v3", "+3V3_LOGIC", x, y + 4, 0.68, "right bottom"),
            resistor_v(f"{prefix}-rtop", r_top, r_top_value, x, y + 4, node_y, VIOLET),
            wire(f"{prefix}-node", x, node_y, cap_x, node_y, 0.15, "solid", VIOLET),
            label(f"{prefix}-node-label", node, cap_x, node_y, 0.70),
            resistor_v(f"{prefix}-rbot", r_bottom, r_bottom_value, x, node_y, ground_y, VIOLET),
            capacitor_v(f"{prefix}-cap", cap, "100nF", cap_x, node_y, ground_y, VIOLET),
            junction(f"{prefix}-node-dot", x, node_y, VIOLET),
            wire(f"{prefix}-gnd-bus", x, ground_y, cap_x, ground_y, 0.15, "solid", BLUE),
            gnd(f"{prefix}-gnd", (x + cap_x) / 2, ground_y),
        ]
    )


def threshold_divider_v(prefix: str, x: float, y: float) -> str:
    """Draw VTH with the purchased 6.7k and 38k resistors in parallel."""
    node_y = y + 24
    ground_y = y + 48
    parallel_x = x + 11
    cap_x = x + 28
    return "".join(
        [
            label(f"{prefix}-3v3", "+3V3_LOGIC", x, y + 4, 0.68, "right bottom"),
            wire(f"{prefix}-top-bus", x, y + 4, parallel_x, y + 4, 0.15, "solid", VIOLET),
            resistor_v(f"{prefix}-rtop-a", "R23", "6k7", x, y + 4, node_y, VIOLET),
            resistor_v(f"{prefix}-rtop-b", "R25", "38k", parallel_x, y + 4, node_y, VIOLET),
            wire(f"{prefix}-node", x, node_y, cap_x, node_y, 0.15, "solid", VIOLET),
            label(f"{prefix}-node-label", "VTH_RX", cap_x, node_y, 0.70),
            resistor_v(f"{prefix}-rbot", "R24", "10k", x, node_y, ground_y, VIOLET),
            capacitor_v(f"{prefix}-cap", "C11", "100nF", cap_x, node_y, ground_y, VIOLET),
            junction(f"{prefix}-top-a", x, y + 4, VIOLET),
            junction(f"{prefix}-top-b", parallel_x, y + 4, VIOLET),
            junction(f"{prefix}-node-a", x, node_y, VIOLET),
            junction(f"{prefix}-node-b", parallel_x, node_y, VIOLET),
            wire(f"{prefix}-gnd-bus", x, ground_y, cap_x, ground_y, 0.15, "solid", BLUE),
            gnd(f"{prefix}-gnd", (x + cap_x) / 2, ground_y),
        ]
    )


def decoupling_quad(
    prefix: str,
    x: float,
    y: float,
    plus_net: str,
    minus_net: str,
    c_plus_100n: str,
    c_plus_bulk: str,
    c_minus_100n: str,
    c_minus_bulk: str,
) -> str:
    return "".join(
        [
            label(f"{prefix}-plus", plus_net, x, y, 0.72),
            wire(f"{prefix}-plus-bus", x + 22, y, x + 54, y, 0.12, "solid", RED),
            capacitor_v(f"{prefix}-c100", c_plus_100n, "100nF", x + 30, y, y + 16),
            capacitor_v(f"{prefix}-c10u", c_plus_bulk, "10uF", x + 45, y, y + 16),
            gnd(f"{prefix}-gnd", x + 38, y + 16),
            label(f"{prefix}-minus", minus_net, x, y + 30, 0.72),
            wire(f"{prefix}-minus-bus", x + 22, y + 30, x + 54, y + 30, 0.12, "solid", RED),
            capacitor_v(f"{prefix}-c100n", c_minus_100n, "100nF", x + 30, y + 30, y + 46),
            capacitor_v(f"{prefix}-c10un", c_minus_bulk, "10uF", x + 45, y + 30, y + 46),
            gnd(f"{prefix}-gndn", x + 38, y + 46),
        ]
    )


def supply_caps_inline(
    prefix: str,
    x: float,
    y: float,
    plus_net: str,
    minus_net: str,
    c_plus_100n: str,
    c_plus_bulk: str,
    c_minus_100n: str,
    c_minus_bulk: str,
) -> str:
    return "".join(
        [
            label(f"{prefix}-plus", plus_net, x, y, 0.68),
            wire(f"{prefix}-plus-bus", x + 19, y, x + 54, y, 0.12, "solid", RED),
            capacitor_v(f"{prefix}-cp100", c_plus_100n, "100nF", x + 28, y, y + 13),
            capacitor_v(f"{prefix}-cpbulk", c_plus_bulk, "10uF", x + 43, y, y + 13),
            gnd(f"{prefix}-gnd-plus", x + 36, y + 13),
            label(f"{prefix}-minus", minus_net, x, y + 23, 0.68),
            wire(f"{prefix}-minus-bus", x + 19, y + 23, x + 54, y + 23, 0.12, "solid", RED),
            capacitor_v(f"{prefix}-cm100", c_minus_100n, "100nF", x + 28, y + 23, y + 36),
            capacitor_v(f"{prefix}-cmbulk", c_minus_bulk, "10uF", x + 43, y + 23, y + 36),
            gnd(f"{prefix}-gnd-minus", x + 36, y + 36),
        ]
    )


def power_unit_with_caps(
    prefix: str,
    ref: str,
    x: float,
    y: float,
    plus_net: str,
    minus_net: str,
    c_plus_100n: str,
    c_plus_bulk: str,
    c_minus_100n: str,
    c_minus_bulk: str,
) -> str:
    box_x = x + 70
    return "".join(
        [
            wire(f"{prefix}-box-top", box_x, y - 7, box_x + 32, y - 7, 0.12, "solid", GRAY),
            wire(f"{prefix}-box-right", box_x + 32, y - 7, box_x + 32, y + 31, 0.12, "solid", GRAY),
            wire(f"{prefix}-box-bottom", box_x + 32, y + 31, box_x, y + 31, 0.12, "solid", GRAY),
            wire(f"{prefix}-box-left", box_x, y + 31, box_x, y - 7, 0.12, "solid", GRAY),
            text(f"{prefix}-title", f"{ref} PWR", box_x + 4, y + 6, 0.72, True),
            text(f"{prefix}-pin-plus", "V+", box_x + 5, y + 13, 0.65),
            text(f"{prefix}-pin-minus", "V-", box_x + 5, y + 24, 0.65),
            label(f"{prefix}-plus-net", plus_net, x, y, 0.68),
            wire(f"{prefix}-plus-rail", x + 18, y, box_x, y, 0.14, "solid", RED),
            capacitor_v(f"{prefix}-plus-100n", c_plus_100n, "100nF", x + 30, y, y + 14),
            capacitor_v(f"{prefix}-plus-bulk", c_plus_bulk, "10uF", x + 46, y, y + 14),
            gnd(f"{prefix}-plus-gnd", x + 38, y + 14),
            label(f"{prefix}-minus-net", minus_net, x, y + 22, 0.68),
            wire(f"{prefix}-minus-rail", x + 18, y + 22, box_x, y + 22, 0.14, "solid", RED),
            capacitor_v(f"{prefix}-minus-100n", c_minus_100n, "100nF", x + 30, y + 22, y + 36),
            capacitor_v(f"{prefix}-minus-bulk", c_minus_bulk, "10uF", x + 46, y + 22, y + 36),
            gnd(f"{prefix}-minus-gnd", x + 38, y + 36),
        ]
    )


def power_unit_3v3_with_cap(prefix: str, ref: str, x: float, y: float, cap: str) -> str:
    box_x = x + 52
    return "".join(
        [
            wire(f"{prefix}-box-top", box_x, y - 7, box_x + 30, y - 7, 0.12, "solid", GRAY),
            wire(f"{prefix}-box-right", box_x + 30, y - 7, box_x + 30, y + 18, 0.12, "solid", GRAY),
            wire(f"{prefix}-box-bottom", box_x + 30, y + 18, box_x, y + 18, 0.12, "solid", GRAY),
            wire(f"{prefix}-box-left", box_x, y + 18, box_x, y - 7, 0.12, "solid", GRAY),
            text(f"{prefix}-title", f"{ref} PWR", box_x + 4, y + 6, 0.68, True),
            text(f"{prefix}-pin", "VDD", box_x + 5, y + 13, 0.62),
            label(f"{prefix}-net", "+3V3_LOGIC", x, y, 0.68),
            wire(f"{prefix}-rail", x + 22, y, box_x, y, 0.14, "solid", RED),
            capacitor_v(f"{prefix}-cap", cap, "100nF", x + 34, y, y + 16),
            gnd(f"{prefix}-gnd", x + 34, y + 16),
        ]
    )


def reference_divider_h(
    prefix: str,
    x: float,
    y: float,
    node: str,
    r_top: str,
    r_top_value: str,
    r_bottom: str,
    r_bottom_value: str,
    cap: str,
) -> str:
    node_x = x + 66
    return "".join(
        [
            label(f"{prefix}-3v3", "+3V3_LOGIC", x, y + 1, 0.68),
            resistor_h(f"{prefix}-rtop", r_top, r_top_value, x + 30, y, x + 60, VIOLET),
            wire(f"{prefix}-node-a", x + 60, y, x + 72, y, 0.12, "solid", VIOLET),
            label(f"{prefix}-node", node, node_x - 2, y - 6, 0.72),
            capacitor_v(f"{prefix}-cap", cap, "100nF", node_x, y, y + 16, VIOLET),
            resistor_h(f"{prefix}-rbot", r_bottom, r_bottom_value, x + 78, y, x + 108, VIOLET),
            gnd(f"{prefix}-gnd", x + 116, y),
        ]
    )


def build_tx() -> str:
    p: list[str] = []
    p.append(text("tx-title", "TX", 15, 20, 1.15, True))
    p.append(connector("j1", "J1 Pico TX / fuentes", 15, 50, [("1", "TX_A_LOGIC"), ("2", "TX_B_LOGIC"), ("3", "+3V3_LOGIC"), ("4", "GND_COMUN"), ("5", "+12VA"), ("6", "-12VA")]))

    p.append(opamp("u1a", "U1A TL082CP", 116, 50))
    p.append(label("txb-u1a", "TX_B_LOGIC", 72, 60, 0.72, "right bottom"))
    p.append(resistor_h("r1", "R1", "22k", 72, 60, 98))
    p.append(wire("u1a-minus-input", 98, 60, 116, 60))
    p.append(junction("u1a-minus", 98, 60))
    p.append(label("u1a-fb", "TXA_DRV", 98, 38, 0.70, "right bottom"))
    p.append(resistor_v("r2", "R2", "33k", 98, 38, 60))
    p.append(label("txa-u1a", "TX_A_LOGIC", 72, 80, 0.72, "right bottom"))
    p.append(resistor_h("r3", "R3", "22k", 72, 80, 98))
    p.append(wire("u1a-plus-input", 98, 80, 116, 80))
    p.append(resistor_v("r4", "R4", "33k", 98, 80, 104))
    p.append(junction("u1a-plus", 98, 80))
    p.append(gnd("gnd-r4", 98, 104))
    p.append(wire("u1a-output", 150, 70, 160, 70))
    p.append(label("u1a-output-net", "TXA_DRV", 155, 70, 0.68, "right bottom"))
    p.append(resistor_h("r9", "R9", "39R", 160, 70, 198, BLUE))
    p.append(wire("line-a-to-j2", 198, 70, 225, 70, 0.16, "solid", BLUE))
    p.append(dual_supply_at_opamp("u1-supply", "U1", 116, 50, "C1", "C2", "C3", "C4"))

    p.append(opamp("u1b", "U1B TL082CP", 116, 145))
    p.append(label("txa-u1b", "TX_A_LOGIC", 72, 155, 0.72, "right bottom"))
    p.append(resistor_h("r5", "R5", "22k", 72, 155, 98))
    p.append(wire("u1b-minus-input", 98, 155, 116, 155))
    p.append(junction("u1b-minus", 98, 155))
    p.append(label("u1b-fb", "TXB_DRV", 98, 133, 0.70, "right bottom"))
    p.append(resistor_v("r6", "R6", "33k", 98, 133, 155))
    p.append(label("txb-u1b", "TX_B_LOGIC", 72, 175, 0.72, "right bottom"))
    p.append(resistor_h("r7", "R7", "22k", 72, 175, 98))
    p.append(wire("u1b-plus-input", 98, 175, 116, 175))
    p.append(resistor_v("r8", "R8", "33k", 98, 175, 199))
    p.append(junction("u1b-plus", 98, 175))
    p.append(gnd("gnd-r8", 98, 199))
    p.append(wire("u1b-output", 150, 165, 160, 165))
    p.append(label("u1b-output-net", "TXB_DRV", 155, 165, 0.68, "right bottom"))
    p.append(resistor_h("r10", "R10", "39R", 160, 165, 198, BLUE))
    p.append(wire("line-b-to-j2", 198, 165, 225, 165, 0.16, "solid", BLUE))
    return "".join(p)


def build_line() -> str:
    return "".join(
        [
            text("line-title", "J2", 225, 20, 1.15, True),
            connector_explicit(
                "j2",
                "Linea A/B/GND",
                225,
                [("1", "LINE_A", 70), ("2", "LINE_B", 165), ("3", "GND_REF", 190)],
                BLUE,
            ),
            wire("j2-gnd-wire", 208, 190, 225, 190, 0.16, "solid", BLUE),
            gnd("j2-gnd", 208, 190),
        ]
    )


def build_vref_local() -> str:
    return "".join(
        [
            reference_divider_v("vref", 405, 96, "VREF_RX", "R21", "1k", "R22", "1k", "C10"),
            threshold_divider_v("vth", 438, 188),
        ]
    )


def build_rx() -> str:
    p: list[str] = []
    p.append(text("rx-title", "RX", 280, 20, 1.15, True))

    p.append(build_vref_local())

    p.append(opamp("u2a", "U2A TL082CP", 315, 50))
    p.append(label("lineb-u2a", "LINE_B", 275, 60, 0.72, "right bottom"))
    p.append(resistor_h("r11", "R11", "99k", 275, 60, 297))
    p.append(wire("u2a-minus-input", 297, 60, 315, 60))
    p.append(junction("u2a-minus", 297, 60))
    p.append(label("u2a-fb", "RX_A_BIASED", 297, 38, 0.68, "right bottom"))
    p.append(resistor_v("r12", "R12", "10k", 297, 38, 60))
    p.append(label("linea-u2a", "LINE_A", 275, 80, 0.72, "right bottom"))
    p.append(resistor_h("r13", "R13", "99k", 275, 80, 297))
    p.append(wire("u2a-plus-input", 297, 80, 315, 80))
    p.append(resistor_v("r14", "R14", "10k", 297, 80, 104, VIOLET))
    p.append(junction("u2a-plus", 297, 80))
    p.append(label("vref-u2a", "VREF_RX", 297, 104, 0.70))
    p.append(wire("u2a-output", 349, 70, 386, 70))
    p.append(label("rxa-biased", "RX_A_BIASED", 386, 70, 0.74))
    p.append(dual_supply_at_opamp("u2-supply", "U2", 315, 50, "C5", "C6", "C7", "C8"))

    p.append(opamp("u2b", "U2B TL082CP", 315, 145))
    p.append(label("linea-u2b", "LINE_A", 275, 155, 0.72, "right bottom"))
    p.append(resistor_h("r15", "R15", "99k", 275, 155, 297))
    p.append(wire("u2b-minus-input", 297, 155, 315, 155))
    p.append(junction("u2b-minus", 297, 155))
    p.append(label("u2b-fb", "RX_B_BIASED", 297, 133, 0.68, "right bottom"))
    p.append(resistor_v("r16", "R16", "10k", 297, 133, 155))
    p.append(label("lineb-u2b", "LINE_B", 275, 175, 0.72, "right bottom"))
    p.append(resistor_h("r17", "R17", "99k", 275, 175, 297))
    p.append(wire("u2b-plus-input", 297, 175, 315, 175))
    p.append(resistor_v("r18", "R18", "10k", 297, 175, 199, VIOLET))
    p.append(junction("u2b-plus", 297, 175))
    p.append(label("vref-u2b", "VREF_RX", 297, 199, 0.70))
    p.append(wire("u2b-output", 349, 165, 386, 165))
    p.append(label("rxb-biased", "RX_B_BIASED", 386, 165, 0.74))

    p.append(opamp("u3a", "U3A MCP6562", 478, 50))
    p.append(label("vth-u3a", "VTH_RX", 466, 60, 0.70, "right bottom"))
    p.append(label("rxa-u3", "RX_A_BIASED", 466, 80, 0.70, "right bottom"))
    p.append(wire("vth-u3a-in", 466, 60, 478, 60))
    p.append(wire("rxa-u3-in", 466, 80, 478, 80))
    p.append(wire("u3a-out", 512, 70, 552, 70))
    p.append(single_supply_at_opamp("u3-supply", "U3", 478, 50, "C9"))

    p.append(opamp("u3b", "U3B MCP6562", 478, 145))
    p.append(label("vth-u3b", "VTH_RX", 466, 155, 0.70, "right bottom"))
    p.append(label("rxb-u3", "RX_B_BIASED", 466, 175, 0.70, "right bottom"))
    p.append(wire("vth-u3b-in", 466, 155, 478, 155))
    p.append(wire("rxb-u3-in", 466, 175, 478, 175))
    p.append(wire("u3b-out", 512, 165, 552, 165))
    p.append(
        connector_explicit(
            "j3",
            "J3 Pico RX",
            552,
            [
                ("1", "RX_A_LOGIC", 70),
                ("2", "RX_B_LOGIC", 165),
                ("3", "+3V3_LOGIC", 205),
                ("4", "GND_COMUN", 219),
            ],
        )
    )
    return "".join(p)


def build_schematic() -> str:
    body = "".join([build_tx(), build_line(), build_rx()])
    return f"""(kicad_sch
	(version 20250610)
	(generator "eeschema")
	(generator_version "10.0")
	(uuid "{uid('schematic-root')}")
	(paper "A2")
	(title_block
		(title "Front-end TL082CP ARINC 429-like")
		(date "2026-08-05")
		(rev "E")
		(company "Proyecto PAMPA")
	)
	(lib_symbols)
{body}
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
            "filename": PRO_PATH.name,
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
            "plot_directory": "exports",
        },
        "sheets": [[uid("schematic-root"), "frontend_arinc429_tl082_circuit"]],
    }
    return json.dumps(data, indent=2)


def main() -> None:
    PROJECT_DIR.mkdir(parents=True, exist_ok=True)
    SCH_PATH.write_text(build_schematic(), encoding="utf-8", newline="\n")
    PRO_PATH.write_text(build_project_file(), encoding="utf-8", newline="\n")
    print(SCH_PATH)
    print(PRO_PATH)


if __name__ == "__main__":
    main()
