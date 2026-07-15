#!/usr/bin/env python3
import argparse
import csv
import json
import os
import re
import signal
import subprocess
import sys
import time
from datetime import datetime
from pathlib import Path
from urllib import error as urllib_error
from urllib import request as urllib_request


DEFAULT_BRIDGE_URL = "http://127.0.0.1:5100"
DEFAULT_RESULTS_DIR = "~/PAMPA/ARINC_RESULTS"
ACTIVE_SESSION_FILE = ".active_session.json"
COUNTER_BALANCE_TOLERANCE_WORDS = 16384

CSV_FIELDS = [
    "timestamp",
    "elapsed_sec",
    "connected",
    "received_words",
    "accepted_words",
    "filtered_words",
    "parity_errors",
    "accepted_rate_wps",
    "received_rate_wps",
    "detected_bit_rate_bps",
    "detected_bit_rate_txt",
    "spi_errors",
    "spi_startup_errors",
    "overflow_events",
    "spi_drop_events",
    "fwd_resync_events",
    "fwd_startup_resync_events",
    "fwd_operational_resync_events",
    "rev_resync_events",
    "temperature_c",
    "temperature_ssm",
    "speed_kt",
    "speed_ssm",
    "altitude_ft",
    "altitude_ssm",
    "last_error",
]

EVENT_FIELDS = [
    "timestamp",
    "elapsed_sec",
    "event",
    "field",
    "previous",
    "current",
    "detail",
]

MONITORED_COUNTERS = [
    "spi_errors",
    "spi_startup_errors",
    "parity_errors_sniffer",
    "overflow_events",
    "spi_drop_events",
    "fwd_startup_resync_events",
    "fwd_operational_resync_events",
    "rev_resync_events",
]


def now_iso():
    return datetime.now().astimezone().isoformat(timespec="seconds")


def atomic_write_json(path, payload):
    path = Path(path)
    temp_path = path.with_suffix(path.suffix + ".tmp")
    temp_path.write_text(
        json.dumps(payload, indent=2, sort_keys=True, ensure_ascii=True) + "\n",
        encoding="utf-8",
    )
    temp_path.replace(path)


def parse_duration(value):
    if value is None:
        return None
    text = str(value).strip().lower()
    if not text:
        return None
    if re.fullmatch(r"\d+(?:\.\d+)?", text):
        return float(text)

    unit_seconds = {"s": 1.0, "m": 60.0, "h": 3600.0, "d": 86400.0}
    total = 0.0
    position = 0
    for match in re.finditer(r"(\d+(?:\.\d+)?)([smhd])", text):
        if match.start() != position:
            raise argparse.ArgumentTypeError(f"duracion invalida: {value}")
        total += float(match.group(1)) * unit_seconds[match.group(2)]
        position = match.end()
    if position != len(text) or total <= 0.0:
        raise argparse.ArgumentTypeError(f"duracion invalida: {value}")
    return total


def slugify(value):
    cleaned = re.sub(r"[^A-Za-z0-9_-]+", "_", value.strip())
    return cleaned.strip("_") or "prueba"


def counter_delta(current, baseline):
    current_value = int(current or 0)
    baseline_value = int(baseline or 0)
    if current_value >= baseline_value:
        return current_value - baseline_value
    return current_value


def get_json(url, timeout=4.0):
    req = urllib_request.Request(url, headers={"Accept": "application/json"})
    with urllib_request.urlopen(req, timeout=timeout) as response:
        return json.loads(response.read().decode("utf-8"))


def process_exists(pid):
    try:
        os.kill(int(pid), 0)
        return True
    except (OSError, ValueError):
        return False


def git_metadata(repo_root):
    result = {"branch": None, "commit": None, "dirty": None}
    commands = {
        "branch": ["git", "branch", "--show-current"],
        "commit": ["git", "rev-parse", "HEAD"],
        "dirty": ["git", "status", "--porcelain"],
    }
    for key, command in commands.items():
        try:
            completed = subprocess.run(
                command,
                cwd=repo_root,
                check=True,
                capture_output=True,
                text=True,
                timeout=5,
            )
            output = completed.stdout.strip()
            result[key] = bool(output) if key == "dirty" else output
        except (OSError, subprocess.SubprocessError):
            result[key] = None
    return result


def latest_value(stats, name):
    record = stats.get("latest_by_name", {}).get(name, {})
    value = record.get("value")
    try:
        numeric = float(value)
    except (TypeError, ValueError):
        numeric = None
    return numeric, record.get("ssm_txt", "")


