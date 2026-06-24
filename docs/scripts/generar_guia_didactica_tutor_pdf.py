from __future__ import annotations

from pathlib import Path
from xml.sax.saxutils import escape

from PIL import Image as PILImage
from PIL import ImageDraw, ImageFont
from reportlab.lib import colors
from reportlab.lib.enums import TA_CENTER, TA_LEFT
from reportlab.lib.pagesizes import letter
from reportlab.lib.styles import ParagraphStyle, getSampleStyleSheet
from reportlab.lib.units import inch
from reportlab.pdfbase import pdfmetrics
from reportlab.pdfbase.ttfonts import TTFont
from reportlab.platypus import (
    Image,
    KeepTogether,
    PageBreak,
    Paragraph,
    SimpleDocTemplate,
    Spacer,
    Table,
    TableStyle,
)

from generar_informe_descriptivo_sistema import (
    ASSET_DIR as REPORT_ASSET_DIR,
    DOCS,
    FWD_DIFF,
    FWD_SINGLE,
    REV_DIFF,
    create_architecture_diagram,
    create_arinc_model_diagram,
    create_internal_sniffer_host_diagram,
    create_internal_tx_rx_diagram,
    create_network_diagram,
    create_results_diagram,
    create_wiring_diagram,
)


PDF_DIR = DOCS / "pdf"
PDF_PATH = PDF_DIR / "guia_didactica_tutor_arinc429.pdf"
ASSET_DIR = DOCS / "entrega_tutor" / "recursos_guia_didactica"

NAVY = colors.HexColor("#17324D")
BLUE = colors.HexColor("#2E74B5")
CYAN = colors.HexColor("#2D8C9F")
GREEN = colors.HexColor("#2F7D5A")
GOLD = colors.HexColor("#B7791F")
RED = colors.HexColor("#A33A3A")
INK = colors.HexColor("#1E2933")
MUTED = colors.HexColor("#5D6975")
LIGHT = colors.HexColor("#F2F4F7")
PALE_BLUE = colors.HexColor("#E8EEF5")
PALE_GREEN = colors.HexColor("#E8F3ED")
PALE_GOLD = colors.HexColor("#FFF4DC")
PALE_RED = colors.HexColor("#FBEAEA")


def register_fonts() -> tuple[str, str]:
    regular = Path("C:/Windows/Fonts/calibri.ttf")
    bold = Path("C:/Windows/Fonts/calibrib.ttf")
    if regular.exists() and bold.exists():
        pdfmetrics.registerFont(TTFont("Calibri", str(regular)))
        pdfmetrics.registerFont(TTFont("Calibri-Bold", str(bold)))
        return "Calibri", "Calibri-Bold"
    return "Helvetica", "Helvetica-Bold"


BODY_FONT, BOLD_FONT = register_fonts()


def font_path(bold: bool = False) -> str:
    candidate = Path("C:/Windows/Fonts/calibrib.ttf" if bold else "C:/Windows/Fonts/calibri.ttf")
    if candidate.exists():
        return str(candidate)
    fallback = Path("C:/Windows/Fonts/arialbd.ttf" if bold else "C:/Windows/Fonts/arial.ttf")
    return str(fallback)


def pil_font(size: int, bold: bool = False) -> ImageFont.FreeTypeFont:
    return ImageFont.truetype(font_path(bold), size=size)


