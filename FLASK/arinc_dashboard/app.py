import re
import time
import threading
import json
import os
from collections import Counter, deque
from urllib import error as urllib_error
from urllib import request as urllib_request

from flask import Flask, Response, jsonify, render_template, request

try:
    import serial
    import serial.tools.list_ports as serial_list_ports
    SERIAL_IMPORT_ERROR = None
except ModuleNotFoundError as exc:
    serial = None
    serial_list_ports = None
    SERIAL_IMPORT_ERROR = exc

app = Flask(__name__)

# El dashboard conserva mas registros para estadistica, pero limita la
# consola textual para no volver pesada la interfaz del navegador.
MAX_CONSOLE_LINES = 1000
MAX_RECORDS = 10000
SOURCE_MODE = os.getenv("ARINC_SOURCE_MODE", "serial").strip().lower()
BRIDGE_URL = os.getenv("ARINC_BRIDGE_URL", "http://127.0.0.1:5100").rstrip("/")
DASHBOARD_HOST = os.getenv("ARINC_DASHBOARD_HOST", "0.0.0.0").strip() or "0.0.0.0"
DASHBOARD_PORT = int(os.getenv("ARINC_DASHBOARD_PORT", "5000"))

latest_lines = deque(maxlen=MAX_CONSOLE_LINES)
latest_records = deque(maxlen=MAX_RECORDS)
data_lock = threading.Lock()

serial_thread = None
serial_stop = threading.Event()

state = {
    "running": False,
    "port": None,
    "baudrate": 115200,
    "filter_text": "",
    "log_file": "arinc_sniffer_log.txt",
    "last_rx_time": None,
    "serial_lines": 0,
    "serial_records": 0,
}


def append_line(text: str):
    """Agrega una linea con timestamp a la consola del dashboard."""
    timestamp = time.strftime("%H:%M:%S")
    with data_lock:
        latest_lines.append(f"[{timestamp}] {text}")


def list_serial_ports():
    """Enumera los puertos serie visibles desde Windows."""
    if SOURCE_MODE == "bridge":
        return []
    if serial_list_ports is None:
        return []
    return [p.device for p in serial_list_ports.comports()]


def bridge_get_json(path: str):
    req = urllib_request.Request(f"{BRIDGE_URL}{path}")
    with urllib_request.urlopen(req, timeout=2.0) as response:
        return json.loads(response.read().decode("utf-8"))


def bridge_get_text(path: str):
    req = urllib_request.Request(f"{BRIDGE_URL}{path}")
    with urllib_request.urlopen(req, timeout=2.0) as response:
        return response.read().decode("utf-8")


def bridge_post_json(path: str, payload: dict):
    body = json.dumps(payload).encode("utf-8")
    req = urllib_request.Request(
        f"{BRIDGE_URL}{path}",
        data=body,
        headers={"Content-Type": "application/json"},
        method="POST",
    )
    with urllib_request.urlopen(req, timeout=3.0) as response:
        return json.loads(response.read().decode("utf-8"))


def parse_filter_entries_text(text: str):
    entries = []
    for chunk in [part.strip() for part in text.split(",") if part.strip()]:
        if "/" in chunk:
            label_text, sdi_text = chunk.split("/", 1)
        else:
            label_text, sdi_text = chunk, "ANY"

        label_text = label_text.strip()
        sdi_text = sdi_text.strip().upper()
        label = int(label_text, 16) if label_text.lower().startswith("0x") else int(label_text, 16)
        sdi = 0xFF if sdi_text == "ANY" else int(sdi_text)
        entries.append({"label": label, "sdi": sdi})

    return entries


def should_store(line: str, filter_text: str) -> bool:
    """
    Filtro textual del lado Flask.

    Este filtro solo afecta lo que se muestra en la consola web. Los registros
    estructurados y las metricas se guardan completos porque el filtrado
    primario ya viene hecho por la Pico sniffer.
    """
    if not filter_text:
        return True

    terms = [t.strip().lower() for t in filter_text.split(",") if t.strip()]
    return any(term in line.lower() for term in terms)


