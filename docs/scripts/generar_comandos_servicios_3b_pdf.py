from __future__ import annotations

from pathlib import Path
from xml.sax.saxutils import escape

from reportlab.lib import colors
from reportlab.lib.enums import TA_LEFT
from reportlab.lib.pagesizes import letter
from reportlab.lib.styles import ParagraphStyle, getSampleStyleSheet
from reportlab.lib.units import inch
from reportlab.pdfbase import pdfmetrics
from reportlab.pdfbase.ttfonts import TTFont
from reportlab.platypus import (
    KeepTogether,
    Paragraph,
    Preformatted,
    SimpleDocTemplate,
    Spacer,
    Table,
    TableStyle,
)


ROOT = Path(__file__).resolve().parents[2]
PDF_DIR = ROOT / "docs" / "pdf"
PDF_PATH = PDF_DIR / "comandos_servicios_3b_autoarranque.pdf"

NAVY = colors.HexColor("#17324D")
BLUE = colors.HexColor("#2E74B5")
GREEN = colors.HexColor("#2F7D5A")
GOLD = colors.HexColor("#B7791F")
INK = colors.HexColor("#1E2933")
MUTED = colors.HexColor("#5D6975")
LIGHT = colors.HexColor("#F2F4F7")
PALE_GREEN = colors.HexColor("#E8F3ED")
PALE_GOLD = colors.HexColor("#FFF4DC")


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
        fontSize=23,
        leading=27,
        alignment=TA_LEFT,
        textColor=NAVY,
        spaceAfter=8,
    ),
    "subtitle": ParagraphStyle(
        "Subtitle",
        parent=sample["BodyText"],
        fontName=BODY_FONT,
        fontSize=11.4,
        leading=14,
        textColor=MUTED,
        spaceAfter=12,
    ),
    "h1": ParagraphStyle(
        "H1",
        parent=sample["Heading1"],
        fontName=BOLD_FONT,
        fontSize=13.4,
        leading=16,
        textColor=BLUE,
        spaceBefore=6,
        spaceAfter=5,
    ),
    "body": ParagraphStyle(
        "Body",
        parent=sample["BodyText"],
        fontName=BODY_FONT,
        fontSize=9.6,
        leading=11.6,
        textColor=INK,
        spaceAfter=5,
    ),
    "code": ParagraphStyle(
        "Code",
        parent=sample["Code"],
        fontName=MONO_FONT,
        fontSize=8.8,
        leading=10.4,
        textColor=INK,
        leftIndent=0,
        spaceAfter=0,
    ),
    "table_header": ParagraphStyle(
        "TableHeader",
        parent=sample["BodyText"],
        fontName=BOLD_FONT,
        fontSize=8.2,
        leading=9.5,
        textColor=NAVY,
        spaceAfter=0,
    ),
    "table_body": ParagraphStyle(
        "TableBody",
        parent=sample["BodyText"],
        fontName=BODY_FONT,
        fontSize=8.0,
        leading=9.4,
        textColor=INK,
        spaceAfter=0,
    ),
}


def p(text: str, style: str = "body") -> Paragraph:
    return Paragraph(escape(text).replace("\n", "<br/>"), ST[style])


def code(text: str) -> Table:
    block = Preformatted(text.strip(), ST["code"])
    table = Table([[block]], colWidths=[6.5 * inch], hAlign="CENTER")
    table.setStyle(
        TableStyle(
            [
                ("BACKGROUND", (0, 0), (-1, -1), LIGHT),
                ("BOX", (0, 0), (-1, -1), 0.6, colors.HexColor("#BAC4CE")),
                ("LEFTPADDING", (0, 0), (-1, -1), 8),
                ("RIGHTPADDING", (0, 0), (-1, -1), 8),
                ("TOPPADDING", (0, 0), (-1, -1), 7),
                ("BOTTOMPADDING", (0, 0), (-1, -1), 7),
            ]
        )
    )
    return table


def callout(title: str, text: str, fill=PALE_GREEN, accent=GREEN) -> Table:
    table = Table(
        [[p(title, "table_header")], [p(text, "table_body")]],
        colWidths=[6.5 * inch],
        hAlign="CENTER",
    )
    table.setStyle(
        TableStyle(
            [
                ("BACKGROUND", (0, 0), (-1, -1), fill),
                ("BOX", (0, 0), (-1, -1), 0.7, accent),
                ("LEFTPADDING", (0, 0), (-1, -1), 8),
                ("RIGHTPADDING", (0, 0), (-1, -1), 8),
                ("TOPPADDING", (0, 0), (-1, -1), 6),
                ("BOTTOMPADDING", (0, 0), (-1, -1), 6),
            ]
        )
    )
    return table


