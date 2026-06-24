from __future__ import annotations

from pathlib import Path

from PIL import Image as PILImage
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
    ASSET_DIR,
    DOCS,
    FWD_DIFF,
    FWD_SINGLE,
    REV_DIFF,
    REV_SINGLE,
    create_architecture_diagram,
    create_arinc_model_diagram,
    create_internal_sniffer_host_diagram,
    create_internal_tx_rx_diagram,
    create_network_diagram,
    create_results_diagram,
    create_wiring_diagram,
)


PDF_DIR = DOCS / "pdf"
PDF_PATH = PDF_DIR / "informe_descriptivo_sistema_arinc429.pdf"

NAVY = colors.HexColor("#17324D")
BLUE = colors.HexColor("#2E74B5")
GREEN = colors.HexColor("#2F7D5A")
GOLD = colors.HexColor("#B7791F")
RED = colors.HexColor("#A33A3A")
INK = colors.HexColor("#1E2933")
MUTED = colors.HexColor("#5D6975")
LIGHT = colors.HexColor("#F2F4F7")
PALE_BLUE = colors.HexColor("#E8EEF5")
PALE_GREEN = colors.HexColor("#E8F3ED")
PALE_GOLD = colors.HexColor("#FFF4DC")


def register_fonts() -> tuple[str, str]:
    regular = Path("C:/Windows/Fonts/calibri.ttf")
    bold = Path("C:/Windows/Fonts/calibrib.ttf")
    if regular.exists() and bold.exists():
        pdfmetrics.registerFont(TTFont("Calibri", str(regular)))
        pdfmetrics.registerFont(TTFont("Calibri-Bold", str(bold)))
        return "Calibri", "Calibri-Bold"
    return "Helvetica", "Helvetica-Bold"


BODY_FONT, BOLD_FONT = register_fonts()


def styles():
    sample = getSampleStyleSheet()
    return {
        "body": ParagraphStyle(
            "Body",
            parent=sample["BodyText"],
            fontName=BODY_FONT,
            fontSize=9.3,
            leading=11.3,
            textColor=INK,
            spaceAfter=5,
        ),
        "small": ParagraphStyle(
            "Small",
            parent=sample["BodyText"],
            fontName=BODY_FONT,
            fontSize=8.3,
            leading=10,
            textColor=INK,
            spaceAfter=3,
        ),
        "caption": ParagraphStyle(
            "Caption",
            parent=sample["BodyText"],
            fontName=BODY_FONT,
            fontSize=7.7,
            leading=9,
            textColor=MUTED,
            alignment=TA_CENTER,
            spaceBefore=2,
            spaceAfter=5,
        ),
        "h1": ParagraphStyle(
            "H1",
            parent=sample["Heading1"],
            fontName=BOLD_FONT,
            fontSize=15,
            leading=18,
            textColor=BLUE,
            spaceBefore=0,
            spaceAfter=6,
        ),
        "intro": ParagraphStyle(
            "Intro",
            parent=sample["BodyText"],
            fontName=BODY_FONT,
            fontSize=8.8,
            leading=10.5,
            textColor=MUTED,
            spaceAfter=7,
        ),
        "title": ParagraphStyle(
            "Title",
            parent=sample["Title"],
            fontName=BOLD_FONT,
            fontSize=25,
            leading=29,
            textColor=NAVY,
            alignment=TA_LEFT,
            spaceAfter=7,
        ),
        "subtitle": ParagraphStyle(
            "Subtitle",
            parent=sample["BodyText"],
            fontName=BODY_FONT,
            fontSize=13,
            leading=16,
            textColor=MUTED,
            spaceAfter=15,
        ),
        "kicker": ParagraphStyle(
            "Kicker",
            parent=sample["BodyText"],
            fontName=BOLD_FONT,
            fontSize=10,
            leading=12,
            textColor=GOLD,
            spaceAfter=6,
        ),
        "callout_title": ParagraphStyle(
            "CalloutTitle",
            parent=sample["BodyText"],
            fontName=BOLD_FONT,
            fontSize=9.2,
            leading=11,
            textColor=NAVY,
            spaceAfter=2,
        ),
        "callout_body": ParagraphStyle(
            "CalloutBody",
            parent=sample["BodyText"],
            fontName=BODY_FONT,
            fontSize=8.7,
            leading=10.4,
            textColor=INK,
            spaceAfter=0,
        ),
        "table_header": ParagraphStyle(
            "TableHeader",
            parent=sample["BodyText"],
            fontName=BOLD_FONT,
            fontSize=7.8,
            leading=9,
            textColor=NAVY,
            spaceAfter=0,
        ),
        "table_body": ParagraphStyle(
            "TableBody",
            parent=sample["BodyText"],
            fontName=BODY_FONT,
            fontSize=7.7,
            leading=9,
            textColor=INK,
            spaceAfter=0,
        ),
    }