def parse_match_line(line: str):
    """
    Parsea las lineas MATCH emitidas por la Pico ARINC_SNIFFER.

    Soporta:
    1. Formato nuevo compacto CSV:
       MATCH,A5,TEMPERATURA,280,20.0,C,0,3,NORMAL,OK
    2. Formato historico verbose:
       MATCH -> LABEL: 0xA5 (...) | ...
    """
    if line.startswith("MATCH,"):
        parts = [part.strip() for part in line.split(",")]
        if len(parts) != 10:
            return None

        return {
            "label": f"0x{parts[1]}",
            "name": parts[2],
            "raw": parts[3],
            "value": parts[4],
            "unit": parts[5],
            "sdi": parts[6],
            "ssm": parts[7],
            "ssm_txt": parts[8],
            "parity": parts[9],
        }

    pattern = (
        r"LABEL:\s*(0x[0-9A-Fa-f]+)\s*\(([^)]+)\)\s*\|\s*"
        r"RAW:\s*([0-9]+)\s*\|\s*"
        r"VAL:\s*([\-0-9.]+)\s*([A-Za-z]+)\s*\|\s*"
        r"SDI:\s*([0-9]+)\s*\|\s*"
        r"SSM:\s*([0-9]+)\s*\|\s*"
        r"SSM_TXT:\s*([A-Z_]+)\s*\|\s*"
        r"PARITY:\s*(OK|ERROR)"
    )

    match = re.search(pattern, line)
    if not match:
        return None

    return {
        "label": match.group(1),
        "name": match.group(2),
        "raw": match.group(3),
        "value": match.group(4),
        "unit": match.group(5),
        "sdi": match.group(6),
        "ssm": match.group(7),
        "ssm_txt": match.group(8),
        "parity": match.group(9),
    }


def handle_serial_line(line: str, filter_text: str, log_file):
    """
    Procesa una linea ya decodificada del puerto serie.

    Se separa de `serial_worker` para que la captura pueda leer por bloques
    grandes y luego despachar lineas una por una sin pagar costo extra de
    abrir archivos o de `readline()` por cada mensaje.
    """
    if not line:
        return

    visible = (
        line.startswith("[INFO]")
        or line.startswith("[ERROR]")
        or should_store(line, filter_text)
    )

    if visible:
        append_line(line)

    log_file.write(line + "\n")

    state["last_rx_time"] = time.strftime("%H:%M:%S")
    state["serial_lines"] += 1

    if line.startswith("MATCH"):
        record = parse_match_line(line)
        if record:
            record["timestamp"] = state["last_rx_time"]
            with data_lock:
                latest_records.append(record)
            state["serial_records"] += 1


def serial_worker(port: str, baudrate: int, filter_text: str):
    """
    Hilo de captura serial.

    Se abre el puerto una sola vez y se leen lineas completas. Cada linea:
    - se muestra en consola,
    - opcionalmente se persiste en archivo,
    - si es MATCH -> se transforma en registro estructurado.
    """
    try:
        if serial is None:
            raise RuntimeError(
                f"pyserial no esta instalado en este entorno ({SERIAL_IMPORT_ERROR}). "
                "En modo bridge no hace falta, pero en modo serial si."
            )

        append_line(f"[INFO] Intentando abrir {port} @ {baudrate}")
        if filter_text:
            append_line(f"[INFO] Filtro visual activo: {filter_text}")
        else:
            append_line("[INFO] Filtro visual inactivo")

        with serial.Serial(port=port, baudrate=baudrate, timeout=0.25) as ser, \
                open(state["log_file"], "a", encoding="utf-8", buffering=1) as log_file:
            # Se limpian residuos previos para arrancar la sesion desde cero.
            ser.reset_input_buffer()
            append_line(f"[INFO] Conectado a {port} @ {baudrate}")
            append_line("[INFO] Esperando datos del ARINC_SNIFFER...")
            pending_text = ""

            while not serial_stop.is_set():
                raw = ser.read(ser.in_waiting or 1)
                if not raw:
                    continue

                pending_text += raw.decode("utf-8", errors="replace")

                while "\n" in pending_text:
                    line, pending_text = pending_text.split("\n", 1)
                    handle_serial_line(line.strip(), filter_text, log_file)

            if pending_text.strip():
                handle_serial_line(pending_text.strip(), filter_text, log_file)

    except Exception as exc:
        append_line(f"[ERROR] No se pudo abrir/leer {port}: {exc}")
    finally:
        state["running"] = False
        append_line("[INFO] Lectura serial finalizada")


