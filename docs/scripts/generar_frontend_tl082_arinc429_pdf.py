from __future__ import annotations

from pathlib import Path
from xml.sax.saxutils import escape

from reportlab.lib import colors
from reportlab.lib.enums import TA_LEFT
from reportlab.lib.pagesizes import A4, landscape
from reportlab.lib.styles import ParagraphStyle, getSampleStyleSheet
from reportlab.lib.units import mm
from reportlab.pdfbase import pdfmetrics
from reportlab.pdfbase.ttfonts import TTFont
from reportlab.platypus import (
    Flowable,
    PageBreak,
    Paragraph,
    Preformatted,
    SimpleDocTemplate,
    Spacer,
    Table,
    TableStyle,
)


ROOT = Path(__file__).resolve().parents[2]
OUT_DIR = ROOT / "ELECTRICO" / "frontend_arinc429_lab" / "exports"
PDF_DIR = ROOT / "docs" / "pdf"
SVG_PATH = OUT_DIR / "frontend_arinc429_tl082_rework.svg"
PDF_PATH = PDF_DIR / "frontend_arinc429_tl082_rework.pdf"

NAVY = colors.HexColor("#102A43")
BLUE = colors.HexColor("#1D5F99")
GREEN = colors.HexColor("#2F7D5A")
ORANGE = colors.HexColor("#B96B1B")
RED = colors.HexColor("#A43E3E")
PURPLE = colors.HexColor("#7A4AA0")
INK = colors.HexColor("#1F2933")
MUTED = colors.HexColor("#5B6876")
GRID = colors.HexColor("#C9D3DF")
LIGHT = colors.HexColor("#F5F7FA")
PALE_BLUE = colors.HexColor("#EAF2FB")
PALE_GREEN = colors.HexColor("#EAF5EF")
PALE_ORANGE = colors.HexColor("#FFF2E2")
PALE_RED = colors.HexColor("#FBEAEA")


def register_fonts() -> tuple[str, str, str]:
    regular = Path("C:/Windows/Fonts/calibri.ttf")
    bold = Path("C:/Windows/Fonts/calibrib.ttf")
    mono = Path("C:/Windows/Fonts/consola.ttf")
    if regular.exists() and bold.exists() and mono.exists():
        pdfmetrics.registerFont(TTFont("Calibri", str(regular)))
        pdfmetrics.registerFont(TTFont("Calibri-Bold", str(bold)))
        pdfmetrics.registerFont(TTFont("Consolas", str(mono)))
        return "Calibri", "Calibri-Bold", "Consolas"
    return "Helvetica", "Helvetica-Bold", "Courier"


BODY_FONT, BOLD_FONT, MONO_FONT = register_fonts()
SAMPLE = getSampleStyleSheet()

ST = {
    "title": ParagraphStyle(
        "Title",
        parent=SAMPLE["Title"],
        fontName=BOLD_FONT,
        fontSize=18,
        leading=21,
        textColor=NAVY,
        alignment=TA_LEFT,
        spaceAfter=5,
    ),
    "subtitle": ParagraphStyle(
        "Subtitle",
        parent=SAMPLE["BodyText"],
        fontName=BODY_FONT,
        fontSize=10,
        leading=12,
        textColor=MUTED,
        spaceAfter=8,
    ),
    "h1": ParagraphStyle(
        "H1",
        parent=SAMPLE["Heading1"],
        fontName=BOLD_FONT,
        fontSize=13,
        leading=15,
        textColor=BLUE,
        spaceBefore=5,
        spaceAfter=4,
    ),
    "body": ParagraphStyle(
        "Body",
        parent=SAMPLE["BodyText"],
        fontName=BODY_FONT,
        fontSize=8.8,
        leading=10.4,
        textColor=INK,
        spaceAfter=4,
    ),
    "small": ParagraphStyle(
        "Small",
        parent=SAMPLE["BodyText"],
        fontName=BODY_FONT,
        fontSize=7.3,
        leading=8.5,
        textColor=INK,
        spaceAfter=0,
    ),
    "small_bold": ParagraphStyle(
        "SmallBold",
        parent=SAMPLE["BodyText"],
        fontName=BOLD_FONT,
        fontSize=7.4,
        leading=8.6,
        textColor=NAVY,
        spaceAfter=0,
    ),
    "table_header": ParagraphStyle(
        "TableHeader",
        parent=SAMPLE["BodyText"],
        fontName=BOLD_FONT,
        fontSize=7.2,
        leading=8.4,
        textColor=NAVY,
        spaceAfter=0,
    ),
    "table_body": ParagraphStyle(
        "TableBody",
        parent=SAMPLE["BodyText"],
        fontName=BODY_FONT,
        fontSize=7.0,
        leading=8.2,
        textColor=INK,
        spaceAfter=0,
    ),
    "code": ParagraphStyle(
        "Code",
        parent=SAMPLE["Code"],
        fontName=MONO_FONT,
        fontSize=7.4,
        leading=8.6,
        textColor=INK,
        spaceAfter=0,
    ),
}


