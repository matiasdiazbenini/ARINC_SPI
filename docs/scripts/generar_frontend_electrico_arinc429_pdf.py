from __future__ import annotations

from pathlib import Path
from xml.sax.saxutils import escape

from reportlab.lib import colors
from reportlab.lib.enums import TA_CENTER, TA_LEFT
from reportlab.lib.pagesizes import letter
from reportlab.lib.styles import ParagraphStyle, getSampleStyleSheet
from reportlab.lib.units import inch
from reportlab.pdfbase import pdfmetrics
from reportlab.pdfbase.ttfonts import TTFont
from reportlab.platypus import (
    PageBreak,
    Paragraph,
    Preformatted,
    SimpleDocTemplate,
    Spacer,
    Table,
    TableStyle,
    Flowable,
)


ROOT = Path(__file__).resolve().parents[2]
PDF_DIR = ROOT / "docs" / "pdf"
PDF_PATH = PDF_DIR / "frontend_electrico_arinc429_lab.pdf"

NAVY = colors.HexColor("#15324A")
BLUE = colors.HexColor("#2267A8")
GREEN = colors.HexColor("#2F7D5A")
ORANGE = colors.HexColor("#B96B1B")
RED = colors.HexColor("#A43E3E")
INK = colors.HexColor("#1F2933")
MUTED = colors.HexColor("#5B6876")
LINE = colors.HexColor("#C9D3DF")
LIGHT = colors.HexColor("#F4F7FA")
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
sample = getSampleStyleSheet()

ST = {
    "title": ParagraphStyle(
        "Title",
        parent=sample["Title"],
        fontName=BOLD_FONT,
        fontSize=22,
        leading=26,
        alignment=TA_LEFT,
        textColor=NAVY,
        spaceAfter=7,
    ),
    "subtitle": ParagraphStyle(
        "Subtitle",
        parent=sample["BodyText"],
        fontName=BODY_FONT,
        fontSize=11.2,
        leading=13.5,
        textColor=MUTED,
        spaceAfter=12,
    ),
    "h1": ParagraphStyle(
        "H1",
        parent=sample["Heading1"],
        fontName=BOLD_FONT,
        fontSize=14,
        leading=16,
        textColor=BLUE,
        spaceBefore=6,
        spaceAfter=6,
    ),
    "h2": ParagraphStyle(
        "H2",
        parent=sample["Heading2"],
        fontName=BOLD_FONT,
        fontSize=11.2,
        leading=13,
        textColor=NAVY,
        spaceBefore=5,
        spaceAfter=4,
    ),
    "body": ParagraphStyle(
        "Body",
        parent=sample["BodyText"],
        fontName=BODY_FONT,
        fontSize=9.4,
        leading=11.4,
        textColor=INK,
        spaceAfter=5,
    ),
    "small": ParagraphStyle(
        "Small",
        parent=sample["BodyText"],
        fontName=BODY_FONT,
        fontSize=7.7,
        leading=9.0,
        textColor=INK,
        spaceAfter=0,
    ),
    "small_bold": ParagraphStyle(
        "SmallBold",
        parent=sample["BodyText"],
        fontName=BOLD_FONT,
        fontSize=7.7,
        leading=9.0,
        textColor=NAVY,
        spaceAfter=0,
    ),
    "table_header": ParagraphStyle(
        "TableHeader",
        parent=sample["BodyText"],
        fontName=BOLD_FONT,
        fontSize=7.8,
        leading=9.0,
        textColor=NAVY,
        alignment=TA_LEFT,
        spaceAfter=0,
    ),
    "table_body": ParagraphStyle(
        "TableBody",
        parent=sample["BodyText"],
        fontName=BODY_FONT,
        fontSize=7.5,
        leading=8.7,
        textColor=INK,
        alignment=TA_LEFT,
        spaceAfter=0,
    ),
    "code": ParagraphStyle(
        "Code",
        parent=sample["Code"],
        fontName=MONO_FONT,
        fontSize=8.0,
        leading=9.2,
        textColor=INK,
        spaceAfter=0,
    ),
    "box_title": ParagraphStyle(
        "BoxTitle",
        parent=sample["BodyText"],
        fontName=BOLD_FONT,
        fontSize=8.4,
        leading=9.6,
        textColor=NAVY,
        alignment=TA_CENTER,
        spaceAfter=2,
    ),
    "box_text": ParagraphStyle(
        "BoxText",
        parent=sample["BodyText"],
        fontName=BODY_FONT,
        fontSize=7.6,
        leading=8.7,
        textColor=INK,
        alignment=TA_CENTER,
        spaceAfter=0,
    ),
}


