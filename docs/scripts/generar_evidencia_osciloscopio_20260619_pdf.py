from __future__ import annotations

from pathlib import Path

from reportlab.lib import colors
from reportlab.lib.enums import TA_CENTER, TA_LEFT
from reportlab.lib.pagesizes import letter
from reportlab.lib.styles import ParagraphStyle, getSampleStyleSheet
from reportlab.lib.units import inch
from reportlab.pdfbase import pdfmetrics
from reportlab.pdfbase.ttfonts import TTFont
from reportlab.platypus import (
    Image,
    PageBreak,
    Paragraph,
    SimpleDocTemplate,
    Spacer,
    Table,
    TableStyle,
)
from PIL import Image as PILImage


ROOT = Path(__file__).resolve().parents[2]
DOCS = ROOT / "docs"
IMAGE_DIR = ROOT / "imagenes" / "validacion_2026-06-19"
PDF_DIR = DOCS / "pdf"
PDF_PATH = PDF_DIR / "evidencia_osciloscopio_arinc429_20260619.pdf"

NAVY = colors.HexColor("#17324D")
BLUE = colors.HexColor("#2E74B5")
GREEN = colors.HexColor("#2F7D5A")
GOLD = colors.HexColor("#B7791F")
INK = colors.HexColor("#1E2933")
MUTED = colors.HexColor("#5D6975")
LIGHT = colors.HexColor("#F2F4F7")
PALE_BLUE = colors.HexColor("#E8EEF5")


def register_fonts() -> tuple[str, str]:
    regular = Path("C:/Windows/Fonts/calibri.ttf")
    bold = Path("C:/Windows/Fonts/calibrib.ttf")
    if regular.exists() and bold.exists():
        pdfmetrics.registerFont(TTFont("Calibri", str(regular)))
        pdfmetrics.registerFont(TTFont("Calibri-Bold", str(bold)))
        return "Calibri", "Calibri-Bold"
    return "Helvetica", "Helvetica-Bold"


BODY_FONT, BOLD_FONT = register_fonts()


def styles() -> dict[str, ParagraphStyle]:
    sample = getSampleStyleSheet()
    return {
        "title": ParagraphStyle(
            "Title",
            parent=sample["Title"],
            fontName=BOLD_FONT,
            fontSize=22,
            leading=26,
            textColor=NAVY,
            alignment=TA_LEFT,
            spaceAfter=8,
        ),
        "subtitle": ParagraphStyle(
            "Subtitle",
            parent=sample["BodyText"],
            fontName=BODY_FONT,
            fontSize=11.5,
            leading=14,
            textColor=MUTED,
            spaceAfter=12,
        ),
        "h1": ParagraphStyle(
            "H1",
            parent=sample["Heading1"],
            fontName=BOLD_FONT,
            fontSize=14,
            leading=17,
            textColor=BLUE,
            spaceBefore=5,
            spaceAfter=5,
        ),
        "body": ParagraphStyle(
            "Body",
            parent=sample["BodyText"],
            fontName=BODY_FONT,
            fontSize=9.3,
            leading=11.5,
            textColor=INK,
            spaceAfter=5,
        ),
        "small": ParagraphStyle(
            "Small",
            parent=sample["BodyText"],
            fontName=BODY_FONT,
            fontSize=8.2,
            leading=9.8,
            textColor=INK,
            spaceAfter=4,
        ),
        "caption": ParagraphStyle(
            "Caption",
            parent=sample["BodyText"],
            fontName=BODY_FONT,
            fontSize=8.0,
            leading=9.6,
            textColor=MUTED,
            alignment=TA_CENTER,
            spaceBefore=3,
            spaceAfter=8,
        ),
        "code": ParagraphStyle(
            "Code",
            parent=sample["Code"],
            fontName="Courier",
            fontSize=7.8,
            leading=9.2,
            textColor=INK,
            backColor=LIGHT,
            leftIndent=7,
            rightIndent=7,
            spaceBefore=3,
            spaceAfter=7,
        ),
    }


def p(text: str, style: ParagraphStyle) -> Paragraph:
    return Paragraph(text, style)