def styles() -> dict[str, ParagraphStyle]:
    sample = getSampleStyleSheet()
    return {
        "title": ParagraphStyle(
            "Title",
            parent=sample["Title"],
            fontName=BOLD_FONT,
            fontSize=24,
            leading=28,
            alignment=TA_LEFT,
            textColor=NAVY,
            spaceAfter=7,
        ),
        "subtitle": ParagraphStyle(
            "Subtitle",
            parent=sample["BodyText"],
            fontName=BODY_FONT,
            fontSize=12.4,
            leading=15,
            textColor=MUTED,
            spaceAfter=12,
        ),
        "kicker": ParagraphStyle(
            "Kicker",
            parent=sample["BodyText"],
            fontName=BOLD_FONT,
            fontSize=9.6,
            leading=11,
            textColor=GOLD,
            spaceAfter=5,
        ),
        "h1": ParagraphStyle(
            "H1",
            parent=sample["Heading1"],
            fontName=BOLD_FONT,
            fontSize=14.2,
            leading=17,
            textColor=BLUE,
            spaceBefore=0,
            spaceAfter=5,
        ),
        "h2": ParagraphStyle(
            "H2",
            parent=sample["Heading2"],
            fontName=BOLD_FONT,
            fontSize=10.8,
            leading=12.5,
            textColor=NAVY,
            spaceBefore=4,
            spaceAfter=3,
        ),
        "body": ParagraphStyle(
            "Body",
            parent=sample["BodyText"],
            fontName=BODY_FONT,
            fontSize=9.1,
            leading=10.9,
            textColor=INK,
            spaceAfter=4,
        ),
        "small": ParagraphStyle(
            "Small",
            parent=sample["BodyText"],
            fontName=BODY_FONT,
            fontSize=8.2,
            leading=9.8,
            textColor=INK,
            spaceAfter=3,
        ),
        "caption": ParagraphStyle(
            "Caption",
            parent=sample["BodyText"],
            fontName=BODY_FONT,
            fontSize=7.6,
            leading=9,
            textColor=MUTED,
            alignment=TA_CENTER,
            spaceBefore=2,
            spaceAfter=5,
        ),
        "card_title": ParagraphStyle(
            "CardTitle",
            parent=sample["BodyText"],
            fontName=BOLD_FONT,
            fontSize=9.3,
            leading=10.8,
            textColor=NAVY,
            spaceAfter=2,
        ),
        "card_body": ParagraphStyle(
            "CardBody",
            parent=sample["BodyText"],
            fontName=BODY_FONT,
            fontSize=8.35,
            leading=9.9,
            textColor=INK,
            spaceAfter=0,
        ),
        "table_header": ParagraphStyle(
            "TableHeader",
            parent=sample["BodyText"],
            fontName=BOLD_FONT,
            fontSize=7.6,
            leading=8.8,
            textColor=NAVY,
            spaceAfter=0,
        ),
        "table_body": ParagraphStyle(
            "TableBody",
            parent=sample["BodyText"],
            fontName=BODY_FONT,
            fontSize=7.45,
            leading=8.7,
            textColor=INK,
            spaceAfter=0,
        ),
    }


ST = styles()


def safe(text: str) -> str:
    return escape(text).replace("\n", "<br/>")


def p(text: str, style: str = "body") -> Paragraph:
    return Paragraph(safe(text), ST[style])


def rich(text: str, style: str = "body") -> Paragraph:
    return Paragraph(text, ST[style])


def footer(canvas, doc) -> None:
    canvas.saveState()
    canvas.setStrokeColor(colors.HexColor("#D7DEE6"))
    canvas.setLineWidth(0.5)
    canvas.line(doc.leftMargin, letter[1] - 0.52 * inch, letter[0] - doc.rightMargin, letter[1] - 0.52 * inch)
    canvas.setFillColor(MUTED)
    canvas.setFont(BOLD_FONT, 7.5)
    canvas.drawString(doc.leftMargin, letter[1] - 0.43 * inch, "PAMPA / ARINC 429  |  Guia didactica para explicacion tecnica")
    canvas.setFont(BODY_FONT, 8)
    canvas.drawRightString(letter[0] - doc.rightMargin, 0.42 * inch, f"Pagina {doc.page}")
    canvas.restoreState()


def image_flow(path: Path, width: float, caption: str) -> list:
    with PILImage.open(path) as image:
        ratio = image.height / image.width
    flow = Image(str(path), width=width * inch, height=width * ratio * inch)
    return [flow, p(caption, "caption")]


def table_flow(headers: list[str], rows: list[list[str]], widths: list[float]) -> Table:
    data = [[p(header, "table_header") for header in headers]]
    data.extend([[p(value, "table_body") for value in row] for row in rows])
    table = Table(data, colWidths=[value * inch for value in widths], repeatRows=1, hAlign="CENTER")
    table.setStyle(
        TableStyle(
            [
                ("BACKGROUND", (0, 0), (-1, 0), LIGHT),
                ("GRID", (0, 0), (-1, -1), 0.45, colors.HexColor("#BAC4CE")),
                ("VALIGN", (0, 0), (-1, -1), "MIDDLE"),
                ("LEFTPADDING", (0, 0), (-1, -1), 5),
                ("RIGHTPADDING", (0, 0), (-1, -1), 5),
                ("TOPPADDING", (0, 0), (-1, -1), 4),
                ("BOTTOMPADDING", (0, 0), (-1, -1), 4),
            ]
        )
    )
    return table