def p(text: str, style: str = "body") -> Paragraph:
    return Paragraph(escape(text).replace("\n", "<br/>"), ST[style])


def code_block(text: str, width: float = 6.55 * inch) -> Table:
    table = Table([[Preformatted(text.strip(), ST["code"])]], colWidths=[width], hAlign="CENTER")
    table.setStyle(
        TableStyle(
            [
                ("BACKGROUND", (0, 0), (-1, -1), LIGHT),
                ("BOX", (0, 0), (-1, -1), 0.55, LINE),
                ("LEFTPADDING", (0, 0), (-1, -1), 8),
                ("RIGHTPADDING", (0, 0), (-1, -1), 8),
                ("TOPPADDING", (0, 0), (-1, -1), 6),
                ("BOTTOMPADDING", (0, 0), (-1, -1), 6),
            ]
        )
    )
    return table


def callout(title: str, text: str, fill=PALE_GREEN, border=GREEN) -> Table:
    table = Table([[p(title, "small_bold")], [p(text, "small")]], colWidths=[6.55 * inch], hAlign="CENTER")
    table.setStyle(
        TableStyle(
            [
                ("BACKGROUND", (0, 0), (-1, -1), fill),
                ("BOX", (0, 0), (-1, -1), 0.7, border),
                ("LEFTPADDING", (0, 0), (-1, -1), 8),
                ("RIGHTPADDING", (0, 0), (-1, -1), 8),
                ("TOPPADDING", (0, 0), (-1, -1), 6),
                ("BOTTOMPADDING", (0, 0), (-1, -1), 6),
            ]
        )
    )
    return table


def boxed(title: str, body: str, fill=PALE_BLUE, border=BLUE) -> Table:
    table = Table([[p(title, "box_title")], [p(body, "box_text")]], colWidths=[1.45 * inch])
    table.setStyle(
        TableStyle(
            [
                ("BACKGROUND", (0, 0), (-1, -1), fill),
                ("BOX", (0, 0), (-1, -1), 0.65, border),
                ("VALIGN", (0, 0), (-1, -1), "MIDDLE"),
                ("LEFTPADDING", (0, 0), (-1, -1), 5),
                ("RIGHTPADDING", (0, 0), (-1, -1), 5),
                ("TOPPADDING", (0, 0), (-1, -1), 6),
                ("BOTTOMPADDING", (0, 0), (-1, -1), 6),
            ]
        )
    )
    return table


def simple_table(rows: list[list[str]], widths: list[float], header_rows: int = 1) -> Table:
    data = []
    for r, row in enumerate(rows):
        style = "table_header" if r < header_rows else "table_body"
        data.append([p(cell, style) for cell in row])
    table = Table(data, colWidths=widths, hAlign="CENTER", repeatRows=header_rows)
    style_cmds = [
        ("GRID", (0, 0), (-1, -1), 0.35, LINE),
        ("BACKGROUND", (0, 0), (-1, header_rows - 1), PALE_BLUE),
        ("VALIGN", (0, 0), (-1, -1), "TOP"),
        ("LEFTPADDING", (0, 0), (-1, -1), 4),
        ("RIGHTPADDING", (0, 0), (-1, -1), 4),
        ("TOPPADDING", (0, 0), (-1, -1), 4),
        ("BOTTOMPADDING", (0, 0), (-1, -1), 4),
    ]
    for row in range(header_rows, len(rows)):
        if row % 2 == 0:
            style_cmds.append(("BACKGROUND", (0, row), (-1, row), colors.HexColor("#FBFCFE")))
    table.setStyle(TableStyle(style_cmds))
    return table


