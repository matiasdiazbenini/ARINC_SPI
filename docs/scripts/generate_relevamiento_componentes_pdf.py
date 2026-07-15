from pathlib import Path
import re
import shutil

from reportlab.lib import colors
from reportlab.lib.enums import TA_CENTER, TA_LEFT
from reportlab.lib.pagesizes import A4
from reportlab.lib.styles import ParagraphStyle, getSampleStyleSheet
from reportlab.lib.units import mm
from reportlab.platypus import (
    SimpleDocTemplate,
    Paragraph,
    Spacer,
    Table,
    TableStyle,
    PageBreak,
)


ROOT = Path(__file__).resolve().parents[2]
SOURCE = ROOT / "ELECTRICO" / "frontend_arinc429_lab" / "relevamiento_componentes_frontend_electrico.md"
DOCS_PDF = ROOT / "docs" / "pdf" / "relevamiento_componentes_frontend_electrico.pdf"
OUTPUT_PDF = ROOT / "output" / "pdf" / "relevamiento_componentes_frontend_electrico.pdf"


def esc(text: str) -> str:
    return (
        text.replace("&", "&amp;")
        .replace("<", "&lt;")
        .replace(">", "&gt;")
    )


def linkify(text: str) -> str:
    text = esc(text.strip())
    url_re = re.compile(r"(https?://[^\s]+)")

    def repl(match):
        url = match.group(1).rstrip(".,)")
        suffix = match.group(1)[len(url):]
        label = "abrir link" if len(url) > 48 else url
        return f'<link href="{url}" color="blue">{label}</link>{suffix}'

    return url_re.sub(repl, text)


def para(text: str, style):
    return Paragraph(linkify(text), style)


def table_from_lines(lines, styles):
    rows = []
    for raw in lines:
        stripped = raw.strip()
        if re.fullmatch(r"\|[\s:\-\|]+\|", stripped):
            continue
        cells = [c.strip() for c in stripped.strip("|").split("|")]
        rows.append(cells)

    if not rows:
        return []

    max_cols = max(len(r) for r in rows)
    for row in rows:
        row.extend([""] * (max_cols - len(row)))

    if max_cols == 5:
        widths = [43 * mm, 31 * mm, 26 * mm, 46 * mm, 42 * mm]
    elif max_cols == 4:
        widths = [48 * mm, 35 * mm, 32 * mm, 74 * mm]
    elif max_cols == 3:
        widths = [55 * mm, 78 * mm, 55 * mm]
    else:
        usable = 188 * mm
        widths = [usable / max_cols] * max_cols

    data = []
    for ri, row in enumerate(rows):
        style = styles["table_header"] if ri == 0 else styles["table_cell"]
        data.append([Paragraph(linkify(cell), style) for cell in row])

    tbl = Table(data, colWidths=widths, repeatRows=1, hAlign="LEFT")
    tbl.setStyle(
        TableStyle(
            [
                ("BACKGROUND", (0, 0), (-1, 0), colors.HexColor("#1F4E79")),
                ("TEXTCOLOR", (0, 0), (-1, 0), colors.white),
                ("GRID", (0, 0), (-1, -1), 0.35, colors.HexColor("#B8C6D1")),
                ("VALIGN", (0, 0), (-1, -1), "TOP"),
                ("LEFTPADDING", (0, 0), (-1, -1), 3),
                ("RIGHTPADDING", (0, 0), (-1, -1), 3),
                ("TOPPADDING", (0, 0), (-1, -1), 3),
                ("BOTTOMPADDING", (0, 0), (-1, -1), 3),
                ("ROWBACKGROUNDS", (0, 1), (-1, -1), [colors.white, colors.HexColor("#F5F8FA")]),
            ]
        )
    )
    return [Spacer(1, 2 * mm), tbl, Spacer(1, 4 * mm)]