ST = styles()


def footer(canvas, doc) -> None:
    canvas.saveState()
    canvas.setStrokeColor(colors.HexColor("#D7DEE6"))
    canvas.setLineWidth(0.5)
    canvas.line(doc.leftMargin, letter[1] - 0.52 * inch, letter[0] - doc.rightMargin, letter[1] - 0.52 * inch)
    canvas.setFillColor(MUTED)
    canvas.setFont(BOLD_FONT, 7.5)
    canvas.drawString(doc.leftMargin, letter[1] - 0.43 * inch, "PAMPA / ARINC 429  |  Descripción del sistema")
    canvas.setFont(BODY_FONT, 8)
    canvas.drawRightString(letter[0] - doc.rightMargin, 0.42 * inch, f"Página {doc.page}")
    canvas.restoreState()


def p(text: str, style: str = "body") -> Paragraph:
    return Paragraph(text, ST[style])


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
    content = [[p(f'<font color="{accent.hexval()}">{title}</font>', "callout_title")], [p(text, "callout_body")]]
    table = Table(content, colWidths=[6.5 * inch], hAlign="CENTER")
    table.setStyle(
        TableStyle(
            [
                ("BACKGROUND", (0, 0), (-1, -1), fill),
                ("BOX", (0, 0), (-1, -1), 0.7, accent),
                ("LEFTPADDING", (0, 0), (-1, -1), 9),
                ("RIGHTPADDING", (0, 0), (-1, -1), 9),
                ("TOPPADDING", (0, 0), (-1, 0), 7),
                ("BOTTOMPADDING", (0, 0), (-1, -1), 7),
            ]
        )
    )
    return table


def section(number: str, title: str, intro: str | None = None) -> list:
    result = [p(f"{number}. {title}", "h1")]
    if intro:
        result.append(p(intro, "intro"))
    return result


