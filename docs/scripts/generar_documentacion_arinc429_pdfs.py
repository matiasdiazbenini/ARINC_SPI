from pathlib import Path
from textwrap import fill

import matplotlib.pyplot as plt
from matplotlib.backends.backend_pdf import PdfPages
from matplotlib.patches import FancyArrowPatch, Rectangle
import numpy as np

from generar_evidencia_osciloscopio_20260619_pdf import (
    PDF_PATH as SCOPE_EVIDENCE_PDF,
    build_pdf as build_scope_evidence_pdf,
)
from generar_comandos_servicios_3b_pdf import (
    PDF_PATH as COMMANDS_PDF,
    build_pdf as build_commands_pdf,
)


DOCS_DIR = Path(__file__).resolve().parents[1]
PDF_DIR = DOCS_DIR / "pdf"
SUMMARY_PDF = PDF_DIR / "resumen_proyecto_arinc429.pdf"
SCOPE_PDF = PDF_DIR / "instructivo_osciloscopio_arinc429_logic.pdf"


def wrap(text: str, width: int) -> str:
    return fill(text, width=width)


def add_box(ax, x, y, w, h, title, body, fc="#f7fbff", ec="#49657a"):
    rect = Rectangle((x, y), w, h, facecolor=fc, edgecolor=ec, linewidth=1.4)
    ax.add_patch(rect)
    ax.text(x + 0.015, y + h - 0.03, title, fontsize=12, fontweight="bold", va="top")
    ax.text(x + 0.015, y + h - 0.08, body, fontsize=10, va="top", linespacing=1.35)


def add_arrow(ax, start, end, color="#355c7d"):
    ax.add_patch(FancyArrowPatch(start, end, arrowstyle="-|>", mutation_scale=16, linewidth=1.6, color=color))


def summary_page_1(pdf):
    fig = plt.figure(figsize=(8.27, 11.69))
    ax = fig.add_axes([0, 0, 1, 1])
    ax.set_axis_off()

    fig.text(0.06, 0.955, "Resumen del proyecto ARINC 429", fontsize=22, fontweight="bold")
    fig.text(0.06, 0.925, "Arquitectura, fases y estado actual", fontsize=11, color="#4a6572")

    intro = (
        "Objetivo del proyecto: interceptar una comunicacion tipo ARINC 429 con una Pico sniffer, "
        "filtrar en hardware solo la informacion relevante y entregarla a una Raspberry Pi 3B+ "
        "para visualizacion y monitoreo."
    )
    fig.text(0.06, 0.87, wrap(intro, 90), fontsize=10.5, va="top", linespacing=1.4)

    add_box(ax, 0.07, 0.56, 0.20, 0.16, "Pico master", "Genera tramas\nTransmite por FWD", fc="#eef7ff")
    add_box(ax, 0.40, 0.56, 0.20, 0.16, "Pico sniffer", "Escucha FWD y REV\nFiltra labels\nExporta por SPI", fc="#f2fff2")
    add_box(ax, 0.73, 0.56, 0.20, 0.16, "Pico slave", "Recibe FWD\nProcesa lotes\nResponde ACK por REV", fc="#fff7ee")
    add_box(ax, 0.40, 0.29, 0.24, 0.14, "Raspberry Pi 3B+", "Bridge SPI\nFlask\nDashboard web", fc="#fff5fb")

    add_arrow(ax, (0.27, 0.64), (0.40, 0.64))
    add_arrow(ax, (0.73, 0.64), (0.60, 0.64))
    add_arrow(ax, (0.50, 0.56), (0.50, 0.43))
    fig.text(0.32, 0.655, "FWD", fontsize=10, fontweight="bold", color="#355c7d")
    fig.text(0.61, 0.655, "REV", fontsize=10, fontweight="bold", color="#355c7d")
    fig.text(0.515, 0.48, "SPI", fontsize=10, fontweight="bold", color="#355c7d")

    bullets = (
        "- La 3B+ consume solo estado filtrado.\n"
        "- El dashboard muestra variables, ACK y salud del sistema.\n"
        "- La logica de filtrado principal vive en la Pico sniffer.\n"
        "- La fase actual ya conserva esta arquitectura completa."
    )
    fig.text(0.06, 0.18, bullets, fontsize=10.5, va="top", linespacing=1.5)

    pdf.savefig(fig)
    plt.close(fig)