def format_bit_rate(bit_rate_bps):
    try:
        bit_rate_bps = int(bit_rate_bps or 0)
    except (TypeError, ValueError):
        bit_rate_bps = 0
    if bit_rate_bps == 100000:
        return "100 kbps"
    if bit_rate_bps == 12500:
        return "12.5 kbps"
    if bit_rate_bps:
        return f"{bit_rate_bps} bps"
    return "unknown"


def session_counters(stats, baseline):
    keys = [
        "received_words",
        "accepted_words",
        "filtered_words",
        "parity_errors_sniffer",
        "spi_errors",
        "spi_startup_errors",
        "overflow_events",
        "spi_drop_events",
        "fwd_resync_events",
        "fwd_startup_resync_events",
        "fwd_operational_resync_events",
        "rev_resync_events",
    ]
    return {
        key: counter_delta(stats.get(key, 0), baseline.get(key, 0))
        for key in keys
    }


def counter_quality(counters):
    received = int(counters.get("received_words", 0))
    classified = sum(
        int(counters.get(field, 0))
        for field in (
            "accepted_words",
            "filtered_words",
            "parity_errors_sniffer",
            "overflow_events",
        )
    )
    balance = received - classified
    return {
        "classified_words": classified,
        "counter_balance_words": balance,
        "counter_balance_tolerance_words": COUNTER_BALANCE_TOLERANCE_WORDS,
        "counter_semantics_consistent": (
            balance >= -COUNTER_BALANCE_TOLERANCE_WORDS
        ),
        "parity_error_ratio_received": (
            int(counters.get("parity_errors_sniffer", 0)) / received
            if received > 0
            else None
        ),
    }


def build_sample(stats, baseline, elapsed, previous_sample):
    counters = session_counters(stats, baseline)
    temperature, temperature_ssm = latest_value(stats, "TEMPERATURA")
    speed, speed_ssm = latest_value(stats, "VELOCIDAD")
    altitude, altitude_ssm = latest_value(stats, "ALTITUD")

    accepted_rate = 0.0
    received_rate = 0.0
    if previous_sample is not None:
        delta_time = elapsed - float(previous_sample["elapsed_sec"])
        if delta_time > 0.0:
            accepted_rate = (
                counters["accepted_words"] - int(previous_sample["accepted_words"])
            ) / delta_time
            received_rate = (
                counters["received_words"] - int(previous_sample["received_words"])
            ) / delta_time

    return {
        "timestamp": now_iso(),
        "elapsed_sec": round(elapsed, 3),
        "connected": 1 if stats.get("connected") else 0,
        "received_words": counters["received_words"],
        "accepted_words": counters["accepted_words"],
        "filtered_words": counters["filtered_words"],
        "parity_errors": counters["parity_errors_sniffer"],
        "accepted_rate_wps": round(max(accepted_rate, 0.0), 3),
        "received_rate_wps": round(max(received_rate, 0.0), 3),
        "detected_bit_rate_bps": int(stats.get("detected_bit_rate_bps", 0) or 0),
        "detected_bit_rate_txt": stats.get("detected_bit_rate_txt") or format_bit_rate(stats.get("detected_bit_rate_bps")),
        "spi_errors": counters["spi_errors"],
        "spi_startup_errors": counters["spi_startup_errors"],
        "overflow_events": counters["overflow_events"],
        "spi_drop_events": counters["spi_drop_events"],
        "fwd_resync_events": counters["fwd_resync_events"],
        "fwd_startup_resync_events": counters["fwd_startup_resync_events"],
        "fwd_operational_resync_events": counters["fwd_operational_resync_events"],
        "rev_resync_events": counters["rev_resync_events"],
        "temperature_c": "" if temperature is None else temperature,
        "temperature_ssm": temperature_ssm,
        "speed_kt": "" if speed is None else speed,
        "speed_ssm": speed_ssm,
        "altitude_ft": "" if altitude is None else altitude,
        "altitude_ssm": altitude_ssm,
        "last_error": stats.get("last_error", ""),
    }


def pdf_escape(text):
    return (
        str(text)
        .encode("ascii", "replace")
        .decode("ascii")
        .replace("\\", "\\\\")
        .replace("(", "\\(")
        .replace(")", "\\)")
    )