def callout(title: str, text: str, fill=PALE_BLUE, accent=BLUE) -> Table:
    content = [[rich(f'<font color="{accent.hexval()}">{safe(title)}</font>', "card_title")], [p(text, "card_body")]]
    table = Table(content, colWidths=[6.5 * inch], hAlign="CENTER")
    table.setStyle(
        TableStyle(
            [
                ("BACKGROUND", (0, 0), (-1, -1), fill),
                ("BOX", (0, 0), (-1, -1), 0.7, accent),
                ("LEFTPADDING", (0, 0), (-1, -1), 8),
                ("RIGHTPADDING", (0, 0), (-1, -1), 8),
                ("TOPPADDING", (0, 0), (-1, 0), 6),
                ("BOTTOMPADDING", (0, 0), (-1, -1), 6),
            ]
        )
    )
    return table


def explainer(title: str, simple: str, technical: str, phrase: str) -> KeepTogether:
    data = [
        [rich(f'<font color="{BLUE.hexval()}">{safe(title)}</font>', "card_title")],
        [p(f"En criollo: {simple}", "card_body")],
        [p(f"Detalle tecnico: {technical}", "card_body")],
        [p(f"Frase defendible: {phrase}", "card_body")],
    ]
    table = Table(data, colWidths=[6.5 * inch], hAlign="CENTER")
    table.setStyle(
        TableStyle(
            [
                ("BACKGROUND", (0, 0), (-1, -1), colors.white),
                ("BOX", (0, 0), (-1, -1), 0.65, colors.HexColor("#BAC4CE")),
                ("LINEBELOW", (0, 0), (-1, 0), 0.55, colors.HexColor("#D7DEE6")),
                ("LEFTPADDING", (0, 0), (-1, -1), 8),
                ("RIGHTPADDING", (0, 0), (-1, -1), 8),
                ("TOPPADDING", (0, 0), (-1, -1), 5),
                ("BOTTOMPADDING", (0, 0), (-1, -1), 5),
            ]
        )
    )
    return KeepTogether([table, Spacer(1, 0.065 * inch)])


def section(number: str, title: str, intro: str | None = None) -> list:
    flow: list = [p(f"{number}. {title}", "h1")]
    if intro:
        flow.append(p(intro, "small"))
    return flow


def draw_centered(draw: ImageDraw.ImageDraw, box: tuple[int, int, int, int], text: str, font, color: str) -> None:
    x1, y1, x2, y2 = box
    lines = text.split("\n")
    heights = [draw.textbbox((0, 0), line, font=font)[3] for line in lines]
    total = sum(heights) + 8 * (len(lines) - 1)
    y = y1 + (y2 - y1 - total) / 2
    for line, height in zip(lines, heights):
        width = draw.textbbox((0, 0), line, font=font)[2]
        draw.text((x1 + (x2 - x1 - width) / 2, y), line, font=font, fill=color)
        y += height + 8