def summary_page_2(pdf):
    fig = plt.figure(figsize=(8.27, 11.69))
    ax = fig.add_axes([0.05, 0.52, 0.90, 0.37])
    ax.set_axis_off()

    fig.text(0.06, 0.94, "Fases y hitos", fontsize=18, fontweight="bold")

    rows = [
        ["Fase 1", "Emulacion funcional", "Arquitectura completa validada y SPI byte a byte estabilizado"],
        ["Fase 1B", "Bridge y dashboard", "Snapshots robustos, ajustes de refresco y perfil recomendado"],
        ["Fase 2", "ARINC429 logic", "Recreacion logica/temporal validada sin cambiar la 3B+"],
    ]

    table = ax.table(
        cellText=rows,
        colLabels=["Fase", "Enfoque", "Resultado principal"],
        loc="upper left",
        cellLoc="left",
        colLoc="left",
        colWidths=[0.14, 0.26, 0.60],
    )
    table.auto_set_font_size(False)
    table.set_fontsize(10)
    table.scale(1, 1.7)
    for (row, col), cell in table.get_celld().items():
        cell.set_edgecolor("#7a8a99")
        if row == 0:
            cell.set_facecolor("#d9edf7")
            cell.set_text_props(fontweight="bold")
        else:
            cell.set_facecolor("#fbfdff" if row % 2 else "#f3f7fb")

    achievements = (
        "Hitos logrados:\n"
        "- Transporte SPI robusto entre sniffer y 3B+\n"
        "- Dashboard estable en corridas largas\n"
        "- Perfil operativo rapido y sano para uso continuo\n"
        "- Modo `arinc429_logic` validado por varias horas de prueba"
    )
    fig.text(0.06, 0.44, achievements, fontsize=10.5, va="top", linespacing=1.5)

    notes = (
        "Lectura practica: la fase actual ya no es una prueba corta. El sistema completo quedo "
        "validado tanto en integracion como en estabilidad, primero en emulacion funcional y luego "
        "en la recreacion logica/temporal mas fiel de ARINC 429."
    )
    fig.text(0.06, 0.24, wrap(notes, 92), fontsize=10.5, va="top", linespacing=1.45)

    pdf.savefig(fig)
    plt.close(fig)


def summary_page_3(pdf):
    fig = plt.figure(figsize=(8.27, 11.69))
    ax = fig.add_axes([0.05, 0.53, 0.90, 0.30])
    ax.set_axis_off()

    fig.text(0.06, 0.94, "Perfiles del bridge y dashboard", fontsize=18, fontweight="bold")

    rows = [
        ["SPI bridge", "800 kHz", "1.2 MHz"],
        ["Delay por byte", "25 us", "0 us"],
        ["Poll bridge", "6 ms", "5 ms"],
        ["Stats bridge", "60 ms", "50 ms"],
        ["Refresh dashboard", "33 ms", "25 ms"],
        ["Comportamiento", "Estable en corridas largas", "Soporta muchas horas, pero con muchos mas spi_errors"],
    ]

    table = ax.table(
        cellText=rows,
        colLabels=["Parametro", "Perfil recomendado", "Perfil de estres"],
        loc="upper left",
        cellLoc="left",
        colLoc="left",
        colWidths=[0.28, 0.28, 0.44],
    )
    table.auto_set_font_size(False)
    table.set_fontsize(9.8)
    table.scale(1, 1.8)
    for (row, col), cell in table.get_celld().items():
        cell.set_edgecolor("#7a8a99")
        if row == 0:
            cell.set_facecolor("#d9edf7")
            cell.set_text_props(fontweight="bold")
        else:
            cell.set_facecolor("#fbfdff" if row % 2 else "#f3f7fb")

    conclusion = (
        "Conclusion operativa: el sistema admite un perfil de estres muy agresivo y aun asi se recupera, "
        "pero el perfil recomendado ofrece una relacion mucho mejor entre fluidez visual y tasa de errores SPI. "
        "Por eso queda elegido como configuracion continua."
    )
    fig.text(0.06, 0.31, wrap(conclusion, 92), fontsize=10.5, va="top", linespacing=1.45)

    pdf.savefig(fig)
    plt.close(fig)