@app.route("/")
def index():
    """Pagina principal del dashboard."""
    return render_template(
        "index.html",
        ports=list_serial_ports(),
        baudrate=state["baudrate"],
        filter_text=state["filter_text"],
        source_mode=SOURCE_MODE,
        bridge_url=BRIDGE_URL,
    )


@app.route("/start", methods=["POST"])
def start():
    """Inicia una nueva sesion de captura."""
    global serial_thread

    if SOURCE_MODE == "bridge":
        return jsonify({"ok": False, "msg": "En modo bridge la captura la maneja spi_sniffer_bridge.py"}), 400

    if state["running"]:
        return jsonify({"ok": False, "msg": "Ya esta corriendo"}), 400

    if serial is None:
        return jsonify({
            "ok": False,
            "msg": f"pyserial no esta disponible en este entorno ({SERIAL_IMPORT_ERROR})",
        }), 400

    port = request.form.get("port", "").strip()
    baudrate = int(request.form.get("baudrate", "115200"))
    filter_text = request.form.get("filter_text", "").strip()

    if not port:
        return jsonify({"ok": False, "msg": "Debes elegir un puerto COM"}), 400

    serial_stop.clear()
    state["running"] = True
    state["port"] = port
    state["baudrate"] = baudrate
    state["filter_text"] = filter_text

    with data_lock:
        latest_lines.clear()
        latest_records.clear()
    state["last_rx_time"] = None
    state["serial_lines"] = 0
    state["serial_records"] = 0

    append_line("[INFO] Iniciando captura...")

    serial_thread = threading.Thread(
        target=serial_worker,
        args=(port, baudrate, filter_text),
        daemon=True,
    )
    serial_thread.start()

    return jsonify({"ok": True, "msg": "Captura iniciada"})


@app.route("/stop", methods=["POST"])
def stop():
    """Detiene la captura actual."""
    if SOURCE_MODE == "bridge":
        return jsonify({"ok": False, "msg": "En modo bridge no aplica detener desde Flask"}), 400

    serial_stop.set()
    append_line("[INFO] Deteniendo captura...")
    return jsonify({"ok": True, "msg": "Captura detenida"})


@app.route("/clear", methods=["POST"])
def clear():
    """Limpia buffers locales del dashboard."""
    if SOURCE_MODE == "bridge":
        return jsonify({"ok": False, "msg": "Usa el reset del bridge SPI para limpiar la sesion"}), 400

    with data_lock:
        latest_lines.clear()
        latest_records.clear()
    state["last_rx_time"] = None
    state["serial_lines"] = 0
    state["serial_records"] = 0

    append_line("[INFO] Consola limpiada")
    return jsonify({"ok": True})


@app.route("/lines")
def lines():
    """Devuelve la consola textual consolidada."""
    with data_lock:
        text = "\n".join(latest_lines)
    return jsonify({"text": text})


@app.route("/records")
def records():
    """Devuelve las palabras MATCH parseadas."""
    if SOURCE_MODE == "bridge":
        try:
            return jsonify(bridge_get_json("/records"))
        except (urllib_error.URLError, json.JSONDecodeError) as exc:
            return jsonify({"records": [], "error": str(exc)}), 502

    with data_lock:
        records_data = list(latest_records)
    return jsonify({"records": records_data})