class PdfCanvas:
    def __init__(self):
        self.operations = []

    def text(self, x, y, text, size=10):
        self.operations.append(
            f"BT /F1 {size:.1f} Tf {x:.1f} {y:.1f} Td ({pdf_escape(text)}) Tj ET"
        )

    def line(self, x1, y1, x2, y2, width=0.8, color=(0.15, 0.2, 0.3)):
        r, g, b = color
        self.operations.append(
            f"{r:.3f} {g:.3f} {b:.3f} RG {width:.2f} w "
            f"{x1:.1f} {y1:.1f} m {x2:.1f} {y2:.1f} l S"
        )

    def polyline(self, points, width=1.0, color=(0.0, 0.35, 0.65)):
        if len(points) < 2:
            return
        r, g, b = color
        path = [f"{points[0][0]:.1f} {points[0][1]:.1f} m"]
        path.extend(f"{x:.1f} {y:.1f} l" for x, y in points[1:])
        self.operations.append(
            f"{r:.3f} {g:.3f} {b:.3f} RG {width:.2f} w {' '.join(path)} S"
        )

    def rect(self, x, y, width, height, color=(0.15, 0.2, 0.3)):
        r, g, b = color
        self.operations.append(
            f"{r:.3f} {g:.3f} {b:.3f} RG 0.8 w "
            f"{x:.1f} {y:.1f} {width:.1f} {height:.1f} re S"
        )

    def stream(self):
        return ("\n".join(self.operations) + "\n").encode("ascii")


def write_pdf(path, pages):
    objects = [None, None, None]
    page_ids = []
    for page in pages:
        page_id = len(objects) + 1
        content_id = page_id + 1
        page_ids.append(page_id)
        stream = page.stream()
        objects.append(
            (
                f"<< /Type /Page /Parent 2 0 R /MediaBox [0 0 612 792] "
                f"/Resources << /Font << /F1 3 0 R >> >> "
                f"/Contents {content_id} 0 R >>"
            ).encode("ascii")
        )
        objects.append(
            f"<< /Length {len(stream)} >>\nstream\n".encode("ascii")
            + stream
            + b"endstream"
        )

    objects[0] = b"<< /Type /Catalog /Pages 2 0 R >>"
    kids = " ".join(f"{page_id} 0 R" for page_id in page_ids)
    objects[1] = f"<< /Type /Pages /Kids [{kids}] /Count {len(page_ids)} >>".encode("ascii")
    objects[2] = b"<< /Type /Font /Subtype /Type1 /BaseFont /Helvetica >>"

    output = bytearray(b"%PDF-1.4\n%\xe2\xe3\xcf\xd3\n")
    offsets = [0]
    for object_id, payload in enumerate(objects, start=1):
        offsets.append(len(output))
        output.extend(f"{object_id} 0 obj\n".encode("ascii"))
        output.extend(payload)
        output.extend(b"\nendobj\n")

    xref_offset = len(output)
    output.extend(f"xref\n0 {len(objects) + 1}\n".encode("ascii"))
    output.extend(b"0000000000 65535 f \n")
    for offset in offsets[1:]:
        output.extend(f"{offset:010d} 00000 n \n".encode("ascii"))
    output.extend(
        (
            f"trailer\n<< /Size {len(objects) + 1} /Root 1 0 R >>\n"
            f"startxref\n{xref_offset}\n%%EOF\n"
        ).encode("ascii")
    )
    Path(path).write_bytes(output)


def downsample(rows, maximum=500):
    if len(rows) <= maximum:
        return rows
    step = len(rows) / maximum
    return [rows[min(int(index * step), len(rows) - 1)] for index in range(maximum)]


def draw_chart(canvas, x, y, width, height, title, rows, fields):
    canvas.text(x, y + height + 12, title, 10)
    canvas.rect(x, y, width, height)
    if not rows:
        canvas.text(x + 8, y + height / 2, "Sin muestras", 9)
        return

    sampled = downsample(rows)
    x_values = [float(row.get("elapsed_sec", 0.0)) for row in sampled]
    series_values = []
    for field, _label, _color in fields:
        values = []
        for row in sampled:
            try:
                values.append(float(row.get(field, 0.0) or 0.0))
            except (TypeError, ValueError):
                values.append(0.0)
        series_values.append(values)

    x_min = min(x_values)
    x_max = max(x_values)
    if x_max <= x_min:
        x_max = x_min + 1.0
    all_values = [value for values in series_values for value in values]
    y_min = min(all_values) if all_values else 0.0
    y_max = max(all_values) if all_values else 1.0
    if y_max <= y_min:
        y_max = y_min + 1.0

    canvas.text(x, y - 12, "0 s", 7)
    canvas.text(x + width - 55, y - 12, f"{x_max:.0f} s", 7)
    canvas.text(x + 3, y + height - 10, f"{y_max:.1f}", 7)
    canvas.text(x + 3, y + 3, f"{y_min:.1f}", 7)

    for (field, label, color), values in zip(fields, series_values):
        points = []
        for x_value, y_value in zip(x_values, values):
            px = x + ((x_value - x_min) / (x_max - x_min)) * width
            py = y + ((y_value - y_min) / (y_max - y_min)) * height
            points.append((px, py))
        canvas.polyline(points, width=1.0, color=color)

    legend_x = x + 8
    legend_y = y + height - 22
    for _field, label, color in fields:
        canvas.line(legend_x, legend_y + 3, legend_x + 12, legend_y + 3, 1.5, color)
        canvas.text(legend_x + 16, legend_y, label, 7)
        legend_x += 95