def create_scope_explainer() -> Path:
    ASSET_DIR.mkdir(parents=True, exist_ok=True)
    path = ASSET_DIR / "osciloscopio_ch1_ch2_math.png"
    image = PILImage.new("RGB", (1650, 720), "#FFFFFF")
    draw = ImageDraw.Draw(image)
    title = pil_font(38, True)
    body = pil_font(25)
    bold = pil_font(27, True)
    small = pil_font(21)

    draw.text((55, 35), "Como leer CH1, CH2 y MATH en el osciloscopio", font=title, fill="#17324D")
    draw.text((55, 88), "El azul/celeste es un canal fisico contra masa; el rojo es la resta matematica CH1 - CH2.", font=body, fill="#5D6975")
    draw.line((55, 128, 1595, 128), fill="#E8EEF5", width=4)

    y0 = 350
    for x in range(80, 1580, 80):
        draw.line((x, 180, x, 615), fill="#EEF2F5", width=1)
    for y in [220, y0, 480]:
        draw.line((80, y, 1580, y), fill="#BAC4CE" if y == y0 else "#E1E7ED", width=2 if y == y0 else 1)
    draw.text((90, 205), "+3,3 V", font=small, fill="#2D8C9F")
    draw.text((90, y0 + 8), "0 V / NULL", font=small, fill="#5D6975")
    draw.text((90, 468), "-3,3 V", font=small, fill="#A33A3A")

    def pulse(x1: int, x2: int, level_y: int, color: str, width: int = 8) -> None:
        draw.line((x1, y0, x1, level_y), fill=color, width=width)
        draw.line((x1, level_y, x2, level_y), fill=color, width=width)
        draw.line((x2, level_y, x2, y0), fill=color, width=width)

    # Case A: CH1 high, CH2 low -> MATH positive.
    pulse(260, 360, 220, "#F2C94C", 7)
    pulse(260, 360, 220, "#A33A3A", 6)
    draw.text((240, 540), "CH1 alto, CH2 bajo", font=bold, fill="#17324D")
    draw.text((240, 575), "CH1 - CH2 da positivo", font=body, fill="#A33A3A")

    # Case B: CH2 high, CH1 low -> MATH negative and mirrored against CH2.
    pulse(720, 820, 220, "#2D8C9F", 7)
    pulse(720, 820, 480, "#A33A3A", 6)
    draw.text((675, 540), "CH2 alto, CH1 bajo", font=bold, fill="#17324D")
    draw.text((675, 575), "CH1 - CH2 da negativo", font=body, fill="#A33A3A")

    # Case C: both low -> NULL.
    draw.line((1120, y0, 1360, y0), fill="#5D6975", width=8)
    draw.text((1110, 540), "Ambos bajos", font=bold, fill="#17324D")
    draw.text((1110, 575), "La resta da 0: NULL", font=body, fill="#5D6975")

    draw.rounded_rectangle((1120, 200, 1530, 320), radius=16, fill="#FFF4DC", outline="#B7791F", width=3)
    draw_centered(
        draw,
        (1140, 210, 1510, 310),
        "Si se invierte la resta\no las puntas de prueba,\nse invierte el signo.",
        small,
        "#1E2933",
    )
    image.save(path, quality=95)
    return path