def summary_page_4(pdf):
    fig = plt.figure(figsize=(8.27, 11.69))
    ax = fig.add_axes([0, 0, 1, 1])
    ax.set_axis_off()

    fig.text(0.06, 0.94, "Cierre de etapa y siguiente paso", fontsize=18, fontweight="bold")

    blocks = [
        (
            "Lo que ya quedo cerrado",
            "- Emulacion funcional completa\n"
            "- Bridge SPI y dashboard estables\n"
            "- Modo `arinc429_logic` validado\n"
            "- Perfil continuo recomendado definido",
        ),
        (
            "Lo que no cambia",
            "- Labels\n"
            "- Filtro\n"
            "- Semantica del dashboard\n"
            "- Papel de la 3B+",
        ),
        (
            "Siguiente fase natural",
            "- Evidencia con osciloscopio\n"
            "- Documentacion final\n"
            "- Mas adelante, capa electrica ARINC 429 real con transceptores",
        ),
    ]

    y = 0.76
    colors = ["#eef7ff", "#f3fdf4", "#fff7ee"]
    for idx, (title, body) in enumerate(blocks):
        add_box(ax, 0.08, y, 0.84, 0.16, title, body, fc=colors[idx])
        y -= 0.21

    final = (
        "Veredicto general: el proyecto ya tiene una base madura y demostrable. "
        "La siguiente etapa ya no es estabilizar la arquitectura digital, sino documentarla "
        "bien y preparar la futura transicion a la capa electrica ARINC 429 real."
    )
    fig.text(0.06, 0.10, wrap(final, 94), fontsize=11, fontweight="bold", va="bottom")

    pdf.savefig(fig)
    plt.close(fig)


def scope_waveform(bit_values):
    t = [0.0]
    a = [0.0]
    b = [0.0]
    time = 0.0
    half = 0.5
    for bit in bit_values:
        if bit == 1:
            first_a, first_b = 1.0, 0.0
        else:
            first_a, first_b = 0.0, 1.0
        t.extend([time, time + half, time + half, time + 1.0])
        a.extend([first_a, first_a, 0.0, 0.0])
        b.extend([first_b, first_b, 0.0, 0.0])
        time += 1.0
    return np.array(t), np.array(a), np.array(b)


def scope_page_1(pdf):
    fig = plt.figure(figsize=(8.27, 11.69))
    ax = fig.add_axes([0, 0, 1, 1])
    ax.set_axis_off()

    fig.text(0.06, 0.955, "Instructivo de osciloscopio", fontsize=22, fontweight="bold")
    fig.text(0.06, 0.925, "Validacion de la fase ARINC429 logic", fontsize=11, color="#4a6572")

    intro = (
        "Objetivo: registrar evidencia visual de que en esta fase ya se logro una recreacion "
        "logica/temporal con retorno a cero, antes de pasar a la futura capa electrica ARINC 429 real."
    )
    fig.text(0.06, 0.87, wrap(intro, 92), fontsize=10.5, va="top", linespacing=1.45)

    add_box(ax, 0.08, 0.58, 0.38, 0.18, "Canal FWD", "CH1 -> FWD_A\nCH2 -> FWD_B\nGND del osciloscopio -> GND comun", fc="#eef7ff")
    add_box(ax, 0.54, 0.58, 0.38, 0.18, "Canal REV", "CH1 -> REV_A\nCH2 -> REV_B\nGND del osciloscopio -> GND comun", fc="#fff7ee")
    add_box(ax, 0.08, 0.31, 0.84, 0.18, "Modo de medicion recomendado", "Primero medir cada hilo respecto de GND. Luego, si el osciloscopio lo permite, usar Math diferencial CH1 - CH2 para ver mejor la bipolaridad logica.", fc="#f3fdf4")

    caution = (
        "Importante: esta fase valida la forma logica/temporal de la senal. "
        "No valida todavia la capa electrica ARINC 429 real, porque aun no se usan "
        "transceptores ni niveles de linea ARINC industriales."
    )
    fig.text(0.06, 0.16, wrap(caution, 92), fontsize=10.5, fontweight="bold", va="top", linespacing=1.4)

    pdf.savefig(fig)
    plt.close(fig)