def p(text: str, style: str = "body") -> Paragraph:
    return Paragraph(escape(text).replace("\n", "<br/>"), ST[style])


def code_block(text: str, width: float = 250 * mm) -> Table:
    table = Table([[Preformatted(text.strip(), ST["code"])]], colWidths=[width], hAlign="CENTER")
    table.setStyle(
        TableStyle(
            [
                ("BACKGROUND", (0, 0), (-1, -1), LIGHT),
                ("BOX", (0, 0), (-1, -1), 0.5, GRID),
                ("LEFTPADDING", (0, 0), (-1, -1), 6),
                ("RIGHTPADDING", (0, 0), (-1, -1), 6),
                ("TOPPADDING", (0, 0), (-1, -1), 5),
                ("BOTTOMPADDING", (0, 0), (-1, -1), 5),
            ]
        )
    )
    return table


def simple_table(rows: list[list[str]], widths: list[float]) -> Table:
    data = []
    for r, row in enumerate(rows):
        style = "table_header" if r == 0 else "table_body"
        data.append([p(cell, style) for cell in row])
    table = Table(data, colWidths=widths, hAlign="CENTER", repeatRows=1)
    commands = [
        ("GRID", (0, 0), (-1, -1), 0.35, GRID),
        ("BACKGROUND", (0, 0), (-1, 0), PALE_BLUE),
        ("VALIGN", (0, 0), (-1, -1), "TOP"),
        ("LEFTPADDING", (0, 0), (-1, -1), 3.5),
        ("RIGHTPADDING", (0, 0), (-1, -1), 3.5),
        ("TOPPADDING", (0, 0), (-1, -1), 3.5),
        ("BOTTOMPADDING", (0, 0), (-1, -1), 3.5),
    ]
    for row in range(1, len(rows)):
        if row % 2 == 0:
            commands.append(("BACKGROUND", (0, row), (-1, row), colors.HexColor("#FBFCFE")))
    table.setStyle(TableStyle(commands))
    return table


def callout(title: str, text: str, fill=PALE_RED, border=RED) -> Table:
    table = Table([[p(title, "small_bold")], [p(text, "small")]], colWidths=[250 * mm], hAlign="CENTER")
    table.setStyle(
        TableStyle(
            [
                ("BACKGROUND", (0, 0), (-1, -1), fill),
                ("BOX", (0, 0), (-1, -1), 0.65, border),
                ("LEFTPADDING", (0, 0), (-1, -1), 7),
                ("RIGHTPADDING", (0, 0), (-1, -1), 7),
                ("TOPPADDING", (0, 0), (-1, -1), 5),
                ("BOTTOMPADDING", (0, 0), (-1, -1), 5),
            ]
        )
    )
    return table


