from pathlib import Path
from textwrap import wrap

import matplotlib.pyplot as plt
from matplotlib.backends.backend_pdf import PdfPages


DOCS_DIR = Path(__file__).resolve().parents[1]
SOURCE_MD = DOCS_DIR / "preguntas_respuestas_tutor_arinc429.md"
OUTPUT_PDF = DOCS_DIR / "pdf" / "preguntas_respuestas_tutor_arinc429.pdf"

PAGE_W = 8.27
PAGE_H = 11.69
LEFT = 0.72
RIGHT = 0.72
TOP = 10.95
BOTTOM = 0.72
LINE_H = 0.19
TEXT_W = 102


def clean_markup(text: str) -> str:
    return text.replace("**", "").replace("`", "")


def add_page(pdf, page_no: int):
    fig = plt.figure(figsize=(PAGE_W, PAGE_H))
    ax = fig.add_axes([0, 0, 1, 1])
    ax.set_axis_off()
    fig.text(LEFT / PAGE_W, 0.965, "Proyecto PAMPA / ARINC 429", fontsize=8.5, color="#5a6772")
    fig.text(0.90, 0.035, str(page_no), fontsize=8.5, color="#5a6772")
    return fig


def emit_line(fig, y, text, size=9.3, weight="normal", color="#1f2933", indent=0.0):
    fig.text(
        (LEFT + indent) / PAGE_W,
        y / PAGE_H,
        text,
        fontsize=size,
        fontweight=weight,
        color=color,
        va="top",
        family="DejaVu Sans",
    )


def render_pdf():
    lines = SOURCE_MD.read_text(encoding="utf-8").splitlines()
    OUTPUT_PDF.parent.mkdir(exist_ok=True)

    with PdfPages(OUTPUT_PDF) as pdf:
        page_no = 1
        fig = add_page(pdf, page_no)
        y = TOP

        for raw in lines:
            line = clean_markup(raw.strip())

            if not line:
                y -= LINE_H * 0.55
                continue

            if line.startswith("# "):
                y -= 0.20
                emit_line(fig, y, line[2:], size=18, weight="bold", color="#123a5a")
                y -= 0.48
                continue

            if line.startswith("## "):
                y -= 0.20
                if y < BOTTOM + 0.95:
                    pdf.savefig(fig)
                    plt.close(fig)
                    page_no += 1
                    fig = add_page(pdf, page_no)
                    y = TOP
                wrapped = wrap(line[3:], width=78)
                for part in wrapped:
                    emit_line(fig, y, part, size=11.5, weight="bold", color="#174a6f")
                    y -= LINE_H * 1.25
                y -= LINE_H * 0.35
                continue

            bullet = line.startswith("- ")
            text = line[2:] if bullet else line
            wrapped = wrap(text, width=TEXT_W - (6 if bullet else 0))
            for idx, part in enumerate(wrapped):
                if y < BOTTOM:
                    pdf.savefig(fig)
                    plt.close(fig)
                    page_no += 1
                    fig = add_page(pdf, page_no)
                    y = TOP
                prefix = "- " if bullet and idx == 0 else "  " if bullet else ""
                emit_line(fig, y, prefix + part, indent=0.16 if bullet else 0.0)
                y -= LINE_H
            y -= LINE_H * 0.25

        pdf.savefig(fig)
        plt.close(fig)

    print(f"PDF generado: {OUTPUT_PDF}")


if __name__ == "__main__":
    render_pdf()