def footer(canvas, doc) -> None:
    canvas.saveState()
    canvas.setStrokeColor(colors.HexColor("#D7DEE6"))
    canvas.setLineWidth(0.5)
    canvas.line(doc.leftMargin, letter[1] - 0.52 * inch, letter[0] - doc.rightMargin, letter[1] - 0.52 * inch)
    canvas.setFillColor(MUTED)
    canvas.setFont(BOLD_FONT, 7.5)
    canvas.drawString(doc.leftMargin, letter[1] - 0.43 * inch, "PAMPA / ARINC 429  |  Servicios en Raspberry Pi 3B+")
    canvas.setFont(BODY_FONT, 8)
    canvas.drawRightString(letter[0] - doc.rightMargin, 0.42 * inch, f"Pagina {doc.page}")
    canvas.restoreState()


def build_pdf() -> None:
    PDF_DIR.mkdir(parents=True, exist_ok=True)
    doc = SimpleDocTemplate(
        str(PDF_PATH),
        pagesize=letter,
        rightMargin=1.0 * inch,
        leftMargin=1.0 * inch,
        topMargin=0.7 * inch,
        bottomMargin=0.65 * inch,
        title="Comandos servicios Raspberry Pi 3B+",
        author="Proyecto PAMPA",
    )

    def command_section(title: str, command_text: str) -> KeepTogether:
        return KeepTogether([p(title, "h1"), code(command_text), Spacer(1, 0.04 * inch)])

    story: list = [
        p("Comandos para servicios ARINC en Raspberry Pi 3B+", "title"),
        p("Referencia corta para instalar, habilitar, detener y diagnosticar el bridge SPI y el dashboard Flask.", "subtitle"),
        callout(
            "Idea clave",
            "Con systemd los procesos corren en segundo plano. No bloquean la terminal y no hace falta usar nohup.",
        ),
        Spacer(1, 0.10 * inch),
        command_section(
            "1. Instalar servicios una sola vez",
            """
cd ~/PAMPA/HOST_3b+

sudo cp systemd/arinc-sniffer-bridge.service /etc/systemd/system/
sudo cp systemd/arinc-dashboard-flask.service /etc/systemd/system/

sudo systemctl daemon-reload
""",
        ),
        command_section(
            "2. Habilitar autoarranque",
            """
sudo systemctl enable arinc-sniffer-bridge arinc-dashboard-flask
sudo systemctl start arinc-sniffer-bridge arinc-dashboard-flask
""",
        ),
        command_section(
            "3. Ver estado",
            """
systemctl status arinc-sniffer-bridge arinc-dashboard-flask --no-pager
""",
        ),
        command_section(
            "4. Reiniciar ambos servicios manualmente",
            """
sudo systemctl restart arinc-sniffer-bridge arinc-dashboard-flask
""",
        ),
        command_section(
            "5. Detenerlos ahora",
            """
sudo systemctl stop arinc-dashboard-flask arinc-sniffer-bridge
""",
        ),
        command_section(
            "6. Evitar autoarranque en el futuro",
            """
sudo systemctl disable arinc-sniffer-bridge arinc-dashboard-flask
""",
        ),
        command_section(
            "7. Volver a habilitar autoarranque",
            """
sudo systemctl enable arinc-sniffer-bridge arinc-dashboard-flask
""",
        ),
        command_section(
            "8. Ver logs en vivo",
            """
journalctl -u arinc-sniffer-bridge -f
journalctl -u arinc-dashboard-flask -f
""",
        ),
        command_section(
            "9. Verificacion rapida",
            """
curl http://127.0.0.1:5100/stats
curl http://127.0.0.1:5000/stats
""",
        ),
        callout(
            "Acceso desde la notebook",
            "Con la red Ethernet directa configurada, abrir http://192.168.50.2:5000.",
            PALE_GOLD,
            GOLD,
        ),
    ]

    doc.build(story, onFirstPage=footer, onLaterPages=footer)
    print(PDF_PATH)


if __name__ == "__main__":
    build_pdf()