def generate_report_pdf(path, summary, samples):
    page1 = PdfCanvas()
    page1.text(42, 752, "PAMPA / ARINC 429 - Informe estadistico", 18)
    page1.text(42, 730, f"Sesion: {summary['session_name']}", 11)
    page1.text(42, 714, f"Inicio: {summary['started_at']}", 9)
    page1.text(42, 700, f"Fin: {summary['ended_at']}", 9)
    page1.text(42, 686, f"Motivo: {summary['termination_reason']}", 9)
    page1.text(440, 730, f"Resultado: {summary['verdict']}", 13)

    lines = [
        ("Duracion efectiva", f"{summary['duration_sec']:.1f} s"),
        ("Muestras", summary["sample_count"]),
        ("Palabras recibidas", summary["counters"]["received_words"]),
        ("Palabras aceptadas", summary["counters"]["accepted_words"]),
        ("Palabras filtradas", summary["counters"]["filtered_words"]),
        ("Palabras clasificadas", summary["quality"]["classified_words"]),
        ("Balance recibidas-clasificadas", summary["quality"]["counter_balance_words"]),
        ("Tasa media aceptada", f"{summary['rates']['accepted_average_wps']:.2f} words/s"),
        ("Tasa maxima aceptada", f"{summary['rates']['accepted_max_wps']:.2f} words/s"),
        ("Velocidad detectada final", format_bit_rate((summary.get("final_stats") or {}).get("detected_bit_rate_bps"))),
        ("Errores de paridad", summary["counters"]["parity_errors_sniffer"]),
        ("Errores SPI operativos", summary["counters"]["spi_errors"]),
        ("Errores SPI de arranque", summary["counters"]["spi_startup_errors"]),
        ("Overflow", summary["counters"]["overflow_events"]),
        ("Drops SPI", summary["counters"]["spi_drop_events"]),
        ("Resync FWD de arranque", summary["counters"]["fwd_startup_resync_events"]),
        ("Resync FWD operativos", summary["counters"]["fwd_operational_resync_events"]),
    ]
    page1.text(42, 650, "Resumen", 13)
    y = 628
    for label, value in lines:
        page1.text(52, y, label, 9)
        page1.text(300, y, value, 9)
        page1.line(50, y - 4, 560, y - 4, 0.25, (0.75, 0.77, 0.8))
        y -= 21

    page1.text(42, 272, "Configuracion", 13)
    config = summary.get("configuration", {})
    config_lines = [
        f"Bridge: {config.get('bridge_url', '-')}",
        f"SPI: {config.get('spi_transfer_mode', '-')} a {config.get('spi_requested_hz', '-')} Hz",
        f"ARINC detectado al inicio: {config.get('detected_bit_rate_txt') or format_bit_rate(config.get('detected_bit_rate_bps'))}",
        f"Puerto: {config.get('port', '-')}",
        f"Objetivo: duracion={config.get('duration_sec')} s, palabras={config.get('word_target')}",
        f"Firmware TX: {config.get('tx_firmware', '-')}",
        f"Firmware RX: {config.get('rx_firmware', '-')}",
        f"Firmware sniffer: {config.get('sniffer_firmware', '-')}",
    ]
    y = 250
    for line in config_lines:
        page1.text(52, y, line, 9)
        y -= 18

    page1.text(42, 105, "Criterio", 13)
    page1.text(52, 84, summary.get("verdict_detail", ""), 9)

    page2 = PdfCanvas()
    page2.text(42, 752, "Evolucion temporal", 16)
    draw_chart(
        page2,
        50,
        525,
        510,
        170,
        "Palabras acumuladas",
        samples,
        [
            ("received_words", "recibidas", (0.15, 0.35, 0.7)),
            ("accepted_words", "aceptadas", (0.0, 0.55, 0.25)),
            ("filtered_words", "filtradas", (0.75, 0.35, 0.05)),
        ],
    )
    draw_chart(
        page2,
        50,
        285,
        510,
        170,
        "Tasa de palabras",
        samples,
        [
            ("received_rate_wps", "recibidas/s", (0.15, 0.35, 0.7)),
            ("accepted_rate_wps", "aceptadas/s", (0.0, 0.55, 0.25)),
        ],
    )
    draw_chart(
        page2,
        50,
        45,
        510,
        170,
        "Errores y resincronizaciones operativas",
        samples,
        [
            ("spi_errors", "SPI", (0.8, 0.1, 0.1)),
            ("parity_errors", "paridad", (0.6, 0.0, 0.5)),
            ("fwd_operational_resync_events", "resync FWD", (0.85, 0.45, 0.0)),
        ],
    )
    write_pdf(path, [page1, page2])