def build_pdf() -> None:
    PDF_DIR.mkdir(parents=True, exist_ok=True)
    ASSET_DIR.mkdir(parents=True, exist_ok=True)
    diagrams = {
        "architecture": create_architecture_diagram(),
        "wiring": create_wiring_diagram(),
        "arinc": create_arinc_model_diagram(),
        "tx_rx": create_internal_tx_rx_diagram(),
        "sniffer_host": create_internal_sniffer_host_diagram(),
        "network": create_network_diagram(),
        "results": create_results_diagram(),
    }

    doc = SimpleDocTemplate(
        str(PDF_PATH),
        pagesize=letter,
        rightMargin=1.0 * inch,
        leftMargin=1.0 * inch,
        topMargin=0.7 * inch,
        bottomMargin=0.65 * inch,
        title="Sistema PAMPA / ARINC 429 - Descripción de arquitectura y validación",
        author="Proyecto PAMPA",
    )
    story: list = []

    story.extend(
        [
            Spacer(1, 0.22 * inch),
            p("SISTEMA PAMPA / ARINC 429", "kicker"),
            p("Descripción de arquitectura y validación de la fase lógica", "title"),
            p("Transmisión, captura pasiva, transporte SPI y observabilidad", "subtitle"),
            table_flow(
                ["Versión", "Rama", "Estado", "Fecha"],
                [["arinc-logic-v1.0.0", "ARINC", "Fase lógica validada", "Junio de 2026"]],
                [1.65, 1.1, 2.45, 1.3],
            ),
            Spacer(1, 0.10 * inch),
        ]
    )
    story.extend(image_flow(diagrams["architecture"], 6.05, "Vista resumida del recorrido completo de los datos."))
    story.append(
        p(
            "El sistema reproduce en laboratorio la estructura lógica y temporal de ARINC 429 mediante "
            "tres Raspberry Pi Pico. Un transmisor genera palabras sobre FWD, un receptor las procesa y "
            "un tercer nodo actúa como observador pasivo. La captura se entrega por SPI a una Raspberry "
            "Pi 3B+, donde se publica mediante un bridge local, Flask y métricas.",
            "small",
        )
    )
    story.append(
        callout(
            "Resultado principal",
            "La configuración final operó ocho horas a 8 MHz sobre SPI PIO-frame, procesó 58.273.540 "
            "palabras y mantuvo en cero los errores SPI operativos, errores de paridad, overflow, drops "
            "y resincronizaciones FWD operativas.",
            PALE_GREEN,
            GREEN,
        )
    )
    story.append(PageBreak())

    story.extend(section("1", "Alcance y arquitectura general", "La implementación actual corresponde a una fase lógica y temporal; todavía no reproduce la interfaz eléctrica de campo."))
    story.extend(image_flow(diagrams["architecture"], 6.25, "Bloques funcionales y enlaces principales."))
    story.append(
        table_flow(
            ["Componente", "Función principal", "Interfaz"],
            [
                ["ARINC-TX", "Genera palabras en flujo continuo y ráfagas variables.", "FWD GP2/GP3"],
                ["ARINC-RX", "Reconstruye y procesa palabras. En stream no transmite ACK.", "FWD GP2/GP3"],
                ["ARINC-SNIFFER", "Toma pasiva sobre FWD/REV, valida paridad, filtra y mantiene snapshots.", "Derivación + SPI"],
                ["Raspberry Pi 3B+", "Extrae datos y ejecuta bridge, Flask y registro.", "SPI + Ethernet"],
                ["Notebook", "Visualización, histórico y análisis.", "Ethernet"],
            ],
            [1.35, 3.65, 1.5],
        )
    )
    story.append(Spacer(1, 0.09 * inch))
    story.append(callout("Pasividad del SNIFFER", "El SNIFFER se conecta en paralelo sobre los conductores FWD/REV; no forma un enlace punto a punto con TX o RX y nunca transmite sobre esas líneas. Su única salida es la respuesta SPI hacia la Raspberry Pi 3B+.", PALE_GOLD, GOLD))
    story.append(PageBreak())

    story.extend(section("2", "Conexionado físico", "TX y RX están unidos por el cable FWD. El SNIFFER no se conecta a una placa específica: sus entradas se derivan en paralelo desde ese mismo par de conductores."))
    story.extend(image_flow(diagrams["wiring"], 6.25, "Esquema de conexión lógica, SPI y Ethernet."))
    story.append(
        table_flow(
            ["Enlace", "Origen", "Destino", "Señales"],
            [
                ["FWD", "ARINC-TX", "ARINC-RX", "FWD_A/FWD_B; SNIFFER derivado en paralelo"],
                ["REV opcional", "Otro canal simplex", "Receptor del canal", "REV_A/REV_B; SNIFFER derivado en paralelo"],
                ["SPI", "Raspberry Pi", "SNIFFER", "MOSI GP16, CS GP17, SCK GP18, MISO GP19"],
                ["Ethernet", "Notebook", "Raspberry Pi", "192.168.50.1 ↔ 192.168.50.2"],
            ],
            [0.75, 1.25, 1.65, 2.85],
        )
    )
    story.append(Spacer(1, 0.08 * inch))
    story.append(callout("Advertencia", "Los GPIO de 3,3 V no se conectan directamente a una línea ARINC real. La siguiente fase requiere protección, entrada de alta impedancia y un receptor ARINC 429 dedicado.", PALE_GOLD, RED))
    story.append(PageBreak())

    story.extend(section("3", "Palabra y codificación ARINC lógica"))
    story.extend(image_flow(diagrams["arinc"], 6.25, "Estructura de palabra y temporización bipolar RZ empleada."))
    story.append(
        table_flow(
            ["Propiedad", "Implementación"],
            [
                ["Velocidad", "100 kbps; cada bit ocupa 10 µs."],
                ["Retorno a cero", "5 µs en nivel activo y 5 µs en nivel NULL."],
                ["Representación diferencial", "Aproximadamente +3,3 V, 0 V y -3,3 V en CH1 - CH2."],
                ["Palabra", "32 bits: Label, SDI, datos, SSM y paridad impar."],
                ["Aceptación", "Paridad correcta, Label/SDI habilitado y dato plausible."],
            ],
            [1.65, 4.85],
        )
    )
    story.append(Spacer(1, 0.08 * inch))
    story.append(p("Las combinaciones operativas principales son 0xA5/0 para temperatura, 0xB1/1 para velocidad y 0xC2/2 para altitud. El modo histórico de laboratorio también utilizó 0xAC/3 para ACK_BATCH.", "small"))
    story.append(PageBreak())

    story.extend(section("4", "Funcionamiento interno de TX y RX"))
    story.extend(image_flow(diagrams["tx_rx"], 6.25, "Movimiento de la palabra desde la aplicación hasta los GPIO y regreso a la CPU receptora."))
    story.append(
        table_flow(
            ["Bloque", "Comportamiento relevante"],
            [
                ["Generador TX", "Construye palabras, alterna SSM y forma ráfagas de 1 a 2000 palabras."],
                ["PIO TX", "Serializa a 100 kbps sin depender del tiempo de ejecución de la CPU."],
                ["PIO RX", "Muestrea el par lógico y reconstruye palabras de 32 bits."],
                ["FIFO RX", "Amortigua diferencias breves; conserva hasta cuatro palabras."],
                ["Lazo de captura", "Drena el FIFO y evita trabajo prolongado en la ruta crítica."],
            ],
            [1.55, 4.95],
        )
    )
    story.append(Spacer(1, 0.08 * inch))
    story.append(callout("Modo stream", "El flujo no depende de bloques fijos de 1000 palabras. La cantidad de tramas y los intervalos entre ráfagas varían; RX recibe continuamente y REV permanece inactivo.", PALE_GREEN, GREEN))
    story.append(PageBreak())

    story.extend(section("5", "Funcionamiento interno del SNIFFER y la 3B+"))
    story.extend(image_flow(diagrams["sniffer_host"], 6.25, "Ruta de captura, clasificación y publicación."))
    story.append(
        table_flow(
            ["Etapa", "Resultado"],
            [
                ["Captura PIO", "Dos receptores independientes observan FWD y REV."],
                ["Paridad estricta", "Una palabra inválida no actualiza accepted_words ni snapshot."],
                ["Filtro", "Whitelist de hasta 10 combinaciones Label/SDI."],
                ["Snapshot", "Hasta 16 variables recientes y contadores acumulados."],
                ["SPI PIO-frame", "Solicitud y respuesta en tramas completas de 32 bytes."],
                ["Bridge lock", "Serializa polling, filtros y reset; no intercala bytes SPI."],
            ],
            [1.55, 4.95],
        )
    )
    story.append(PageBreak())

    story.extend(section("6", "Red, direcciones IP y servicios"))
    story.extend(image_flow(diagrams["network"], 6.25, "Distribución de servicios entre la Raspberry Pi y la notebook."))
    story.append(
        table_flow(
            ["Elemento", "Dirección", "Uso"],
            [
                ["Notebook Ethernet", "192.168.50.1/24", "Dashboard y observabilidad."],
                ["Raspberry Pi eth0", "192.168.50.2/24", "Servidor visible en la red directa."],
                ["Bridge", "127.0.0.1:5100", "API SPI local; no expuesta."],
                ["Flask", "0.0.0.0:5000", "Dashboard en 192.168.50.2:5000."],
                ["Prometheus", "Notebook :9090", "Consulta 192.168.50.2:5000/metrics."],
                ["Grafana", "Notebook :3000", "Paneles e histórico."],
            ],
            [1.55, 1.7, 3.25],
        )
    )
    story.append(Spacer(1, 0.08 * inch))
    story.append(p("Flask consulta al bridge mediante HTTP sobre la pila TCP/IP local de la 3B+ y la dirección 127.0.0.1. La notebook solo accede a Flask.", "small"))
    story.append(PageBreak())

    story.extend(section("7", "Validación por osciloscopio: canal FWD"))
    story.extend(image_flow(FWD_SINGLE, 5.65, "GP2 respecto de masa: amplitud aproximada de 3,36 V y retorno a cero entre pulsos."))
    story.extend(image_flow(FWD_DIFF, 5.65, "MATH = CH1 - CH2: niveles aproximados de +3,36 V, 0 V y -3,44 V."))
    story.append(p("La traza individual confirma el nivel GPIO y la media celda activa. La resta de ambos hilos muestra la bipolaridad lógica; los colores corresponden a las dos polaridades de la señal diferencial calculada.", "small"))
    story.append(PageBreak())

    story.extend(section("8", "Validación por osciloscopio: canal REV histórico"))
    story.extend(image_flow(REV_SINGLE, 5.65, "GP4 en el modo batch/ACK: actividad agrupada y pulso activo cercano a 5 µs."))
    story.extend(image_flow(REV_DIFF, 5.65, "MATH del par REV: ráfaga con niveles positivos, nulos y negativos."))
    story.append(callout("Contexto de esta evidencia", "Estas capturas pertenecen al modo previo donde ARINC-RX emitía ACK. En el modo stream final REV no se utiliza para responder; el SNIFFER conserva sus entradas REV para observar un segundo canal simplex.", PALE_GOLD, GOLD))
    story.append(PageBreak())

    story.extend(section("9", "Resultados de validación"))
    story.extend(image_flow(diagrams["results"], 6.25, "Contadores relativos de la corrida final sin inyección de errores."))
    story.append(
        table_flow(
            ["Prueba", "Resultado", "Conclusión"],
            [
                ["Corrida final", "8,0004 h; 58.273.540 palabras; cero errores operativos.", "PASS"],
                ["Paridad estricta", "6.109 errores inyectados y descartados sobre 610.905 palabras.", "PASS"],
                ["Barrido SPI", "8 y 9 MHz PASS; 10 MHz parcial; 16 y 50 MHz FAIL.", "8 MHz"],
                ["Stream aleatorio", "Ráfagas variables, sin lotes fijos y sin ACK.", "Validado"],
            ],
            [1.35, 3.85, 1.3],
        )
    )
    story.append(Spacer(1, 0.08 * inch))
    story.append(p("La clasificación conservó exactamente el total: 17.482.062 aceptadas más 40.791.478 filtradas equivalen a 58.273.540 recibidas. La distribución 30 % / 70 % coincide con el perfil deliberado del transmisor.", "small"))
    story.append(PageBreak())

    story.extend(section("10", "Configuración recomendada y estado del desarrollo"))
    story.append(
        table_flow(
            ["Parámetro", "Valor recomendado"],
            [
                ["Target TX", "arinc_tx_arinc429_logic_stream"],
                ["Target RX", "arinc_rx_arinc429_logic_stream"],
                ["Target SNIFFER", "sniffer_arinc429_logic_pio_frame"],
                ["SPI", "PIO-frame, 8 MHz, CS manual por GPIO8"],
                ["Temporización CS", "setup 100 µs, hold 100 µs, respuesta 2 ms"],
                ["Polling / stats", "6 ms / 60 ms"],
                ["Red", "Ethernet directo 192.168.50.1 ↔ 192.168.50.2"],
            ],
            [2.0, 4.5],
        )
    )
    story.append(Spacer(1, 0.18 * inch))
    story.append(callout("Estado actual", "La fase lógica queda cerrada y reproducible: enlace FWD a 100 kbps, SNIFFER pasivo, paridad estricta, SPI por tramas completas a 8 MHz, exportación estadística y observabilidad.", PALE_GREEN, GREEN))
    story.append(Spacer(1, 0.12 * inch))
    story.append(callout("Límite vigente", "La implementación usa niveles GPIO de 3,3 V y masa común. Todavía no incorpora aislamiento, protección de línea ni receptor diferencial compatible con una instalación ARINC 429 real.", PALE_GOLD, RED))
    story.append(Spacer(1, 0.12 * inch))
    story.append(p("La siguiente fase debe comenzar por la especificación del frente eléctrico: conector, protección contra transitorios, entrada de alta impedancia, receptor ARINC dedicado, fuente de prueba y validación instrumental. La lógica de captura, filtrado, SPI y visualización desarrollada en esta fase puede reutilizarse.", "body"))
    story.append(Spacer(1, 0.22 * inch))
    story.append(p("Fuentes internas: PROJECT_CONTEXT.md, documentación de evidencias, diagramas de bloques y capturas almacenadas en imagenes/.", "intro"))

    doc.build(story, onFirstPage=footer, onLaterPages=footer)
    print(PDF_PATH)


if __name__ == "__main__":
    build_pdf()