class Schematic:
    def __init__(self) -> None:
        self.w = 1500
        self.h = 900

    def svg_box(self, x: int, y: int, w: int, h: int, title: str, lines: list[str], fill: str, stroke: str) -> str:
        body = [
            f'<rect x="{x}" y="{y}" width="{w}" height="{h}" rx="8" fill="{fill}" stroke="{stroke}" stroke-width="2"/>',
            f'<text x="{x + 12}" y="{y + 28}" class="title">{escape(title)}</text>',
        ]
        for i, line in enumerate(lines):
            body.append(f'<text x="{x + 12}" y="{y + 56 + i * 22}" class="small">{escape(line)}</text>')
        return "\n".join(body)

    def svg_wire(self, x1: int, y1: int, x2: int, y2: int, color: str = "#2F7D5A", dash: str = "") -> str:
        d = f' stroke-dasharray="{dash}"' if dash else ""
        return f'<line x1="{x1}" y1="{y1}" x2="{x2}" y2="{y2}" stroke="{color}" stroke-width="3"{d}/>'

    def build_svg(self) -> str:
        s: list[str] = [
            f'<svg xmlns="http://www.w3.org/2000/svg" width="{self.w}" height="{self.h}" viewBox="0 0 {self.w} {self.h}">',
            "<style>",
            ".title{font-family:Arial,sans-serif;font-size:21px;font-weight:700;fill:#102A43}",
            ".small{font-family:Arial,sans-serif;font-size:17px;fill:#1F2933}",
            ".tiny{font-family:Arial,sans-serif;font-size:14px;fill:#1F2933}",
            ".note{font-family:Arial,sans-serif;font-size:15px;font-weight:700;fill:#A43E3E}",
            "</style>",
            '<rect width="1500" height="900" fill="#FFFFFF"/>',
            '<text x="35" y="42" class="title">Front-end ARINC 429-like con TL082CP - revision electrica de banco</text>',
            '<text x="35" y="72" class="note">Advertencia: TX con TL082 es viable para banco; RX solo con TL082 no entrega GPIO seguro sin etapa final de decision/proteccion.</text>',
        ]
        s.append(self.svg_box(35, 110, 170, 245, "Pico TX", ["GP2 TX_A_LOGIC", "GP3 TX_B_LOGIC", "0..3,3 V", "GND comun"], "#EAF2FB", "#1D5F99"))
        s.append(self.svg_box(260, 95, 365, 330, "TX bipolar - U1 TL082CP", ["U1A: diff amp", "LINE_A_PRE=1,5*(A-B)", "R1/R3=20k, R2/R4=30k", "U1B: diff amp complementario", "LINE_B_PRE=1,5*(B-A)", "Alim: +12V/-12V", "Sin capacitores de acople"], "#FFF2E2", "#B96B1B"))
        s.append(self.svg_box(690, 130, 190, 230, "Salida TX", ["R9 39R serie A", "R10 39R serie B", "Zout diff ~=78R", "C opcional slew: DNP"], "#EAF5EF", "#2F7D5A"))
        s.append(self.svg_box(940, 130, 210, 230, "J2 linea", ["LINE_A", "LINE_B", "GND_REF/shield", "Cable STP 78R", "Sin terminacion RX"], "#F5F7FA", "#102A43"))
        s.append(self.svg_box(1210, 95, 250, 330, "RX analogico - U2 TL082CP", ["U2A=0,3*(A-B)", "U2B=0,3*(B-A)", "Rin=100k, Rf=30k", "Zin por linea ~=56k", "Alim: +12V/-12V", "Salida analogica NO GPIO"], "#FFF2E2", "#B96B1B"))
        s.append(self.svg_box(1210, 505, 250, 190, "Etapa final obligatoria", ["Schmitt/comparador/level-shift", "Salida 0..3,3 V", "Histeresis definida", "Proteccion GPIO"], "#FBEAEA", "#A43E3E"))
        s.append(self.svg_box(940, 530, 210, 155, "Pico RX", ["GP4 RX_A_LOGIC", "GP5 RX_B_LOGIC", "solo 0..3,3 V"], "#EAF2FB", "#1D5F99"))

        # Signal path
        s += [
            self.svg_wire(205, 185, 260, 185),
            self.svg_wire(205, 235, 260, 235),
            '<text x="218" y="174" class="tiny">TX_A</text>',
            '<text x="218" y="225" class="tiny">TX_B</text>',
            self.svg_wire(625, 190, 690, 190),
            self.svg_wire(625, 280, 690, 280),
            self.svg_wire(880, 190, 940, 190),
            self.svg_wire(880, 280, 940, 280),
            self.svg_wire(1150, 190, 1210, 190),
            self.svg_wire(1150, 280, 1210, 280),
            '<text x="895" y="178" class="tiny">LINE_A</text>',
            '<text x="895" y="268" class="tiny">LINE_B</text>',
            self.svg_wire(1335, 425, 1335, 505, "#A43E3E", "8 7"),
            '<text x="1165" y="466" class="note">No conectar salida U2 directa a Pico</text>',
            self.svg_wire(1210, 590, 1150, 590, "#A43E3E", "8 7"),
        ]
        # Power and decoupling
        s.append(self.svg_box(260, 505, 365, 160, "Alimentacion y desacople", ["U1/U2 TL082CP: +12V/-12V/GND", "C1,C3,C5,C7: 100nF por rail", "C2,C4,C6,C8: 10uF por rail", "GND comun solo en banco"], "#F5F7FA", "#5B6876"))
        s.append(self.svg_box(690, 505, 190, 160, "Mediciones", ["CH1=LINE_A", "CH2=LINE_B", "MATH=A-B", "+10/0/-10 V", "gap >=4 bits"], "#EAF5EF", "#2F7D5A"))

        s.append("</svg>")
        return "\n".join(s)

    def draw_pdf(self, c, x: float, y: float, scale: float = 0.165) -> None:
        def box(px, py, pw, ph, title, lines, fill, stroke):
            c.setFillColor(fill)
            c.setStrokeColor(stroke)
            c.setLineWidth(1.1)
            c.roundRect(x + px * scale, y - (py + ph) * scale, pw * scale, ph * scale, 4, fill=1, stroke=1)
            c.setFillColor(NAVY)
            c.setFont(BOLD_FONT, 8.0)
            c.drawString(x + (px + 8) * scale, y - (py + 24) * scale, title)
            c.setFillColor(INK)
            c.setFont(BODY_FONT, 6.6)
            for i, line in enumerate(lines):
                c.drawString(x + (px + 8) * scale, y - (py + 50 + i * 19) * scale, line)

        def wire(x1, y1, x2, y2, color=GREEN, dash=None):
            c.setStrokeColor(color)
            c.setLineWidth(1.0)
            if dash:
                c.setDash(dash)
            c.line(x + x1 * scale, y - y1 * scale, x + x2 * scale, y - y2 * scale)
            c.setDash()

        c.setFont(BOLD_FONT, 11)
        c.setFillColor(NAVY)
        c.drawString(x, y - 6, "Esquematico funcional TL082CP")
        c.setFont(BODY_FONT, 7)
        c.setFillColor(RED)
        c.drawString(x, y - 18, "TX completo; RX TL082 analogico requiere etapa final antes de GPIO.")
        box(35, 110, 170, 245, "Pico TX", ["GP2 TX_A_LOGIC", "GP3 TX_B_LOGIC", "0..3,3 V", "GND comun"], PALE_BLUE, BLUE)
        box(260, 95, 365, 330, "TX bipolar - U1 TL082CP", ["U1A: diff amp", "LINE_A_PRE=1,5*(A-B)", "R1/R3=20k, R2/R4=30k", "U1B: diff amp complementario", "LINE_B_PRE=1,5*(B-A)", "Alim: +12V/-12V", "Sin capacitores de acople"], PALE_ORANGE, ORANGE)
        box(690, 130, 190, 230, "Salida TX", ["R9 39R serie A", "R10 39R serie B", "Zout diff ~=78R", "C slew opcional DNP"], PALE_GREEN, GREEN)
        box(940, 130, 210, 230, "J2 linea", ["LINE_A", "LINE_B", "GND_REF/shield", "Cable STP 78R", "Sin terminacion RX"], LIGHT, NAVY)
        box(1210, 95, 250, 330, "RX analogico - U2 TL082CP", ["U2A=0,3*(A-B)", "U2B=0,3*(B-A)", "Rin=100k, Rf=30k", "Zin por linea ~=56k", "Alim: +12V/-12V", "Salida analogica NO GPIO"], PALE_ORANGE, ORANGE)
        box(1210, 505, 250, 190, "Etapa final obligatoria", ["Schmitt/comparador/level-shift", "Salida 0..3,3 V", "Histeresis definida", "Proteccion GPIO"], PALE_RED, RED)
        box(940, 530, 210, 155, "Pico RX", ["GP4 RX_A_LOGIC", "GP5 RX_B_LOGIC", "solo 0..3,3 V"], PALE_BLUE, BLUE)
        box(260, 505, 365, 160, "Alimentacion y desacople", ["U1/U2: +12V/-12V/GND", "100nF por rail cerca de cada CI", "10uF por rail por bloque", "GND comun solo en banco"], LIGHT, MUTED)
        box(690, 505, 190, 160, "Mediciones", ["CH1=LINE_A", "CH2=LINE_B", "MATH=A-B", "+10/0/-10 V", "gap >=4 bits"], PALE_GREEN, GREEN)
        for yv in (185, 235):
            wire(205, yv, 260, yv)
        for yv in (190, 280):
            wire(625, yv, 690, yv)
            wire(880, yv, 940, yv)
            wire(1150, yv, 1210, yv)
        wire(1335, 425, 1335, 505, RED, [3, 3])
        wire(1210, 590, 1150, 590, RED, [3, 3])