class ArchitectureDiagram(Flowable):
    def __init__(self) -> None:
        super().__init__()
        self.width = 6.50 * inch
        self.height = 0.98 * inch

    def draw_box(self, x: float, y: float, title: str, body: str, fill, border) -> None:
        canvas = self.canv
        w = 1.12 * inch
        h = 0.68 * inch
        canvas.setStrokeColor(border)
        canvas.setFillColor(fill)
        canvas.setLineWidth(0.8)
        canvas.rect(x, y, w, h, fill=1, stroke=1)
        canvas.setFillColor(NAVY)
        canvas.setFont(BOLD_FONT, 7.6)
        canvas.drawCentredString(x + w / 2, y + h - 14, title)
        canvas.setFillColor(INK)
        canvas.setFont(BODY_FONT, 7.0)
        lines = body.split("\n")
        first_y = y + h - 25
        for i, line in enumerate(lines):
            canvas.drawCentredString(x + w / 2, first_y - i * 8.0, line)

    def draw_arrow(self, x1: float, y: float, x2: float) -> None:
        canvas = self.canv
        canvas.setStrokeColor(NAVY)
        canvas.setFillColor(NAVY)
        canvas.setLineWidth(0.9)
        canvas.line(x1, y, x2, y)
        canvas.line(x2, y, x2 - 4, y + 3)
        canvas.line(x2, y, x2 - 4, y - 3)

    def draw(self) -> None:
        y = 0.18 * inch
        h = 0.68 * inch
        gap = 0.17 * inch
        w = 1.12 * inch
        x0 = 0.03 * inch
        xs = [x0 + i * (w + gap) for i in range(5)]
        items = [
            ("Pico TX", "GP2/GP3\n0..3,3 V", PALE_BLUE, BLUE),
            ("Front-end TX", "+5/-5\n0/0\n-5/+5", PALE_ORANGE, ORANGE),
            ("Linea A/B", "Par diferencial\nde banco", LIGHT, NAVY),
            ("Front-end RX", "Protege\natenua\ncompara", PALE_ORANGE, ORANGE),
            ("Pico RX", "GP4/GP5\n0..3,3 V", PALE_BLUE, BLUE),
        ]
        for x, item in zip(xs, items):
            self.draw_box(x, y, *item)
        cy = y + h / 2
        for i in range(4):
            self.draw_arrow(xs[i] + w + 3, cy, xs[i + 1] - 3)


def architecture_diagram() -> Flowable:
    return ArchitectureDiagram()


def footer(canvas, doc) -> None:
    canvas.saveState()
    canvas.setStrokeColor(colors.HexColor("#D7DEE6"))
    canvas.setLineWidth(0.5)
    canvas.line(doc.leftMargin, letter[1] - 0.52 * inch, letter[0] - doc.rightMargin, letter[1] - 0.52 * inch)
    canvas.setFillColor(MUTED)
    canvas.setFont(BOLD_FONT, 7.5)
    canvas.drawString(doc.leftMargin, letter[1] - 0.43 * inch, "PAMPA / ARINC 429  |  Frente electrico de laboratorio")
    canvas.setFont(BODY_FONT, 8)
    canvas.drawRightString(letter[0] - doc.rightMargin, 0.42 * inch, f"Pagina {doc.page}")
    canvas.restoreState()