def determine_verdict(
    counters,
    termination_reason,
    expect_parity_errors=False,
    quality=None,
):
    failures = []
    quality = quality or counter_quality(counters)
    for field in ("spi_errors", "overflow_events", "spi_drop_events"):
        if int(counters.get(field, 0)) > 0:
            failures.append(f"{field}={counters[field]}")
    if not quality["counter_semantics_consistent"]:
        failures.append(
            "contadores incompatibles con rechazo estricto "
            f"(balance={quality['counter_balance_words']}, "
            f"tolerancia={quality['counter_balance_tolerance_words']})"
        )
    parity_errors = int(counters.get("parity_errors_sniffer", 0))
    if expect_parity_errors:
        if parity_errors == 0:
            failures.append("no se detectaron los errores de paridad esperados")
    elif parity_errors > 0:
        failures.append(f"parity_errors_sniffer={parity_errors}")
    if termination_reason in ("link_timeout", "start_timeout", "recorder_error"):
        failures.append(f"termination={termination_reason}")
    if failures:
        return "FAIL", ", ".join(failures)

    warnings = []
    if int(counters.get("fwd_operational_resync_events", 0)) > 0:
        warnings.append(
            f"fwd_operational_resync_events={counters['fwd_operational_resync_events']}"
        )
    if warnings:
        return "WARN", ", ".join(warnings)
    if expect_parity_errors:
        return "PASS", f"Se detectaron {parity_errors} errores de paridad inyectados."
    return "PASS", "Sin errores operativos, paridad invalida, overflow ni drops SPI."


