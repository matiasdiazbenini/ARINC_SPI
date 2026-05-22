from pathlib import Path
from textwrap import fill

import matplotlib.pyplot as plt
from matplotlib.backends.backend_pdf import PdfPages
from matplotlib.patches import FancyArrowPatch, Rectangle


BASE_DIR = Path(__file__).resolve().parent
OUTPUT_PDF = BASE_DIR / "resumen_spi_sniffer_pi3.pdf"


def wrapped_lines(text, width):
    return fill(text, width=width)


def add_box(ax, xy, wh, title, body, fc="#f7fbff", ec="#5b6c7a"):
    x, y = xy
    w, h = wh
    rect = Rectangle((x, y), w, h, facecolor=fc, edgecolor=ec, linewidth=1.5)
    ax.add_patch(rect)
    ax.text(x + 0.02, y + h - 0.04, title, fontsize=12, fontweight="bold", va="top")
    ax.text(x + 0.02, y + h - 0.10, body, fontsize=10, va="top", linespacing=1.35)


def add_arrow(ax, start, end):
    arrow = FancyArrowPatch(start, end, arrowstyle="-|>", mutation_scale=16, linewidth=1.6, color="#355c7d")
    ax.add_patch(arrow)


def page_one(pdf):
    fig = plt.figure(figsize=(8.27, 11.69))
    ax = fig.add_axes([0, 0, 1, 1])
    ax.set_axis_off()

    fig.text(0.06, 0.955, "Resumen de Integracion SPI", fontsize=22, fontweight="bold")
    fig.text(0.06, 0.925, "Pico 2 WH sniffer <-> Raspberry Pi 3B+ | estado actual", fontsize=11, color="#4a6572")

    contexto = (
        "Objetivo: conectar la Pico sniffer a la Raspberry Pi 3B+ por SPI para que la Pi reciba eventos ya "
        "filtrados y desde ahi alimente Flask/Grafana.\n\n"
        "Roles buscados:\n"
        "- Raspberry Pi 3B+ = SPI master\n"
        "- Pico sniffer = SPI slave\n"
        "- El filtrado primario ARINC queda en la Pico, no en Flask"
    )
    fig.text(0.06, 0.86, wrapped_lines(contexto, 85), fontsize=10.5, va="top", linespacing=1.45)

    add_box(
        ax,
        (0.07, 0.49),
        (0.22, 0.18),
        "Pico MASTER",
        "Transmite ARINC-like\nCanal FWD\nGP2 / GP3",
        fc="#eef7ff",
    )
    add_box(
        ax,
        (0.39, 0.49),
        (0.22, 0.18),
        "Pico SNIFFER",
        "Escucha FWD y REV\nFiltra labels\nExporta a la Pi",
        fc="#f3fdf4",
    )
    add_box(
        ax,
        (0.71, 0.49),
        (0.22, 0.18),
        "Pico SLAVE",
        "Recibe FWD\nResponde ACK por REV\nGP4 / GP5",
        fc="#fff6ec",
    )
    add_box(
        ax,
        (0.39, 0.23),
        (0.30, 0.16),
        "Raspberry Pi 3B+",
        "SPI master\nBridge HTTP\nFlask / Grafana",
        fc="#fff5fa",
    )

    add_arrow(ax, (0.29, 0.58), (0.39, 0.58))
    add_arrow(ax, (0.71, 0.58), (0.61, 0.58))
    add_arrow(ax, (0.50, 0.49), (0.50, 0.39))

    fig.text(0.34, 0.605, "FWD", fontsize=10, color="#355c7d", fontweight="bold")
    fig.text(0.62, 0.605, "REV", fontsize=10, color="#355c7d", fontweight="bold")
    fig.text(0.515, 0.44, "SPI", fontsize=11, color="#355c7d", fontweight="bold")

    estado = (
        "Estado actual:\n"
        "- ARINC-like entre master/slave/sniffer: OK\n"
        "- Flask y bridge HTTP: OK\n"
        "- Cableado SPI principal y continuidad GPIO: OK\n"
        "- Problema aislado al comportamiento SPI slave hardware de la Pico"
    )
    fig.text(0.06, 0.16, wrapped_lines(estado, 88), fontsize=10.5, va="top", linespacing=1.45)

    pdf.savefig(fig)
    plt.close(fig)