def build_pdf() -> None:
    PDF_DIR.mkdir(parents=True, exist_ok=True)
    ASSET_DIR.mkdir(parents=True, exist_ok=True)
    REPORT_ASSET_DIR.mkdir(parents=True, exist_ok=True)
    diagrams = {
        "architecture": create_architecture_diagram(),
        "wiring": create_wiring_diagram(),
        "arinc": create_arinc_model_diagram(),
        "tx_rx": create_internal_tx_rx_diagram(),
        "sniffer_host": create_internal_sniffer_host_diagram(),
        "network": create_network_diagram(),
        "results": create_results_diagram(),
        "scope": create_scope_explainer(),
    }

    doc = SimpleDocTemplate(
        str(PDF_PATH),
        pagesize=letter,
        rightMargin=1.0 * inch,
        leftMargin=1.0 * inch,
        topMargin=0.7 * inch,
        bottomMargin=0.65 * inch,
        title="Guia didactica PAMPA / ARINC 429 para explicacion tecnica",
        author="Proyecto PAMPA",
    )
    story: list = []

    story.extend(
        [
            Spacer(1, 0.2 * inch),
            p("GUIA DIDACTICA PARA EXPLICACION TECNICA", "kicker"),
            p("PAMPA / ARINC 429 - de la palabra al dashboard", "title"),
            p("Resumen para explicar el sistema completo con lenguaje simple, sin perder precision tecnica.", "subtitle"),
            table_flow(
                ["Fase", "Rama", "Modo recomendado", "Estado"],
                [["arinc429_logic", "ARINC", "stream + sniffer PIO-frame 8 MHz", "validado"]],
                [1.45, 1.05, 2.85, 1.15],
            ),
            Spacer(1, 0.10 * inch),
        ]
    )
    story.extend(image_flow(diagrams["architecture"], 6.05, "Cadena completa: TX genera, RX recibe, SNIFFER observa, 3B+ publica, notebook visualiza."))
    story.append(
        callout(
            "Idea central",
            "El proyecto no intenta decir que un GPIO sea una linea ARINC real. Lo que se valido es la logica de palabra, la temporizacion de 100 kbps, la captura pasiva, la integridad por paridad, el transporte SPI por trama completa y la visualizacion.",
            PALE_GREEN,
            GREEN,
        )
    )
    story.append(PageBreak())

    story.extend(section("1", "Lectura de extremo a extremo", "Esta es la explicacion corta del recorrido de una palabra desde que nace hasta que aparece en pantalla."))
    story.append(explainer(
        "Flujo general",
        "TX arma una palabra, la saca por dos GPIO, RX la interpreta y el SNIFFER la escucha como si estuviera mirando el cable desde el costado.",
        "FWD es el canal simplex principal. El SNIFFER toma FWD/REV en paralelo y no transmite sobre esas lineas; solamente responde por SPI a la 3B+.",
        "El SNIFFER no conversa con TX ni con RX por ARINC: solo observa el cable y reporta por otro enlace.",
    ))
    story.append(explainer(
        "Por que hay Raspberry Pi 3B+",
        "La Pico SNIFFER se concentra en capturar. La 3B+ hace de puente: pregunta por SPI, convierte a JSON y deja los datos disponibles.",
        "El bridge HTTP escucha en 127.0.0.1:5100 dentro de la 3B+. Flask consulta ese bridge local y expone el dashboard en 192.168.50.2:5000.",
        "Separamos adquisicion dura de visualizacion: la Pico captura, la 3B+ sirve datos, la notebook mira.",
    ))
    story.append(explainer(
        "Por que Ethernet directo",
        "Con Wi-Fi aparecia mas variacion y mas riesgo de saturar la 3B+. Con cable directo queda mas estable y repetible.",
        "La red recomendada es notebook 192.168.50.1/24 y 3B+ 192.168.50.2/24. Solo Flask queda visible hacia la notebook.",
        "El enlace operativo no depende de Internet ni del router: es una red local corta y controlada.",
    ))
    story.append(PageBreak())

    story.extend(section("2", "La palabra ARINC logica", "Antes de hablar de cables, el sistema arma una palabra de 32 bits con campos definidos."))
    story.extend(image_flow(diagrams["arinc"], 6.15, "Palabra de 32 bits y bit time de 10 us con retorno a cero."))
    story.append(explainer(
        "Como nace la palabra",
        "Una palabra es un paquete chico de 32 bits: trae un nombre de variable, un identificador, el dato, estado y paridad.",
        "Los campos usados son Label, SDI, DATA, SSM y paridad impar. Ejemplos utiles: 0xA5 temperatura, 0xB1 velocidad, 0xC2 altitud.",
        "No se manda texto por el cable: se manda una palabra binaria que despues se interpreta por label y SDI.",
    ))
    story.append(explainer(
        "Para que sirve la paridad",
        "La paridad es un control rapido: si una palabra llega con un bit mal, queda marcada como invalida.",
        "En la version endurecida, una palabra con mala paridad suma error, pero no entra a accepted_words ni actualiza el snapshot.",
        "El sistema prefiere perder una palabra dudosa antes que mostrar un dato corrupto como valido.",
    ))
    story.append(PageBreak())

    story.extend(section("3", "ARINC-TX: generacion y transmision", "El TX no dibuja pulsos desde la CPU bit por bit. La CPU arma palabras y el PIO mantiene el tiempo fino."))
    story.extend(image_flow(diagrams["tx_rx"], 6.2, "Camino interno resumido de TX y RX: CPU, FIFO, PIO y procesamiento."))
    story.append(explainer(
        "Que hace TX",
        "TX inventa valores de laboratorio, por ejemplo temperatura, velocidad y altitud, y los empaqueta en palabras ARINC logicas.",
        "El target recomendado es arinc_tx_arinc429_logic_stream. Emite rafagas variables y pausas variables; no depende de bloques fijos de 1000 palabras.",
        "La transmision actual es stream: no espera ACK para seguir funcionando.",
    ))
    story.append(explainer(
        "Por que usamos PIO",
        "PIO es como un asistente de hardware dentro de la Pico: una vez cargada la palabra, mantiene los tiempos sin distraerse.",
        "La CPU coloca palabras en el TX FIFO; la state machine PIO usa OSR/X/Y para desplazar bit por bit a 100 kbps.",
        "La temporizacion de 10 us por bit no queda atada a la velocidad variable del codigo C.",
    ))
    story.append(PageBreak())

    story.extend(section("4", "Linea FWD y codificacion bipolar RZ", "En esta fase el nivel electrico es GPIO de 3,3 V, pero se reproduce la idea diferencial y el retorno a cero."))
    story.extend(image_flow(FWD_SINGLE, 5.55, "Canal individual respecto de masa: pulso de 0 a 3,3 V y retorno a cero."))
    story.append(explainer(
        "Que significa retorno a cero",
        "Cada bit no queda alto durante todo el tiempo. Primero aparece el pulso activo y despues vuelve a cero antes del siguiente bit.",
        "Cada bit dura 10 us. Durante 5 us hay nivel activo y durante 5 us hay NULL. Por eso la captura muestra pulsos de ancho cercano a 5 us.",
        "El cero entre pulsos no es falta de senal: es parte de la codificacion RZ.",
    ))
    story.append(explainer(
        "Por que hay dos conductores",
        "La informacion sale como diferencia entre dos lineas. A veces domina una, a veces domina la otra, y a veces ambas quedan en cero.",
        "En laboratorio se implementa con dos GPIO complementarios. En MATH = CH1 - CH2 aparecen niveles positivos, negativos y cero.",
        "Lo importante no es solo cada canal contra masa, sino la resta entre ambos.",
    ))
    story.append(PageBreak())

    story.extend(section("5", "Osciloscopio: rojo, azul y la resta CH1 - CH2", "Esta es la explicacion practica para la duda de los pulsos espejados."))
    story.extend(image_flow(diagrams["scope"], 6.15, "Guia de lectura de CH1, CH2 y MATH en el osciloscopio."))
    story.extend(image_flow(FWD_DIFF, 5.7, "Captura real: MATH = CH1 - CH2 con niveles positivo, nulo y negativo."))
    story.append(explainer(
        "Pulsos celestes y rojos",
        "El celeste es CH2 medido contra masa. El rojo es la cuenta que hace el osciloscopio: CH1 menos CH2.",
        "Si CH2 esta alto y CH1 bajo, CH1 - CH2 da negativo; por eso se ve celeste hacia arriba y rojo hacia abajo. Si CH1 esta alto y CH2 bajo, la resta da positiva y el rojo va hacia arriba.",
        "El pulso celeste no es otra palabra: es una linea fisica. El rojo es la resta matematica. Positivo y negativo son las dos polaridades usadas para representar bits.",
    ))
    story.append(PageBreak())

    story.extend(section("6", "ARINC-RX: recepcion y procesamiento", "RX reconstruye la palabra desde la linea, valida y actualiza su estado interno."))
    story.append(explainer(
        "Que hace RX",
        "RX mira el par FWD, detecta los pulsos, reconstruye la palabra y la interpreta.",
        "El PIO RX detecta simbolos HI/LO/NULL, reconstruye 32 bits y empuja palabras al RX FIFO para que la CPU las procese.",
        "La CPU no tiene que adivinar el tiempo exacto de cada bit; recibe palabras reconstruidas desde el PIO.",
    ))
    story.append(explainer(
        "Que es el FIFO",
        "El FIFO es una fila corta entre PIO y CPU. Sirve para que una palabra no se pierda si la CPU tarda muy poco en atender.",
        "En RX conserva hasta 4 palabras. Por eso el lazo de captura debe drenar seguido y no hacer tareas largas como imprimir demasiado o procesar archivos.",
        "El FIFO no es una base de datos: es un amortiguador corto entre hardware y software.",
    ))
    story.append(explainer(
        "REV y ACK",
        "En el modo historico RX podia responder ACK por REV. En el modo stream final no hace falta: RX recibe y procesa sin devolver confirmacion.",
        "ARINC 429 real es simplex. Si un equipo necesita responder, normalmente usa otro canal fisico en sentido inverso.",
        "REV no es el mismo bus hablando al reves: es otro enlace simplex, y en el modo final queda inactivo.",
    ))
    story.append(PageBreak())

    story.extend(section("7", "ARINC-SNIFFER: captura pasiva", "El SNIFFER es la pieza central del proyecto: observa sin intervenir y entrega una vista limpia de los datos."))
    story.extend(image_flow(diagrams["sniffer_host"], 6.2, "Ruta interna: captura PIO, paridad, filtro, snapshot, SPI y bridge."))
    story.append(explainer(
        "Donde se conecta el SNIFFER",
        "Se conecta al cable en paralelo. No va 'entre' TX y RX como si cortara la linea.",
        "Sus entradas GP2/GP3 observan FWD y GP4/GP5 observan REV opcional. No transmite en esos pines.",
        "Es un observador de alta prioridad logica: escucha todo lo que puede, pero no modifica el enlace.",
    ))
    story.append(explainer(
        "Filtro Label/SDI",
        "No todo lo que pasa por el cable interesa. El filtro deja pasar solo combinaciones conocidas.",
        "La whitelist actual acepta 0xA5/0, 0xB1/1, 0xC2/2 y conserva 0xAC/3 como ACK historico.",
        "Una palabra puede estar bien formada y aun asi ser filtrada si no pertenece al conjunto de interes.",
    ))
    story.append(explainer(
        "Snapshot",
        "En vez de mandar millones de palabras a la 3B+, el SNIFFER conserva el ultimo valor y contadores por variable.",
        "El snapshot tiene 16 slots. Cada slot se identifica por canal, label y SDI, e incluye valor, SSM, contador y revision.",
        "La 3B+ consulta estado reciente y contadores; no necesita copiar cada palabra cruda.",
    ))
    story.append(PageBreak())

    story.extend(section("8", "SPI entre SNIFFER y Raspberry Pi 3B+", "SPI no reemplaza a ARINC. Es solo el enlace interno entre el sniffer y el host."))
    story.append(explainer(
        "Por que SPI a 8 MHz si ARINC va a 100 kbps",
        "Son enlaces distintos. ARINC es el cable observado; SPI es el camino para sacar datos del SNIFFER hacia la 3B+.",
        "SPI a 8 MHz da margen para polling, respuestas de 32 bytes, estadisticas y dashboard sin apretar el sistema.",
        "Que ARINC sea 100 kbps no obliga a que SPI tambien sea 100 kbps.",
    ))
    story.append(explainer(
        "Por que no 50 MHz",
        "Porque el maximo teorico no es lo mismo que el maximo estable del sistema real con cables, PIO, CS manual y Linux.",
        "El barrido mostro 8 y 9 MHz PASS, 10 MHz parcial, 16 y 50 MHz FAIL. Por margen se eligio 8 MHz.",
        "La frecuencia correcta es la mas rapida estable, no la mas grande que aparece en una hoja de datos.",
    ))
    story.append(explainer(
        "Trama completa, no byte suelto",
        "La objecion del byte a byte se resolvio usando PIO-frame: el intercambio se organiza como paquetes de 32 bytes.",
        "El host manda request de 32 bytes, espera 2 ms y luego relojea response de 32 bytes. El slave SPI esta implementado con PIO.",
        "La unidad logica de SPI ahora es la trama completa de protocolo.",
    ))
    story.append(explainer(
        "Errores SPI de arranque",
        "Si el bridge arranca antes que la Pico, puede consultar cuando todavia no hay respuesta valida. Eso no es un error operativo del enlace.",
        "Se separaron spi_startup_errors y spi_errors. spi_errors empieza a contar despues de la primera respuesta SPI valida.",
        "La corrida final tuvo spi_errors operativos en cero.",
    ))
    story.append(PageBreak())

    story.extend(section("9", "Bridge, Flask, Prometheus y Grafana", "La visualizacion esta separada en una vista operativa rapida y una vista historica."))
    story.extend(image_flow(diagrams["network"], 6.2, "Servicios y direcciones IP recomendadas."))
    story.append(explainer(
        "Bridge HTTP",
        "El bridge es un traductor local: habla SPI con el SNIFFER y HTTP/JSON con Flask.",
        "Escucha en 127.0.0.1:5100. Eso significa que queda dentro de la 3B+ y no se publica directamente a la notebook.",
        "El control bajo nivel de SPI queda encerrado en la Raspberry.",
    ))
    story.append(explainer(
        "Dashboard Flask",
        "Flask es la pantalla de uso diario. Muestra valores actuales, contadores, errores y estado del enlace.",
        "Corre en 0.0.0.0:5000 para que la notebook entre por http://192.168.50.2:5000.",
        "Flask es la vista operativa rapida: sirve para mirar si el sistema esta vivo y sano.",
    ))
    story.append(explainer(
        "Prometheus y Grafana",
        "Prometheus guarda series temporales y Grafana las grafica. No reemplazan al dashboard, lo complementan.",
        "Prometheus consulta /metrics de Flask desde la notebook y Grafana usa esos datos para historico.",
        "Flask responde que pasa ahora; Prometheus/Grafana responden que paso durante horas.",
    ))
    story.append(PageBreak())

    story.extend(section("10", "Estadistica, exportacion y evidencias", "El proyecto no depende solo de mirar la pantalla: genera evidencia exportable."))
    story.extend(image_flow(diagrams["results"], 6.15, "Resumen visual de contadores de la corrida final."))
    story.append(
        table_flow(
            ["Prueba", "Resultado principal", "Lectura"],
            [
                ["Corrida final 8 h", "58.273.540 recibidas, 17.482.062 aceptadas, 40.791.478 filtradas", "PASS"],
                ["Errores operativos", "spi_errors=0, parity_errors=0, overflow=0, drops=0", "sin fallas de transporte"],
                ["Paridad estricta", "6.109 errores inyectados y descartados", "filtro de integridad validado"],
                ["Barrido SPI", "8/9 MHz PASS, 10 MHz parcial, 16/50 MHz FAIL", "8 MHz recomendado"],
            ],
            [1.55, 3.95, 1.0],
        )
    )
    story.append(explainer(
        "stats_recorder",
        "Durante una prueba se puede dejar la 3B+ registrando muestras y eventos. Al cerrar, quedan archivos para revisar.",
        "El recorder genera samples.csv, events.csv, summary.json y report.pdf. Eso permite analizar sin depender del chat ni de capturas manuales.",
        "La prueba deja evidencia reproducible: datos crudos, resumen y reporte.",
    ))
    story.append(PageBreak())

    story.extend(section("11", "Limites actuales y siguiente fase", "Esta seccion conviene decirla con claridad: lo validado es fuerte, pero tiene frontera definida."))
    story.append(explainer(
        "Que se valido",
        "Se valido la cadena logica completa: palabra, temporizacion, captura pasiva, integridad, SPI, bridge, dashboard y observabilidad.",
        "La fase final quedo reproducible con targets stream, sniffer PIO-frame, SPI a 8 MHz y red Ethernet directa.",
        "La capa digital del prototipo ya tiene una base estable para seguir.",
    ))
    story.append(explainer(
        "Que no es todavia",
        "Todavia no es un sniffer plug-and-play para enchufar a un bus ARINC real de avionica.",
        "Faltan receptor ARINC 429 real, proteccion, entrada de alta impedancia, tolerancia a niveles de campo, conectores, masa/blindaje y validacion electrica.",
        "No se debe conectar GP2/GP3 directo a una linea ARINC real.",
    ))
    story.append(explainer(
        "Como sigue",
        "El siguiente paso es agregar un frente electrico real delante de la Pico SNIFFER, sin tirar lo ya hecho.",
        "La captura, filtros, protocolo SPI, bridge, Flask, Prometheus, Grafana y recorder pueden reutilizarse detras del receptor ARINC dedicado.",
        "La evolucion natural es cambiar la entrada electrica, no reescribir todo el sistema.",
    ))
    story.append(callout(
        "Cierre defendible",
        "La etapa actual demuestra que la arquitectura digital funciona y que el cuello pendiente esta en la interfaz fisica ARINC real. Ese es el proximo desarrollo de hardware.",
        PALE_BLUE,
        BLUE,
    ))
    story.append(PageBreak())

    story.extend(section("12", "Respuestas cortas ante preguntas previsibles", "Estas frases sirven como guia oral si el tutor apunta a puntos concretos."))
    story.append(
        table_flow(
            ["Pregunta", "Respuesta breve"],
            [
                ["Por que FWD y REV?", "Porque son dos sentidos logicos. En ARINC real cada canal es simplex; si hay retorno se usa otro canal."],
                ["El SNIFFER transmite?", "No sobre ARINC. Solo responde por SPI hacia la 3B+ cuando el host le consulta."],
                ["SPI o UART/I2C?", "SPI da mas margen y menor overhead. I2C no conviene por direccionamiento/latencia; UART seria simple pero menos flexible para paquetes y control."],
                ["Por que bridge local?", "Para no exponer el control SPI a la red. Flask consume por 127.0.0.1 y la notebook ve solo Flask."],
                ["Por que no contar startup como error operativo?", "Porque consultar antes de que la Pico responda no mide la calidad del enlace en regimen."],
                ["Que representa el rojo del osciloscopio?", "La resta CH1 - CH2. Si CH2 esta alto y CH1 bajo, la resta se ve negativa."],
                ["Que falta para campo real?", "Receptor ARINC, proteccion, alta impedancia, aislamiento/conectores y validacion con fuente ARINC real."],
            ],
            [2.1, 4.4],
        )
    )
    story.append(Spacer(1, 0.14 * inch))
    story.append(p("Fuentes internas: PROJECT_CONTEXT.md, README.md, FASE2_ARINC429_LOGIC.md, evidencias de prueba, diagramas internos y capturas de imagenes/.", "caption"))

    doc.build(story, onFirstPage=footer, onLaterPages=footer)
    print(PDF_PATH)


if __name__ == "__main__":
    build_pdf()