class Recorder:
    def __init__(self, args):
        self.args = args
        self.stop_requested = False
        self.stop_reason = "manual_signal"
        self.output_root = Path(args.output_dir).expanduser().resolve()
        timestamp = datetime.now().strftime("%Y%m%d_%H%M%S")
        self.session_name = f"{timestamp}_{slugify(args.name)}"
        self.session_dir = self.output_root / self.session_name
        self.metadata_path = self.session_dir / "metadata.json"
        self.samples_path = self.session_dir / "samples.csv"
        self.events_path = self.session_dir / "events.csv"
        self.summary_path = self.session_dir / "summary.json"
        self.pdf_path = self.session_dir / "report.pdf"
        self.active_path = self.output_root / ACTIVE_SESSION_FILE
        self.samples = []
        self.baseline = None
        self.last_stats = None
        self.started_monotonic = None
        self.started_at = None
        self.last_connected_monotonic = None
        self.previous_connected = None

    def request_stop(self, _signum=None, _frame=None):
        self.stop_requested = True

    def write_event(self, writer, event, elapsed=0.0, field="", previous="", current="", detail=""):
        writer.writerow(
            {
                "timestamp": now_iso(),
                "elapsed_sec": round(elapsed, 3),
                "event": event,
                "field": field,
                "previous": previous,
                "current": current,
                "detail": detail,
            }
        )

    def setup(self):
        self.output_root.mkdir(parents=True, exist_ok=True)
        if self.active_path.exists():
            try:
                active = json.loads(self.active_path.read_text(encoding="utf-8"))
            except (OSError, json.JSONDecodeError):
                active = {}
            if process_exists(active.get("pid")):
                raise RuntimeError(
                    f"ya existe una sesion activa con PID {active.get('pid')}: "
                    f"{active.get('session_dir')}"
                )
            stale_metadata_path = active.get("metadata")
            if stale_metadata_path and Path(stale_metadata_path).exists():
                try:
                    stale_metadata = json.loads(
                        Path(stale_metadata_path).read_text(encoding="utf-8")
                    )
                    stale_metadata.update(
                        {
                            "status": "interrupted",
                            "ended_at": now_iso(),
                            "termination_reason": "unclean_shutdown",
                        }
                    )
                    atomic_write_json(stale_metadata_path, stale_metadata)
                except (OSError, json.JSONDecodeError):
                    pass
            self.active_path.unlink(missing_ok=True)

        self.session_dir.mkdir(parents=False)
        repo_root = Path(__file__).resolve().parent.parent
        metadata = {
            "schema_version": 1,
            "status": "armed",
            "session_name": self.session_name,
            "created_at": now_iso(),
            "pid": os.getpid(),
            "configuration": {
                "bridge_url": self.args.bridge_url,
                "sample_interval_sec": self.args.interval,
                "fsync_every_samples": self.args.fsync_every,
                "duration_sec": self.args.duration,
                "word_target": self.args.words,
                "disconnect_timeout_sec": self.args.disconnect_timeout,
                "start_timeout_sec": self.args.start_timeout,
                "start_condition": self.args.start_condition,
                "tx_firmware": self.args.tx_firmware,
                "rx_firmware": self.args.rx_firmware,
                "sniffer_firmware": self.args.sniffer_firmware,
                "notes": self.args.notes,
                "expect_parity_errors": self.args.expect_parity_errors,
            },
            "git": git_metadata(repo_root),
        }
        atomic_write_json(self.metadata_path, metadata)
        atomic_write_json(
            self.active_path,
            {
                "pid": os.getpid(),
                "session_name": self.session_name,
                "session_dir": str(self.session_dir),
                "metadata": str(self.metadata_path),
            },
        )

    def update_metadata(self, **changes):
        metadata = json.loads(self.metadata_path.read_text(encoding="utf-8"))
        metadata.update(changes)
        atomic_write_json(self.metadata_path, metadata)

    def start_recording(self, stats, baseline, event_writer):
        self.baseline = dict(baseline)
        self.started_monotonic = time.monotonic()
        self.started_at = now_iso()
        self.last_connected_monotonic = self.started_monotonic
        configuration = json.loads(self.metadata_path.read_text(encoding="utf-8"))["configuration"]
        configuration.update(
            {
                "port": stats.get("port"),
                "source_mode": stats.get("source_mode"),
                "spi_requested_hz": stats.get("spi_requested_hz"),
                "spi_transfer_mode": stats.get("spi_transfer_mode"),
                "spi_manual_cs": stats.get("spi_manual_cs"),
                "spi_cs_setup_us": stats.get("spi_cs_setup_us"),
                "spi_cs_hold_us": stats.get("spi_cs_hold_us"),
                "detected_bit_rate_bps": stats.get("detected_bit_rate_bps"),
                "detected_bit_rate_txt": stats.get("detected_bit_rate_txt"),
            }
        )
        self.update_metadata(
            status="recording",
            started_at=self.started_at,
            baseline=self.baseline,
            configuration=configuration,
        )
        self.write_event(event_writer, "session_started", detail="condicion de inicio cumplida")

    def finalize(self, reason, final_stats):
        ended_at = now_iso()
        duration = 0.0
        if self.started_monotonic is not None:
            duration = max(time.monotonic() - self.started_monotonic, 0.0)
        counters = session_counters(final_stats or {}, self.baseline or {})
        quality = counter_quality(counters)
        max_rate = max((float(row["accepted_rate_wps"]) for row in self.samples), default=0.0)
        average_rate = counters["accepted_words"] / duration if duration > 0 else 0.0
        verdict, detail = determine_verdict(
            counters,
            reason,
            expect_parity_errors=self.args.expect_parity_errors,
            quality=quality,
        )
        metadata = json.loads(self.metadata_path.read_text(encoding="utf-8"))
        summary = {
            "schema_version": 1,
            "session_name": self.session_name,
            "started_at": self.started_at,
            "ended_at": ended_at,
            "duration_sec": round(duration, 3),
            "sample_count": len(self.samples),
            "termination_reason": reason,
            "verdict": verdict,
            "verdict_detail": detail,
            "counters": counters,
            "quality": quality,
            "rates": {
                "accepted_average_wps": round(average_rate, 3),
                "accepted_max_wps": round(max_rate, 3),
            },
            "configuration": metadata.get("configuration", {}),
            "baseline": self.baseline,
            "final_stats": final_stats,
        }
        atomic_write_json(self.summary_path, summary)
        generate_report_pdf(self.pdf_path, summary, self.samples)
        self.update_metadata(
            status="finished",
            ended_at=ended_at,
            termination_reason=reason,
            verdict=verdict,
            summary=str(self.summary_path),
            report_pdf=str(self.pdf_path),
        )
        try:
            active = json.loads(self.active_path.read_text(encoding="utf-8"))
        except (OSError, json.JSONDecodeError):
            active = {}
        if active.get("pid") == os.getpid():
            self.active_path.unlink(missing_ok=True)
        return summary

    def mark_failed(self, exc):
        if self.metadata_path.exists():
            try:
                self.update_metadata(
                    status="failed",
                    ended_at=now_iso(),
                    termination_reason="recorder_error",
                    error=str(exc),
                )
            except (OSError, json.JSONDecodeError):
                pass
        try:
            active = json.loads(self.active_path.read_text(encoding="utf-8"))
        except (OSError, json.JSONDecodeError):
            active = {}
        if active.get("pid") == os.getpid():
            self.active_path.unlink(missing_ok=True)

    def run(self):
        self.setup()
        signal.signal(signal.SIGTERM, self.request_stop)
        signal.signal(signal.SIGINT, self.request_stop)

        armed_at = time.monotonic()
        candidate_baseline = None
        termination_reason = "manual_signal"
        final_stats = {}

        with self.samples_path.open("w", newline="", encoding="utf-8") as sample_file, \
                self.events_path.open("w", newline="", encoding="utf-8") as event_file:
            sample_writer = csv.DictWriter(sample_file, fieldnames=CSV_FIELDS)
            event_writer = csv.DictWriter(event_file, fieldnames=EVENT_FIELDS)
            sample_writer.writeheader()
            event_writer.writeheader()
            self.write_event(event_writer, "session_armed", detail=self.args.start_condition)
            sample_file.flush()
            event_file.flush()

            while not self.stop_requested:
                loop_started = time.monotonic()
                stats = None
                try:
                    stats = get_json(f"{self.args.bridge_url.rstrip('/')}/stats")
                except (OSError, urllib_error.URLError, json.JSONDecodeError) as exc:
                    if self.started_monotonic is not None:
                        elapsed = time.monotonic() - self.started_monotonic
                        if self.previous_connected is not False:
                            self.write_event(event_writer, "link_down", elapsed, detail=str(exc))
                            event_file.flush()
                        self.previous_connected = False
                        if (
                            self.last_connected_monotonic is not None
                            and time.monotonic() - self.last_connected_monotonic
                            >= self.args.disconnect_timeout
                        ):
                            termination_reason = "link_timeout"
                            break
                    elif time.monotonic() - armed_at >= self.args.start_timeout:
                        termination_reason = "start_timeout"
                        break
                else:
                    connected = bool(stats.get("connected"))
                    if connected:
                        final_stats = stats
                        self.last_connected_monotonic = time.monotonic()
                        if self.previous_connected is False and self.started_monotonic is not None:
                            elapsed = time.monotonic() - self.started_monotonic
                            self.write_event(event_writer, "link_up", elapsed)
                            event_file.flush()
                        self.previous_connected = True

                        if candidate_baseline is None:
                            candidate_baseline = dict(stats)

                        if self.started_monotonic is None:
                            should_start = self.args.start_condition == "connected"
                            if self.args.start_condition == "traffic":
                                should_start = (
                                    int(stats.get("accepted_words", 0))
                                    != int(candidate_baseline.get("accepted_words", 0))
                                )
                            if should_start:
                                self.start_recording(stats, candidate_baseline, event_writer)
                                event_file.flush()
                        else:
                            elapsed = time.monotonic() - self.started_monotonic
                            sample = build_sample(
                                stats,
                                self.baseline,
                                elapsed,
                                self.samples[-1] if self.samples else None,
                            )
                            sample_writer.writerow(sample)
                            self.samples.append(sample)
                            sample_file.flush()
                            if len(self.samples) % self.args.fsync_every == 0:
                                os.fsync(sample_file.fileno())
                                event_file.flush()
                                os.fsync(event_file.fileno())

                            previous_stats = self.last_stats or self.baseline
                            for field in MONITORED_COUNTERS:
                                previous = counter_delta(
                                    previous_stats.get(field, 0),
                                    self.baseline.get(field, 0),
                                )
                                current = counter_delta(
                                    stats.get(field, 0),
                                    self.baseline.get(field, 0),
                                )
                                if current > previous:
                                    self.write_event(
                                        event_writer,
                                        "counter_increment",
                                        elapsed,
                                        field,
                                        previous,
                                        current,
                                    )
                            event_file.flush()
                            self.last_stats = dict(stats)

                            if self.args.duration is not None and elapsed >= self.args.duration:
                                termination_reason = "duration_reached"
                                break
                            if (
                                self.args.words is not None
                                and int(sample["accepted_words"]) >= self.args.words
                            ):
                                termination_reason = "word_target_reached"
                                break
                    else:
                        if self.started_monotonic is None:
                            candidate_baseline = None
                        elif self.previous_connected is not False:
                            elapsed = time.monotonic() - self.started_monotonic
                            self.write_event(
                                event_writer,
                                "link_down",
                                elapsed,
                                detail=stats.get("last_error", "connected=false"),
                            )
                            event_file.flush()
                            self.previous_connected = False
                        if (
                            self.started_monotonic is not None
                            and self.last_connected_monotonic is not None
                            and time.monotonic() - self.last_connected_monotonic
                            >= self.args.disconnect_timeout
                        ):
                            termination_reason = "link_timeout"
                            break
                    if self.started_monotonic is None and time.monotonic() - armed_at >= self.args.start_timeout:
                        termination_reason = "start_timeout"
                        break

                sleep_time = self.args.interval - (time.monotonic() - loop_started)
                if sleep_time > 0:
                    time.sleep(sleep_time)

            if self.stop_requested:
                termination_reason = self.stop_reason
            elapsed = (
                time.monotonic() - self.started_monotonic
                if self.started_monotonic is not None
                else 0.0
            )
            self.write_event(event_writer, "session_stopped", elapsed, detail=termination_reason)
            sample_file.flush()
            event_file.flush()
            os.fsync(sample_file.fileno())
            os.fsync(event_file.fileno())

        return self.finalize(termination_reason, final_stats)