@app.route("/stats")
def stats():
    """Resume los registros estructurados para la UI."""
    if SOURCE_MODE == "bridge":
        try:
            return jsonify(bridge_get_json("/stats"))
        except (urllib_error.URLError, json.JSONDecodeError) as exc:
            return jsonify({
                "total": 0,
                "max_records": MAX_RECORDS,
                "max_console_lines": MAX_CONSOLE_LINES,
                "serial_lines": 0,
                "serial_records": 0,
                "last_rx_time": None,
                "parity_ok": 0,
                "parity_error": 0,
                "by_label": {},
                "by_ssm": {},
                "latest_by_name": {},
                "running": False,
                "connected": False,
                "port": "spi-bridge",
                "source_mode": "bridge",
                "spi_errors": 1,
                "last_error": str(exc),
                "ack_events": 0,
                "filtered_words": 0,
                "overflow_events": 0,
                "fwd_resync_events": 0,
                "rev_resync_events": 0,
                "spi_drop_events": 0,
                "slot_evictions": 0,
                "snapshot_revision": 0,
                "last_update_counter": 0,
                "slot_count": 0,
                "filter_mode": 0,
                "filter_entries": [],
            }), 502

    with data_lock:
        records_data = list(latest_records)

    by_label = Counter()
    by_ssm = Counter()
    parity_ok = 0
    parity_error = 0
    latest_by_name = {}

    for record in records_data:
        label_key = f"{record['label']} ({record['name']})"
        by_label[label_key] += 1
        by_ssm[record["ssm_txt"]] += 1

        if record["parity"] == "OK":
            parity_ok += 1
        else:
            parity_error += 1

        latest_by_name[record["name"]] = {
            "timestamp": record["timestamp"],
            "label": record["label"],
            "value": record["value"],
            "unit": record["unit"],
            "ssm_txt": record["ssm_txt"],
            "parity": record["parity"],
        }

    return jsonify({
        "total": len(records_data),
        "max_records": MAX_RECORDS,
        "max_console_lines": MAX_CONSOLE_LINES,
        "serial_lines": state["serial_lines"],
        "serial_records": state["serial_records"],
        "last_rx_time": state["last_rx_time"],
        "parity_ok": parity_ok,
        "parity_error": parity_error,
        "by_label": dict(by_label),
        "by_ssm": dict(by_ssm),
        "latest_by_name": latest_by_name,
        "running": state["running"],
        "port": state["port"],
    })


def prometheus_escape(value: str) -> str:
    """Escapa strings para etiquetas Prometheus."""
    return str(value).replace("\\", "\\\\").replace('"', '\\"').replace("\n", " ")


@app.route("/metrics")
def metrics():
    """
    Endpoint Prometheus para Grafana.

    Expone contadores y ultimos valores filtrados por el sniffer. Prometheus
    scrapea este endpoint y Grafana grafica las series en el tiempo.
    """
    if SOURCE_MODE == "bridge":
        try:
            return Response(
                bridge_get_text("/metrics"),
                mimetype="text/plain; version=0.0.4; charset=utf-8",
            )
        except urllib_error.URLError as exc:
            return Response(
                f"# bridge_error 1\n# bridge_message {exc}\n",
                mimetype="text/plain; version=0.0.4; charset=utf-8",
                status=502,
            )

    with data_lock:
        records_data = list(latest_records)

    by_label = Counter()
    by_ssm = Counter()
    parity_ok = 0
    parity_error = 0
    latest_by_name = {}

    for record in records_data:
        label_key = (record["label"], record["name"])
        by_label[label_key] += 1
        by_ssm[record["ssm_txt"]] += 1

        if record["parity"] == "OK":
            parity_ok += 1
        else:
            parity_error += 1

        latest_by_name[record["name"]] = record

    lines = [
        "# HELP arinc_records_total Registros ARINC filtrados en memoria.",
        "# TYPE arinc_records_total gauge",
        f"arinc_records_total {len(records_data)}",
        "# HELP arinc_parity_ok_total Registros con paridad OK.",
        "# TYPE arinc_parity_ok_total gauge",
        f"arinc_parity_ok_total {parity_ok}",
        "# HELP arinc_parity_error_total Registros con error de paridad.",
        "# TYPE arinc_parity_error_total gauge",
        f"arinc_parity_error_total {parity_error}",
    ]

    lines.extend([
        "# HELP arinc_label_records_total Registros filtrados por label.",
        "# TYPE arinc_label_records_total gauge",
    ])
    for (label, name), count in by_label.items():
        lines.append(
            f'arinc_label_records_total{{label="{prometheus_escape(label)}",name="{prometheus_escape(name)}"}} {count}'
        )

    lines.extend([
        "# HELP arinc_ssm_records_total Registros filtrados por SSM.",
        "# TYPE arinc_ssm_records_total gauge",
    ])
    for ssm_txt, count in by_ssm.items():
        lines.append(
            f'arinc_ssm_records_total{{ssm="{prometheus_escape(ssm_txt)}"}} {count}'
        )

    lines.extend([
        "# HELP arinc_latest_value Ultimo valor decodificado por variable.",
        "# TYPE arinc_latest_value gauge",
    ])
    for name, record in latest_by_name.items():
        try:
            value = float(record["value"])
        except ValueError:
            continue

        lines.append(
            "arinc_latest_value"
            f'{{name="{prometheus_escape(name)}",'
            f'label="{prometheus_escape(record["label"])}",'
            f'unit="{prometheus_escape(record["unit"])}",'
            f'ssm="{prometheus_escape(record["ssm_txt"])}",'
            f'parity="{prometheus_escape(record["parity"])}"}} {value}'
        )

    return Response("\n".join(lines) + "\n", mimetype="text/plain; version=0.0.4; charset=utf-8")