def build_story(markdown: str):
    base = getSampleStyleSheet()
    styles = {
        "h1": ParagraphStyle(
            "h1",
            parent=base["Title"],
            fontName="Helvetica-Bold",
            fontSize=18,
            leading=22,
            alignment=TA_CENTER,
            textColor=colors.HexColor("#12344D"),
            spaceAfter=6 * mm,
        ),
        "h2": ParagraphStyle(
            "h2",
            parent=base["Heading2"],
            fontName="Helvetica-Bold",
            fontSize=12.5,
            leading=15,
            textColor=colors.HexColor("#1F4E79"),
            spaceBefore=4 * mm,
            spaceAfter=2 * mm,
        ),
        "h3": ParagraphStyle(
            "h3",
            parent=base["Heading3"],
            fontName="Helvetica-Bold",
            fontSize=10.5,
            leading=13,
            textColor=colors.HexColor("#6B3F00"),
            spaceBefore=3 * mm,
            spaceAfter=1.5 * mm,
        ),
        "body": ParagraphStyle(
            "body",
            parent=base["BodyText"],
            fontName="Helvetica",
            fontSize=9,
            leading=12,
            alignment=TA_LEFT,
            spaceAfter=1.6 * mm,
        ),
        "bullet": ParagraphStyle(
            "bullet",
            parent=base["BodyText"],
            fontName="Helvetica",
            fontSize=8.8,
            leading=11.4,
            leftIndent=5 * mm,
            firstLineIndent=-3 * mm,
            spaceAfter=1 * mm,
        ),
        "table_cell": ParagraphStyle(
            "table_cell",
            parent=base["BodyText"],
            fontName="Helvetica",
            fontSize=6.7,
            leading=8.2,
        ),
        "table_header": ParagraphStyle(
            "table_header",
            parent=base["BodyText"],
            fontName="Helvetica-Bold",
            fontSize=6.9,
            leading=8.2,
            textColor=colors.white,
        ),
        "small": ParagraphStyle(
            "small",
            parent=base["BodyText"],
            fontName="Helvetica",
            fontSize=7.5,
            leading=9.3,
            textColor=colors.HexColor("#455A64"),
        ),
    }

    story = []
    lines = markdown.splitlines()
    table_lines = []

    def flush_table():
        nonlocal table_lines
        if table_lines:
            story.extend(table_from_lines(table_lines, styles))
            table_lines = []

    for raw in lines:
        line = raw.rstrip()
        if line.strip().startswith("|"):
            table_lines.append(line)
            continue

        flush_table()

        if not line.strip():
            story.append(Spacer(1, 1.2 * mm))
            continue
        if line.startswith("# "):
            story.append(para(line[2:].strip(), styles["h1"]))
            story.append(Paragraph("Relevamiento de precios, disponibilidad y contactos publicados", styles["small"]))
            story.append(Spacer(1, 3 * mm))
        elif line.startswith("## "):
            heading = line[3:].strip()
            if heading in {
                "Componentes con precio local verificado",
                "Componentes no encontrados con precio local verificable",
                "Pendientes reales de compra o verificacion",
            }:
                story.append(PageBreak())
            story.append(para(heading, styles["h2"]))
        elif line.startswith("### "):
            story.append(para(line[4:].strip(), styles["h3"]))
        elif line.startswith("- "):
            story.append(Paragraph("- " + linkify(line[2:].strip()), styles["bullet"]))
        else:
            story.append(para(line, styles["body"]))

    flush_table()
    return story


def footer(canvas, doc):
    canvas.saveState()
    canvas.setStrokeColor(colors.HexColor("#B0BEC5"))
    canvas.line(doc.leftMargin, 12 * mm, A4[0] - doc.rightMargin, 12 * mm)
    canvas.setFont("Helvetica", 7)
    canvas.setFillColor(colors.HexColor("#607D8B"))
    canvas.drawString(doc.leftMargin, 7.5 * mm, "PAMPA / ARINC 429 - relevamiento de componentes")
    canvas.drawRightString(A4[0] - doc.rightMargin, 7.5 * mm, f"Pagina {doc.page}")
    canvas.restoreState()


def main():
    DOCS_PDF.parent.mkdir(parents=True, exist_ok=True)
    OUTPUT_PDF.parent.mkdir(parents=True, exist_ok=True)

    doc = SimpleDocTemplate(
        str(DOCS_PDF),
        pagesize=A4,
        rightMargin=10 * mm,
        leftMargin=10 * mm,
        topMargin=12 * mm,
        bottomMargin=18 * mm,
        title="Relevamiento de componentes - front-end electrico ARINC 429",
        author="Proyecto PAMPA",
    )
    story = build_story(SOURCE.read_text(encoding="utf-8"))
    doc.build(story, onFirstPage=footer, onLaterPages=footer)
    shutil.copyfile(DOCS_PDF, OUTPUT_PDF)
    print(DOCS_PDF)
    print(OUTPUT_PDF)


if __name__ == "__main__":
    main()
