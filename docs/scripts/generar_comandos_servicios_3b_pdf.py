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
    canvas.drawString(doc.leftMargin, letter[1] - 0.43 * inch, "PAMPA / ARINC 429  |  Comandos de arranque y rescate")
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
        title="Comandos de arranque y rescate ARINC",
        author="Proyecto PAMPA",
    )

    def command_section(title: str, command_text: str) -> KeepTogether:
        return KeepTogether([p(title, "h1"), code(command_text), Spacer(1, 0.04 * inch)])

    story: list = [
        p("Comandos de arranque y rescate ARINC", "title"),
        p("Referencia corta para notebook, Prometheus/Grafana, Raspberry Pi 3B+, bridge SPI y dashboard Flask.", "subtitle"),
        callout(
            "Idea clave",
            "En operacion normal la 3B+ arranca sola con systemd. En la notebook, Prometheus/Grafana se inician con el script PowerShell del repo.",
        ),
        Spacer(1, 0.10 * inch),
        command_section(
            "1. Notebook - iniciar Prometheus/Grafana",
            r"""
cd "C:\Users\usuario\RASPI PICO\PAMPA\ARINC_SPI\prometheus"
powershell -ExecutionPolicy Bypass -File .\start_prometheus_grafana.ps1
""",
        ),
        command_section(
            "2. Notebook - verificar estado",
            r"""
cd "C:\Users\usuario\RASPI PICO\PAMPA\ARINC_SPI\prometheus"
powershell -ExecutionPolicy Bypass -File .\start_prometheus_grafana.ps1 -Status
""",
        ),
        command_section(
            "3. Notebook - detener Prometheus/Grafana",
            r"""
cd "C:\Users\usuario\RASPI PICO\PAMPA\ARINC_SPI\prometheus"
powershell -ExecutionPolicy Bypass -File .\start_prometheus_grafana.ps1 -Stop
""",
        ),
        callout(
            "URLs notebook",
            "Prometheus: http://127.0.0.1:9090 | Targets: http://127.0.0.1:9090/targets | Grafana: http://127.0.0.1:3000",
            PALE_GOLD,
            GOLD,
        ),
        Spacer(1, 0.10 * inch),
        command_section(
            "4. 3B+ - verificar que el autoarranque funciono",
            """
systemctl status arinc-sniffer-bridge arinc-dashboard-flask --no-pager
curl http://127.0.0.1:5100/stats
curl http://127.0.0.1:5000/stats
""",
        ),
        command_section(
            "5. 3B+ - rescate rapido si Flask o bridge no arrancaron",
            """
sudo systemctl restart arinc-sniffer-bridge arinc-dashboard-flask
sleep 2
systemctl status arinc-sniffer-bridge arinc-dashboard-flask --no-pager
""",
        ),
        command_section(
            "6. 3B+ - verificar perfil real del bridge",
            """
PID="$(systemctl show -p MainPID --value arinc-sniffer-bridge)"
tr '\\0' '\\n' < "/proc/$PID/environ" | grep '^ARINC_SPI_'
""",
        ),
        command_section(
            "7. 3B+ - valores esperados del perfil",
            """
ARINC_SPI_HZ=8000000
ARINC_SPI_TRANSFER_MODE=pio-frame
ARINC_SPI_MANUAL_CS=1
ARINC_SPI_REQUEST_MODE=drdy
ARINC_SPI_DRDY_GPIO=25
""",
        ),
        command_section(
            "8. 3B+ - configurar IP fija Ethernet",
            """
sudo bash ~/PAMPA/HOST_3b+/configurar_eth0_ip_fija.sh
ip -4 addr show eth0
""",
        ),
        command_section(
            "9. 3B+ - reinstalar servicios una sola vez",
            """
cd ~/PAMPA/HOST_3b+

sudo cp systemd/arinc-sniffer-bridge.service /etc/systemd/system/
sudo cp systemd/arinc-dashboard-flask.service /etc/systemd/system/

sudo systemctl daemon-reload
""",
        ),
        command_section(
            "10. 3B+ - habilitar autoarranque",
            """
sudo systemctl enable arinc-sniffer-bridge arinc-dashboard-flask
sudo systemctl start arinc-sniffer-bridge arinc-dashboard-flask
""",
        ),
        command_section(
            "11. 3B+ - detenerlos ahora",
            """
sudo systemctl stop arinc-dashboard-flask arinc-sniffer-bridge
""",
        ),
        command_section(
            "12. 3B+ - evitar autoarranque en el futuro",
            """
sudo systemctl disable arinc-sniffer-bridge arinc-dashboard-flask
""",
        ),
        command_section(
            "13. 3B+ - volver a habilitar autoarranque",
            """
sudo systemctl enable arinc-sniffer-bridge arinc-dashboard-flask
""",
        ),
        command_section(
            "14. 3B+ - ver logs en vivo",
            """
journalctl -u arinc-sniffer-bridge -f
journalctl -u arinc-dashboard-flask -f
""",
        ),
        command_section(
            "15. 3B+ - matar procesos manuales si quedaron duplicados",
            """
pkill -f '[s]pi_sniffer_bridge.py'
pkill -f '[a]rinc_dashboard/.*/app.py'
sudo systemctl restart arinc-sniffer-bridge arinc-dashboard-flask
""",
        ),
        callout(
            "Acceso operativo",
            "Con Ethernet directa: notebook 192.168.50.1/24, 3B+ 192.168.50.2/24. Dashboard: http://192.168.50.2:5000.",
            PALE_GOLD,
            GOLD,
        ),
    ]

    doc.build(story, onFirstPage=footer, onLaterPages=footer)
    print(PDF_PATH)


if __name__ == "__main__":
    build_pdf()