def page_two(pdf):
    fig = plt.figure(figsize=(8.27, 11.69))
    ax = fig.add_axes([0.05, 0.55, 0.90, 0.34])
    ax.set_axis_off()

    fig.text(0.06, 0.94, "Pruebas Realizadas y Resultado", fontsize=18, fontweight="bold")

    rows = [
        ["Continuidad GPIO Pi->Pico", "GPIO test", "OK en CS / MOSI / SCLK"],
        ["Continuidad GPIO Pico->Pi", "GPIO test", "OK en MISO y GP20/GPIO25"],
        ["Heartbeat GP20", "gpio25_probe.py", "OK, la Pi ve transiciones"],
        ["SPI scope por GPIO", "SPI_SCOPE", "CS unico + 256 clocks + MOSI correcto"],
        ["SPI slave minimo", "SPI_MIN", "La Pico solo ve len=1"],
        ["Lectura en la 3B+", "spi_slave_min_test.py", "Bytes fijos saliendo de a uno o nulos"],
    ]

    table = ax.table(
        cellText=rows,
        colLabels=["Prueba", "Herramienta", "Resultado"],
        loc="upper left",
        cellLoc="left",
        colLoc="left",
        colWidths=[0.36, 0.20, 0.44],
    )
    table.auto_set_font_size(False)
    table.set_fontsize(9.5)
    table.scale(1, 1.6)
    for (row, col), cell in table.get_celld().items():
        if row == 0:
            cell.set_text_props(fontweight="bold", color="black")
            cell.set_facecolor("#d9edf7")
        else:
            cell.set_facecolor("#fbfdff" if row % 2 else "#f3f7fb")
        cell.set_edgecolor("#7a8a99")

    conclusiones = (
        "Conclusión técnica actual:\n"
        "La evidencia ya no apunta a cableado, pinout, Flask, bridge ni a la complejidad del sniffer ARINC. "
        "El problema quedó aislado al uso del SPI slave hardware de la Pico 2 WH en esta integración con la 3B+."
    )
    fig.text(0.06, 0.46, wrapped_lines(conclusiones, 93), fontsize=10.5, va="top", linespacing=1.45)

    opciones_titulo = "Opciones reales desde acá"
    fig.text(0.06, 0.39, opciones_titulo, fontsize=16, fontweight="bold")

    opciones = [
        (
            "1. Seguir con SPI slave hardware",
            "Probar registros crudos/SSI, IRQ en vez de polling, otra instancia SPI y ajuste fino de modos/temporización.\n"
            "Ventaja: mantiene SPI hardware puro.\n"
            "Desventaja: debugging más ingrato e incierto.",
        ),
        (
            "2. Implementar SPI slave por PIO",
            "Mantener la interfaz SPI, pero controlando framing, CS y timing desde PIO.\n"
            "Ventaja: más control y mejor capacidad de adaptación.\n"
            "Desventaja: más desarrollo inicial.",
        ),
        (
            "3. Cambiar la interfaz Pico <-> Pi",
            "Ejemplo: UART.\n"
            "Ventaja: probablemente más rápido de estabilizar.\n"
            "Desventaja: se aleja del camino SPI pedido para esta etapa.",
        ),
    ]

    y = 0.34
    for titulo, cuerpo in opciones:
        fig.text(0.07, y, titulo, fontsize=12, fontweight="bold", va="top")
        fig.text(0.09, y - 0.03, wrapped_lines(cuerpo, 88), fontsize=10, va="top", linespacing=1.35)
        y -= 0.14

    cierre = (
        "Recomendación práctica: si se quiere conservar SPI, la alternativa técnicamente más prometedora hoy es "
        "probar un slave SPI por PIO antes de seguir invirtiendo tiempo en el hardware SPI slave de la Pico."
    )
    fig.text(0.06, 0.05, wrapped_lines(cierre, 96), fontsize=10.5, fontweight="bold", va="bottom")

    pdf.savefig(fig)
    plt.close(fig)


def main():
    with PdfPages(OUTPUT_PDF) as pdf:
        page_one(pdf)
        page_two(pdf)
    print(f"PDF generado: {OUTPUT_PDF}")


if __name__ == "__main__":
    main()