def build_story() -> list:
    levels = [
        ["Estado", "Linea A vs GND", "Linea B vs GND", "Diferencial A-B"],
        ["HIGH", "+5 V", "-5 V", "+10 V"],
        ["NULL", "0 V", "0 V", "0 V"],
        ["LOW", "-5 V", "+5 V", "-10 V"],
    ]
    logic = [
        ["Estado actual", "GP TX_A", "GP TX_B", "Diferencial logico"],
        ["HIGH logico", "3,3 V", "0 V", "+3,3 V"],
        ["NULL", "0 V", "0 V", "0 V"],
        ["LOW logico", "0 V", "3,3 V", "-3,3 V"],
    ]
    bom = [
        ["Item", "Funcion", "Cant.", "Precio ARS aprox.", "Comentario"],
        ["Raspberry Pi Pico / Pico H", "Placa de prueba separada del sistema principal", "1", "12.000 a 35.000", "Puede ser una Pico 0 km para no tocar las placas actuales."],
        ["MCP6562", "Comparador dual rapido para RX", "1 a 2", "8.000 a 30.000 c/u", "Recomendado si se consigue. Salida push-pull y retardo en ns."],
        ["LM393B / LM393", "Comparador dual alternativo", "1 a 2", "800 a 4.000 c/u", "Sirve como respaldo local. Requiere pull-up; menos margen temporal."],
        ["TLV3702", "Comparador dual nanopower", "1", "6.000 a 20.000", "No recomendado para 100 kbps por retardo alto."],
        ["Op-amp dual rapido tipo TLV9352 / OPA197 / OPA2197", "Driver bipolar TX y acondicionamiento RX", "2 a 3", "8.000 a 35.000 c/u", "Elegir por disponibilidad, alimentacion bipolar y slew rate."],
        ["ICL7660 / TC7660", "Generar tension negativa simple", "1 a 2", "2.000 a 8.000 c/u", "Aceptable para pruebas de baja corriente."],
        ["Modulo DC/DC +-5 V o +-6 V", "Fuente bipolar mas estable", "1", "8.000 a 35.000", "Preferible para un montaje repetible."],
        ["Resistencias 1 %", "Ganancias, divisores, umbrales, pull-up", "kit", "5.000 a 18.000", "Valores utiles: 330R, 1k, 10k, 15k, 20k, 30k, 100k."],
        ["Capacitores 100 nF, 1 uF, 10 uF", "Desacople de integrados y fuentes", "kit", "5.000 a 15.000", "100 nF cerca de cada integrado."],
        ["Diodos TVS/ESD y Schottky", "Proteccion de entrada RX", "kit", "3.000 a 15.000", "No saltear si se ensaya con cable externo."],
        ["Protoboard o placa perforada", "Armado inicial", "1 a 2", "6.000 a 20.000 c/u", "La placa perforada es mas estable que protoboard para osciloscopio."],
        ["Cables, pin headers, borneras", "Conexionado", "kit", "5.000 a 20.000", "Separar A/B, GND y puntos de medicion."],
    ]
    tools = [
        ["Herramienta", "Uso", "Precio ARS aprox."],
        ["Osciloscopio 2 canales o mas con MATH", "Medir A, B y A-B", "Disponible / 600.000 a 1.800.000 si se compra"],
        ["Puntas x10 y clips", "Medicion con menor carga", "20.000 a 80.000"],
        ["Multimetro", "Continuidad, fuentes, niveles DC", "20.000 a 120.000"],
        ["Fuente de banco dual o dos fuentes aisladas", "Alimentar +-5 V o +-6 V y 3,3/5 V", "150.000 a 500.000"],
        ["Analizador logico USB", "Verificar GP4/GP5 despues del RX", "10.000 a 40.000"],
        ["Soldador, estanio, flux, pinzas", "Montaje estable", "30.000 a 150.000"],
    ]
    tests = [
        ["Paso", "Criterio de aceptacion"],
        ["1", "Sin conectar la Pico RX, LINE_A y LINE_B deben tomar +5 V, 0 V y -5 V respecto de masa."],
        ["2", "MATH = LINE_A - LINE_B debe mostrar aproximadamente +10 V, 0 V y -10 V."],
        ["3", "A 100 kbps: bit de 10 us, media celda activa de 5 us y retorno a cero."],
        ["4", "A 12,5 kbps: bit de 80 us, media celda activa de 40 us y retorno a cero."],
        ["5", "El front-end RX debe entregar GP4/GP5 entre 0 V y 3,3 V, sin valores negativos."],
        ["6", "El firmware debe volver a decodificar label, SDI, SSM, dato y paridad."],
    ]

    story: list = [
        p("Frente electrico ARINC 429 para banco", "title"),
        p("Propuesta corta para convertir la senal logica 0..3,3 V de una Pico en una senal bipolar diferencial tipo ARINC, y recuperarla luego como logica segura para GPIO.", "subtitle"),
        callout(
            "Alcance",
            "Esto es un banco de laboratorio. No debe conectarse a una linea ARINC real de campo hasta agregar proteccion, impedancia, aislamiento, validacion ambiental y criterios de certificacion.",
            PALE_RED,
            RED,
        ),
        Spacer(1, 0.09 * inch),
        p("1. Idea conceptual", "h1"),
        p("ARINC 429 no es una senal simple de un pin contra masa. Es un par diferencial A/B con tres estados: HIGH, NULL y LOW. La etapa actual ya genera la logica diferencial correcta con dos GPIO, pero a 0..3,3 V. La nueva etapa debe traducir esa logica a niveles bipolares, y despues volver a traducirla a 0..3,3 V para que la Pico pueda recibirla.", "body"),
        simple_table(levels, [1.25 * inch, 1.65 * inch, 1.65 * inch, 1.75 * inch]),
        Spacer(1, 0.06 * inch),
        simple_table(logic, [1.45 * inch, 1.45 * inch, 1.45 * inch, 1.9 * inch]),
        Spacer(1, 0.08 * inch),
        architecture_diagram(),
        Spacer(1, 0.10 * inch),
        p("2. Transmisor electrico", "h1"),
        p("La solucion mas ordenada para el prototipo es usar amplificadores operacionales con fuente bipolar. No se recomienda pensar en un unico pin elevado a 5 V y luego invertido; lo correcto es generar dos conductores complementarios.", "body"),
        code_block(
            """
LINE_A = 1,5 * (TX_A - TX_B)
LINE_B = 1,5 * (TX_B - TX_A)

TX_A=3,3 V / TX_B=0 V -> LINE_A=+5 V, LINE_B=-5 V
TX_A=0 V / TX_B=3,3 V -> LINE_A=-5 V, LINE_B=+5 V
TX_A=0 V / TX_B=0 V -> LINE_A=0 V, LINE_B=0 V
"""
        ),
        p("El driver debe alimentarse con margen, por ejemplo +-6 V o +-7 V, para entregar +-5 V por conductor sin saturacion excesiva. La velocidad de flanco debe validarse con osciloscopio, especialmente en 100 kbps.", "body"),
        PageBreak(),
        p("3. Receptor electrico", "h1"),
        p("La Pico no debe recibir senales negativas ni diferenciales directas. El RX primero protege, despues reduce la amplitud y finalmente compara contra dos umbrales para reconstruir los dos GPIO logicos.", "body"),
        code_block(
            """
Vsense = 1,65 V + 0,1 * (LINE_A - LINE_B)

A-B=+10 V -> Vsense ~= 2,65 V -> RX_A=1, RX_B=0
A-B=  0 V -> Vsense ~= 1,65 V -> RX_A=0, RX_B=0
A-B=-10 V -> Vsense ~= 0,65 V -> RX_A=0, RX_B=1
"""
        ),
        p("Umbrales recomendados de inicio: comparador alto en 2,30 V y comparador bajo en 1,00 V. Esos umbrales dejan una zona NULL amplia alrededor de 1,65 V.", "body"),
        p("4. Lista de componentes", "h1"),
        p("Precios orientativos en pesos argentinos a julio de 2026. No son cotizacion cerrada; deben verificarse contra stock local al comprar.", "body"),
        simple_table(bom, [1.15 * inch, 1.55 * inch, 0.42 * inch, 0.92 * inch, 2.25 * inch]),
        PageBreak(),
        p("5. Herramientas", "h1"),
        simple_table(tools, [1.9 * inch, 2.6 * inch, 2.0 * inch]),
        Spacer(1, 0.09 * inch),
        p("6. Presupuesto orientativo", "h1"),
        callout("Minimo", "Usando instrumentos del laboratorio y componentes disponibles: 70.000 a 180.000 ARS.", LIGHT, LINE),
        Spacer(1, 0.05 * inch),
        callout("Recomendado", "Con mejores comparadores, op-amps rapidos, protecciones y montaje mas estable: 160.000 a 380.000 ARS.", PALE_GREEN, GREEN),
        Spacer(1, 0.05 * inch),
        callout("Con instrumental", "Si hubiera que comprar osciloscopio y fuente de banco, el presupuesto total puede superar 800.000 ARS.", PALE_ORANGE, ORANGE),
        Spacer(1, 0.08 * inch),
        p("7. Ensayos de aceptacion", "h1"),
        simple_table(tests, [0.55 * inch, 5.95 * inch]),
        Spacer(1, 0.08 * inch),
        p("8. Decision tecnica", "h1"),
        p("La ruta recomendada es empezar con un front-end analogico medible: op-amps para el TX bipolar y comparadores para el RX. Esta alternativa es mas clara para depurar que un puente con MOSFET discretos desde el primer ensayo, porque cada nodo se puede medir y ajustar por separado.", "body"),
        p("El MCP6562 es el candidato recomendado para el comparador del RX por velocidad y salida push-pull. El LM393/LM393B puede usarse si es lo unico disponible localmente, pero requiere pull-up y tiene menos margen. El TLV3702 queda descartado para 100 kbps por su retardo de propagacion, aunque podria funcionar en pruebas lentas de baja velocidad.", "body"),
        p("9. Referencias abiertas", "h1"),
        p("Raspberry Pi Pico: https://www.raspberrypi.com/products/raspberry-pi-pico/", "small"),
        p("TI LM393: https://www.ti.com/product/LM393", "small"),
        p("TI TLV3702: https://www.ti.com/product/TLV3702", "small"),
        p("Microchip MCP6562: https://www.microchip.com/en-us/product/MCP6562", "small"),
        p("Resumen abierto de niveles ARINC 429: https://fr.wikipedia.org/wiki/ARINC_429", "small"),
    ]
    return story


def build_pdf() -> None:
    PDF_DIR.mkdir(parents=True, exist_ok=True)
    doc = SimpleDocTemplate(
        str(PDF_PATH),
        pagesize=letter,
        rightMargin=0.82 * inch,
        leftMargin=0.82 * inch,
        topMargin=0.70 * inch,
        bottomMargin=0.65 * inch,
        title="Frente electrico ARINC 429 para banco",
        author="Proyecto PAMPA",
    )
    doc.build(build_story(), onFirstPage=footer, onLaterPages=footer)


if __name__ == "__main__":
    build_pdf()
    print(PDF_PATH)