@app.route("/ports")
def ports():
    """Permite refrescar la lista de puertos sin reiniciar la pagina."""
    return jsonify({
        "ports": list_serial_ports(),
        "source_mode": SOURCE_MODE,
        "bridge_url": BRIDGE_URL,
    })


@app.route("/filter_config", methods=["GET", "POST"])
def filter_config():
    if SOURCE_MODE != "bridge":
        return jsonify({"ok": False, "msg": "La configuracion remota del filtro solo existe en modo bridge"}), 400

    if request.method == "GET":
        try:
            payload = bridge_get_json("/filter")
            payload["entries_text"] = ", ".join(
                f"0x{entry['label']:02X}/{'ANY' if entry['sdi'] == 0xFF else entry['sdi']}"
                for entry in payload.get("entries", [])
            )
            return jsonify(payload)
        except (urllib_error.URLError, json.JSONDecodeError) as exc:
            return jsonify({"ok": False, "msg": str(exc)}), 502

    pass_all = request.form.get("pass_all") == "on"
    entries_text = request.form.get("filter_entries", "").strip()
    try:
        payload = bridge_post_json("/filter", {
            "pass_all": pass_all,
            "entries": parse_filter_entries_text(entries_text),
        })
        payload["ok"] = True
        payload["entries_text"] = ", ".join(
            f"0x{entry['label']:02X}/{'ANY' if entry['sdi'] == 0xFF else entry['sdi']}"
            for entry in payload.get("entries", [])
        )
        return jsonify(payload)
    except (ValueError, urllib_error.URLError, json.JSONDecodeError) as exc:
        return jsonify({"ok": False, "msg": str(exc)}), 400


@app.route("/bridge_reset", methods=["POST"])
def bridge_reset():
    if SOURCE_MODE != "bridge":
        return jsonify({"ok": False, "msg": "Este reset solo aplica en modo bridge"}), 400

    try:
        payload = bridge_post_json("/control/reset", {})
        return jsonify({"ok": True, "msg": "Contadores del bridge reiniciados", "payload": payload})
    except (urllib_error.URLError, json.JSONDecodeError) as exc:
        return jsonify({"ok": False, "msg": str(exc)}), 502


if __name__ == "__main__":
    with open(state["log_file"], "a", encoding="utf-8"):
        pass

    # Se desactiva el reloader para evitar dobles procesos y dobles hilos
    # de captura, que en Windows con serial suele traer comportamiento raro.
    app.run(
        debug=False,
        host=DASHBOARD_HOST,
        port=DASHBOARD_PORT,
        threaded=True,
        use_reloader=False,
    )