class SchematicFlowable(Flowable):
    def __init__(self) -> None:
        super().__init__()
        self.width = 250 * mm
        self.height = 150 * mm

    def draw(self) -> None:
        Schematic().draw_pdf(self.canv, 0, self.height, 0.44)


def write_svg() -> None:
    OUT_DIR.mkdir(parents=True, exist_ok=True)
    SVG_PATH.write_text(Schematic().build_svg(), encoding="utf-8", newline="\n")


def footer(canvas, doc) -> None:
    canvas.saveState()
    canvas.setFillColor(MUTED)
    canvas.setFont(BODY_FONT, 7)
    canvas.drawString(doc.leftMargin, 8 * mm, "PAMPA / ARINC 429 - front-end TL082CP de banco")
    canvas.drawRightString(landscape(A4)[0] - doc.rightMargin, 8 * mm, f"Pagina {doc.page}")
    canvas.restoreState()


def build_story() -> list:
    levels = [
        ["Estado", "LINE_A", "LINE_B", "A-B"],
        ["HIGH", "+5 V", "-5 V", "+10 V"],
        ["NULL", "0 V", "0 V", "0 V"],
        ["LOW", "-5 V", "+5 V", "-10 V"],
    ]
    tx = [
        ["Bloque", "Valores", "Impedancia / nota"],
        ["U1A", "R1=R3=20k, R2=R4=30k, G=1,5", "Zin inversora 20k; no inversora 50k"],
        ["U1B", "R1=R3=20k, R2=R4=30k, G=1,5", "Complementa LINE_B = 1,5*(B-A)"],
        ["Salida", "R9=39R, R10=39R", "Zout diferencial ~=78R"],
        ["Alimentacion", "+12V/-12V recomendado", "+/-9V minimo razonable; +/-6V marginal"],
        ["Capacitores", "100nF por rail + 10uF por bloque", "Desacople, no acople de senal"],
    ]
    rx = [
        ["Bloque", "Valores", "Impedancia / resultado"],
        ["U2A", "Rin=100k, Rf=30k, G=0,3", "RX_A_ANALOG=0,3*(A-B)"],
        ["U2B", "Rin=100k, Rf=30k, G=0,3", "RX_B_ANALOG=0,3*(B-A)"],
        ["Entrada RX", "cada linea ve aprox. 100k || 130k", "Zin por linea ~=56,5k, mayor que 8k"],
        ["Salida U2", "+3V / 0 / -3V segun polaridad", "No es GPIO seguro sin etapa final"],
        ["Etapa final", "Schmitt/comparador/level-shift", "Obligatoria para 0..3,3V e histeresis"],
    ]
    timing = [
        ["Caso", "Valor"],
        ["TL082 slew tipico", "13 V/us"],
        ["TL082 slew conservador de tabla", "8 V/us"],
        ["Cambio por conductor", "5 V"],
        ["t_slew tipico", "5/13 = 0,38 us"],
        ["100 kbps", "bit 10 us, media celda 5 us, subida+bajada tipica 0,76 us, meseta util 4,24 us"],
        ["12,5 kbps", "bit 80 us, media celda 40 us, subida+bajada tipica 0,76 us, meseta util 39,24 us"],
    ]
    bom = [
        ["Ref", "Componente", "Cantidad", "Valor / tipo"],
        ["U1", "TL082CP", "1", "TX bipolar, doble op-amp"],
        ["U2", "TL082CP", "1", "RX analogico diferencial, doble op-amp"],
        ["R1-R8", "Resistencias 1%", "8", "20k y 30k para U1A/U1B"],
        ["R9-R10", "Resistencias serie", "2", "39R 1%"],
        ["R11-R18", "Resistencias 1%", "8", "100k y 30k para U2A/U2B"],
        ["C1-C4", "Ceramicos", "4", "100nF, un capacitor por rail y por TL082"],
        ["C5-C8", "Electroliticos/ceramicos", "4", "10uF por rail y por bloque"],
        ["J1", "Header Pico TX/RX", "2", "GPIO y GND"],
        ["J2", "Bornera linea", "1", "LINE_A, LINE_B, GND_REF"],
        ["Cable", "Par trenzado blindado", "1", "78R caracteristico si se consigue"],
        ["Etapa final RX", "Schmitt/comparador/level-shift", "2 canales", "Obligatoria si se conecta a GP4/GP5"],
    ]

    story: list = [
        p("Front-end ARINC 429-like con TL082CP", "title"),
        p("Revision electrica para usar TL082CP entre una Pico TX y una Pico RX, manteniendo el proyecto KiCad actual como referencia de base.", "subtitle"),
        callout("Decision critica", "El TX con TL082CP es viable para banco. El RX con solo TL082CP no puede entregar una salida GPIO 0..3,3 V segura sin una etapa final no lineal: comparador, Schmitt trigger, level-shift o proteccion activa.", PALE_RED, RED),
        Spacer(1, 4 * mm),
        p("1. Objetivo ARINC-like", "h1"),
        simple_table(levels, [45 * mm, 45 * mm, 45 * mm, 45 * mm]),
        Spacer(1, 3 * mm),
    ]
    story.append(PageBreak())
    story.extend([
        p("2. Esquematico funcional", "h1"),
        SchematicFlowable(),
    ])
    story.append(PageBreak())
    story.extend(
        [
            p("3. Etapa TX bipolar", "h1"),
            p("Los dos amplificadores del TL082CP se usan retroalimentados como diferenciales complementarios. No se usan capacitores de acople en serie porque ARINC necesita conservar el NULL en DC. Solo se usan capacitores de desacople de alimentacion.", "body"),
            code_block(
                """
U1A: LINE_A_PRE = 1,5 * (TX_A_LOGIC - TX_B_LOGIC)
U1B: LINE_B_PRE = 1,5 * (TX_B_LOGIC - TX_A_LOGIC)
Zout diferencial = 39R + 39R ~= 78R
"""
            ),
            simple_table(tx, [38 * mm, 95 * mm, 115 * mm]),
            Spacer(1, 3 * mm),
            p("4. Etapa RX con TL082CP", "h1"),
            p("La configuracion TL082-only se documenta como front-end analogico. No se marca como salida final directa a la Pico porque una de las dos salidas puede ser negativa segun la polaridad de la linea.", "body"),
            simple_table(rx, [38 * mm, 95 * mm, 115 * mm]),
        ]
    )
    story.append(PageBreak())
    story.extend(
        [
            p("5. Masa, referencia e histeresis", "h1"),
            p("Referenciar el detector a masa permite detectar signo alrededor de 0 V, pero deja al sistema sensible al ruido en NULL. Se puede agregar histeresis con realimentacion positiva, pero en TL082 alimentado a +/-12 V el umbral depende del swing de salida, que no es rail-to-rail. La solucion limpia es mover la decision digital a un comparador o Schmitt trigger alimentado a 3,3 V.", "body"),
            p("6. Slew rate y pulso util", "h1"),
            simple_table(timing, [70 * mm, 178 * mm]),
            Spacer(1, 3 * mm),
            p("7. Impedancias relevantes", "h1"),
            p("La impedancia del cable objetivo es 78R. El transmisor se aproxima con 39R en cada rama. El receptor no se termina con 78R: su entrada queda en alta impedancia, aproximadamente 56,5k por linea en la etapa TL082 analogica. La Pico tiene entrada CMOS de alta impedancia, pero no debe usarse como proteccion electrica.", "body"),
            p("8. Lista de componentes", "h1"),
            simple_table(bom, [28 * mm, 78 * mm, 32 * mm, 110 * mm]),
        ]
    )
    return story


def build_pdf() -> None:
    PDF_DIR.mkdir(parents=True, exist_ok=True)
    doc = SimpleDocTemplate(
        str(PDF_PATH),
        pagesize=landscape(A4),
        rightMargin=12 * mm,
        leftMargin=12 * mm,
        topMargin=11 * mm,
        bottomMargin=14 * mm,
        title="Front-end ARINC 429-like con TL082CP",
        author="Proyecto PAMPA",
    )

    story = build_story()

    def first(canvas, doc_obj):
        footer(canvas, doc_obj)

    def later(canvas, doc_obj):
        footer(canvas, doc_obj)

    doc.build(story, onFirstPage=first, onLaterPages=later)


def main() -> None:
    write_svg()
    build_pdf()
    print(SVG_PATH)
    print(PDF_PATH)


if __name__ == "__main__":
    main()