def scope_page_2(pdf):
    fig = plt.figure(figsize=(8.27, 11.69))
    fig.text(0.06, 0.955, "Que deberias ver", fontsize=18, fontweight="bold")

    ax1 = fig.add_axes([0.10, 0.58, 0.80, 0.25])
    bits = [1, 0, 1, 1, 0]
    t, a, b = scope_waveform(bits)
    ax1.step(t, a, where="post", label="Linea A")
    ax1.step(t, b, where="post", label="Linea B")
    ax1.set_ylim(-0.1, 1.2)
    ax1.set_xlim(0, max(t))
    ax1.set_ylabel("Nivel")
    ax1.set_xlabel("Tiempo relativo")
    ax1.set_title("Medicion de cada hilo contra GND")
    ax1.grid(True, alpha=0.25)
    ax1.legend(loc="upper right")

    ax2 = fig.add_axes([0.10, 0.22, 0.80, 0.25])
    diff = a - b
    ax2.step(t, diff, where="post", color="#7b3294")
    ax2.set_ylim(-1.2, 1.2)
    ax2.set_xlim(0, max(t))
    ax2.set_ylabel("A - B")
    ax2.set_xlabel("Tiempo relativo")
    ax2.set_title("Vista diferencial conceptual")
    ax2.grid(True, alpha=0.25)

    notes = (
        "Interpretacion:\n"
        "- Primera mitad del bit: actividad en A o B\n"
        "- Segunda mitad del bit: retorno a cero\n"
        "- Entre palabras: zona NULL mas larga\n"
        "- En diferencial deberian aparecer tres estados conceptuales: positivo, cero y negativo"
    )
    fig.text(0.10, 0.08, notes, fontsize=10.5, va="bottom", linespacing=1.5)

    pdf.savefig(fig)
    plt.close(fig)


def scope_page_3(pdf):
    fig = plt.figure(figsize=(8.27, 11.69))
    ax = fig.add_axes([0.05, 0.48, 0.90, 0.36])
    ax.set_axis_off()

    fig.text(0.06, 0.94, "Configuracion sugerida y checklist", fontsize=18, fontweight="bold")

    rows = [
        ["Acoplamiento", "DC"],
        ["Escala vertical inicial", "1 V/div o 2 V/div"],
        ["Base de tiempo inicial", "5 us/div o 10 us/div"],
        ["Trigger", "Flanco ascendente en CH1 o Math diferencial"],
        ["Modo", "Normal o single para capturas limpias"],
    ]

    table = ax.table(
        cellText=rows,
        colLabels=["Parametro", "Sugerencia"],
        loc="upper left",
        cellLoc="left",
        colLoc="left",
        colWidths=[0.36, 0.64],
    )
    table.auto_set_font_size(False)
    table.set_fontsize(10)
    table.scale(1, 1.7)
    for (row, col), cell in table.get_celld().items():
        cell.set_edgecolor("#7a8a99")
        if row == 0:
            cell.set_facecolor("#d9edf7")
            cell.set_text_props(fontweight="bold")
        else:
            cell.set_facecolor("#fbfdff" if row % 2 else "#f3f7fb")

    checklist = (
        "Checklist de evidencia:\n"
        "1. Captura de FWD_A y FWD_B contra GND.\n"
        "2. Captura diferencial CH1 - CH2.\n"
        "3. Zoom de pocos bits donde se vea el retorno a cero.\n"
        "4. Captura con separacion de palabra visible.\n"
        "5. En el informe, aclarar que esta fase valida la recreacion logica/temporal y no la capa electrica real."
    )
    fig.text(0.06, 0.36, checklist, fontsize=10.5, va="top", linespacing=1.5)

    closing = (
        "Criterio de exito: si las dos lineas muestran pulsos alternados con retorno a cero "
        "y la vista diferencial exhibe la bipolaridad logica esperada, la fase ARINC429 logic "
        "queda visualmente demostrada."
    )
    fig.text(0.06, 0.11, wrap(closing, 92), fontsize=10.5, fontweight="bold", va="bottom")

    pdf.savefig(fig)
    plt.close(fig)


def main():
    PDF_DIR.mkdir(exist_ok=True)

    with PdfPages(SUMMARY_PDF) as pdf:
        summary_page_1(pdf)
        summary_page_2(pdf)
        summary_page_3(pdf)
        summary_page_4(pdf)

    with PdfPages(SCOPE_PDF) as pdf:
        scope_page_1(pdf)
        scope_page_2(pdf)
        scope_page_3(pdf)

    print(f"PDF generado: {SUMMARY_PDF}")
    print(f"PDF generado: {SCOPE_PDF}")

    build_scope_evidence_pdf()
    print(f"PDF generado: {SCOPE_EVIDENCE_PDF}")

    build_commands_pdf()
    print(f"PDF generado: {COMMANDS_PDF}")


if __name__ == "__main__":
    main()