def code(text: str, style: ParagraphStyle) -> Paragraph:
    escaped = (
        text.replace("&", "&amp;")
        .replace("<", "&lt;")
        .replace(">", "&gt;")
        .replace("\n", "<br/>")
    )
    return Paragraph(escaped, style)


def scaled_image(path: Path, max_width: float, max_height: float) -> Image:
    with PILImage.open(path) as img:
        width_px, height_px = img.size
    ratio = min(max_width / width_px, max_height / height_px)
    return Image(str(path), width=width_px * ratio, height=height_px * ratio)


def image_section(story: list, st: dict[str, ParagraphStyle], filename: str, title: str, caption: str) -> None:
    story.append(p(title, st["h1"]))
    story.append(scaled_image(IMAGE_DIR / filename, max_width=7.2 * inch, max_height=4.35 * inch))
    story.append(p(caption, st["caption"]))


def build_pdf() -> None:
    PDF_DIR.mkdir(parents=True, exist_ok=True)
    st = styles()
    doc = SimpleDocTemplate(
        str(PDF_PATH),
        pagesize=letter,
        leftMargin=0.55 * inch,
        rightMargin=0.55 * inch,
        topMargin=0.5 * inch,
        bottomMargin=0.5 * inch,
        title="Evidencia de osciloscopio ARINC 429 logico",
        author="Proyecto PAMPA / ARINC 429",
    )

    story = [
        p("Evidencia de osciloscopio ARINC 429 logico", st["title"]),
        p("Capturas del 19 de junio de 2026. Validacion temporal con niveles GPIO de 3,3 V.", st["subtitle"]),
        p(
            "Estas capturas verifican la forma de onda bipolar con retorno a cero, "
            "la palabra de 32 bits, el gap entre palabras y la interpretacion de "
            "los canales individuales frente a la medicion diferencial MATH.",
            st["body"],
        ),
        p("Resumen tecnico", st["h1"]),
    ]

    table = Table(
        [
            ["Parametro", "Valor observado / implementado"],
            ["Bit rate", "100 kbps"],
            ["Periodo de bit", "10 us"],
            ["Mitad activa", "5 us"],
            ["Retorno a cero", "5 us"],
            ["Longitud de palabra", "32 bits = 320 us"],
            ["Gap minimo", "4 bits = 40 us de NULL"],
            ["Nivel logico diferencial", "+3,3 V / 0 V / -3,3 V aprox."],
        ],
        colWidths=[2.4 * inch, 4.8 * inch],
    )
    table.setStyle(
        TableStyle(
            [
                ("BACKGROUND", (0, 0), (-1, 0), PALE_BLUE),
                ("TEXTCOLOR", (0, 0), (-1, 0), NAVY),
                ("FONTNAME", (0, 0), (-1, 0), BOLD_FONT),
                ("FONTNAME", (0, 1), (0, -1), BOLD_FONT),
                ("FONTNAME", (1, 1), (1, -1), BODY_FONT),
                ("FONTSIZE", (0, 0), (-1, -1), 8.5),
                ("GRID", (0, 0), (-1, -1), 0.35, colors.HexColor("#CBD5DF")),
                ("VALIGN", (0, 0), (-1, -1), "MIDDLE"),
                ("LEFTPADDING", (0, 0), (-1, -1), 6),
                ("RIGHTPADDING", (0, 0), (-1, -1), 6),
            ]
        )
    )
    story.append(table)
    story.append(Spacer(1, 8))
    story.append(
        code(
            "Vdiff = CH1 - CH2  o  Vdiff = CH2 - CH1\n"
            "pulso positivo -> una polaridad logica\n"
            "pulso negativo -> polaridad opuesta\n"
            "cero           -> NULL / retorno a cero / gap",
            st["code"],
        )
    )

    story.append(PageBreak())
    story.append(p("Capturas con MATH", st["h1"]))
    story.append(
        p(
            "En estas imagenes la senal diferencial queda reconstruida por el osciloscopio. "
            "El signo depende de si MATH esta como CH1-CH2 o CH2-CH1, pero la presencia de "
            "niveles positivo, cero y negativo confirma la codificacion bipolar RZ logica.",
            st["body"],
        )
    )
    image_section(
        story,
        st,
        "TEK0007_MATH.JPG",
        "TEK0007 - MATH, vista amplia",
        "Varias palabras consecutivas. Cada grupo de pulsos corresponde a una palabra ARINC de 32 bits; los espacios horizontales son NULL.",
    )
    image_section(
        story,
        st,
        "TEK0008_MATH.JPG",
        "TEK0008 - MATH, 50 us/div",
        "La escala permite distinguir la estructura interna de palabra: pulso activo, retorno a cero y alternancia de polaridad.",
    )
    image_section(
        story,
        st,
        "TEK0009_MATH.JPG",
        "TEK0009 - MATH, gap visible",
        "Se observa separacion entre grupos. El firmware implementa un gap minimo de 40 us, y el modo stream puede sumar pausas mayores.",
    )
    image_section(
        story,
        st,
        "TEK0010_MATH.JPG",
        "TEK0010 - MATH, palabras sucesivas",
        "La secuencia coincide con palabras de 32 bits a 100 kbps y separacion por estado NULL.",
    )

    story.append(PageBreak())
    story.append(p("Capturas sin MATH", st["h1"]))
    story.append(
        p(
            "Sin MATH se ven las dos lineas medidas contra masa. No son dos datos independientes: "
            "son las dos mitades del mismo par diferencial. En un simbolo activo una linea sube "
            "y la otra queda baja; para la polaridad opuesta se invierte.",
            st["body"],
        )
    )
    image_section(
        story,
        st,
        "TEK0011_CANALES.JPG",
        "TEK0011 - canales individuales",
        "CH1 y CH2 no se pisan. La alternancia indica que el dato esta en la diferencia entre ambos conductores.",
    )
    image_section(
        story,
        st,
        "TEK0012_CANALES.JPG",
        "TEK0012 - canales, 50 us/div",
        "Se observan amplitudes individuales cercanas a 3,32 V, coherentes con el prototipo logico.",
    )
    image_section(
        story,
        st,
        "TEK0013_CANALES.JPG",
        "TEK0013 - canales complementarios",
        "La vista complementaria refuerza que A/B forman un par diferencial, no un canal de datos mas masa.",
    )

    story.append(PageBreak())
    story.append(p("Reconstruccion de palabra", st["h1"]))
    story.append(
        p(
            "Desde un JPG no es riguroso afirmar una palabra exacta. Para reconstruirla con precision "
            "hay que exportar muestras o colocar cursores cada 10 us desde el inicio de palabra. "
            "El metodo es:",
            st["body"],
        )
    )
    story.append(
        code(
            "1. Detectar inicio de palabra.\n"
            "2. Muestrear la primera mitad de cada celda de 10 us.\n"
            "3. Convertir polaridad positiva/negativa a 1/0 segun la convencion usada.\n"
            "4. Repetir hasta obtener 32 bits.\n"
            "5. Separar label, SDI, data, SSM y paridad.",
            st["code"],
        )
    )
    story.append(
        code(
            "bits 1..8    label\n"
            "bits 9..10   SDI\n"
            "bits 11..29  data\n"
            "bits 30..31  SSM\n"
            "bit 32       paridad impar",
            st["code"],
        )
    )
    story.append(
        p(
            "En el stream actual se transmiten labels utiles 0xA5/0, 0xB1/1 y 0xC2/2, "
            "mas labels de ruido 0x11, 0x24, 0x39, 0x4E, 0x57, 0x6A y 0x7D. "
            "Una palabra reconstruida desde CSV deberia caer en ese conjunto o ser descartada por filtro.",
            st["body"],
        )
    )
    story.append(p("Conclusion", st["h1"]))
    story.append(
        p(
            "La evidencia valida el comportamiento temporal y logico: 10 us por bit, 32 bits por palabra, "
            "retorno a cero, gap minimo de 40 us y niveles diferenciales logicos de aproximadamente +/-3,3 V. "
            "La interfaz aun no representa los niveles electricos reales de campo (+/-10 V diferencial); "
            "esa conversion queda para la etapa de front-end electrico.",
            st["body"],
        )
    )

    doc.build(story)


if __name__ == "__main__":
    build_pdf()
    print(PDF_PATH)