def active_session(output_dir):
    active_path = Path(output_dir).expanduser().resolve() / ACTIVE_SESSION_FILE
    if not active_path.exists():
        return active_path, None
    try:
        return active_path, json.loads(active_path.read_text(encoding="utf-8"))
    except (OSError, json.JSONDecodeError):
        return active_path, None


def command_stop(args):
    active_path, active = active_session(args.output_dir)
    if not active:
        print("No hay una sesion activa.")
        return 1
    pid = active.get("pid")
    if not process_exists(pid):
        active_path.unlink(missing_ok=True)
        print("La sesion registrada ya no esta ejecutandose.")
        return 1
    os.kill(int(pid), signal.SIGTERM)
    print(f"SIGTERM enviado al registrador PID {pid}.")
    return 0


def command_status(args):
    _active_path, active = active_session(args.output_dir)
    if not active:
        print("No hay una sesion activa.")
        return 1
    running = process_exists(active.get("pid"))
    print(json.dumps({**active, "running": running}, indent=2, ensure_ascii=True))
    return 0 if running else 1


def build_parser():
    parser = argparse.ArgumentParser(
        description="Registra evidencia estadistica del bridge ARINC."
    )
    subparsers = parser.add_subparsers(dest="command", required=True)

    start = subparsers.add_parser("start", help="Inicia una sesion de registro.")
    start.add_argument("--name", default="prueba_arinc")
    start.add_argument("--bridge-url", default=DEFAULT_BRIDGE_URL)
    start.add_argument(
        "--output-dir",
        default=os.getenv("ARINC_RESULTS_DIR", DEFAULT_RESULTS_DIR),
    )
    start.add_argument("--interval", type=float, default=5.0)
    start.add_argument(
        "--fsync-every",
        type=int,
        default=12,
        help="Fuerza escritura fisica cada N muestras.",
    )
    start.add_argument("--duration", type=parse_duration)
    start.add_argument("--words", type=int)
    start.add_argument("--disconnect-timeout", type=float, default=30.0)
    start.add_argument("--start-timeout", type=float, default=300.0)
    start.add_argument(
        "--start-condition",
        choices=("traffic", "connected"),
        default="traffic",
    )
    start.add_argument(
        "--tx-firmware",
        default="arinc_tx_arinc429_logic_stream",
    )
    start.add_argument(
        "--rx-firmware",
        default="arinc_rx_arinc429_logic_stream",
    )
    start.add_argument(
        "--sniffer-firmware",
        default="sniffer_arinc429_logic_pio_frame",
    )
    start.add_argument("--notes", default="")
    start.add_argument(
        "--expect-parity-errors",
        action="store_true",
        help="Exige detectar al menos un error de paridad inyectado.",
    )

    stop = subparsers.add_parser("stop", help="Finaliza limpiamente la sesion activa.")
    stop.add_argument(
        "--output-dir",
        default=os.getenv("ARINC_RESULTS_DIR", DEFAULT_RESULTS_DIR),
    )

    status = subparsers.add_parser("status", help="Muestra la sesion activa.")
    status.add_argument(
        "--output-dir",
        default=os.getenv("ARINC_RESULTS_DIR", DEFAULT_RESULTS_DIR),
    )
    return parser


def validate_start_args(args):
    if args.interval <= 0:
        raise ValueError("--interval debe ser mayor que cero")
    if args.words is not None and args.words <= 0:
        raise ValueError("--words debe ser mayor que cero")
    if args.fsync_every <= 0:
        raise ValueError("--fsync-every debe ser mayor que cero")
    if args.disconnect_timeout <= 0 or args.start_timeout <= 0:
        raise ValueError("los timeouts deben ser mayores que cero")


def main():
    parser = build_parser()
    args = parser.parse_args()
    recorder = None
    try:
        if args.command == "stop":
            return command_stop(args)
        if args.command == "status":
            return command_status(args)

        validate_start_args(args)
        recorder = Recorder(args)
        summary = recorder.run()
        print(json.dumps(summary, indent=2, ensure_ascii=True))
        return 0 if summary["verdict"] != "FAIL" else 2
    except Exception as exc:
        if recorder is not None:
            recorder.mark_failed(exc)
        print(f"ERROR: {exc}", file=sys.stderr)
        return 2


if __name__ == "__main__":
    raise SystemExit(main())
