import re
import time
import threading
from collections import deque

from flask import Flask, jsonify, render_template, request
import serial
import serial.tools.list_ports

app = Flask(__name__)

latest_lines = deque(maxlen=300)
latest_records = deque(maxlen=100)

serial_thread = None
serial_stop = threading.Event()

state = {
    "running": False,
    "port": None,
    "baudrate": 115200,
    "filter_text": "",
    "log_file": "data_log.txt",
}


def append_line(text: str):
    timestamp = time.strftime("%H:%M:%S")
    latest_lines.append(f"[{timestamp}] {text}")


def list_serial_ports():
    return [p.device for p in serial.tools.list_ports.comports()]


def should_store(line: str, filter_text: str) -> bool:
    if not filter_text:
        return True
    return filter_text.lower() in line.lower()


def parse_match_line(line: str):
    """
    Parsea lineas del slave tipo:
    MATCH -> LABEL: 0xA5 (TEMPERATURA) | RAW: 280 | VAL: 20.0 C | SDI: 0 | SSM: 3 | SSM_TXT: NORMAL | PARITY: OK
    """
    pattern = (
        r"LABEL:\s*(0x[0-9A-Fa-f]+)\s*\(([^)]+)\)\s*\|\s*"
        r"RAW:\s*([0-9]+)\s*\|\s*"
        r"VAL:\s*([\-0-9.]+)\s*([A-Za-z]+)\s*\|\s*"
        r"SDI:\s*([0-9]+)\s*\|\s*"
        r"SSM:\s*([0-9]+)\s*\|\s*"
        r"SSM_TXT:\s*([A-Z_]+)\s*\|\s*"
        r"PARITY:\s*(OK|ERROR)"
    )

    m = re.search(pattern, line)
    if not m:
        return None

    return {
        "label": m.group(1),
        "name": m.group(2),
        "raw": m.group(3),
        "value": m.group(4),
        "unit": m.group(5),
        "sdi": m.group(6),
        "ssm": m.group(7),
        "ssm_txt": m.group(8),
        "parity": m.group(9),
    }


def serial_worker(port: str, baudrate: int, filter_text: str):
    try:
        append_line(f"[INFO] Intentando abrir {port} @ {baudrate}")

        with serial.Serial(port, baudrate=baudrate, timeout=1) as ser:
            append_line(f"[INFO] Conectado a {port} @ {baudrate}")

            while not serial_stop.is_set():
                raw = ser.readline()
                if not raw:
                    continue

                line = raw.decode("utf-8", errors="replace").strip()
                if not line:
                    continue

                formatted = line

                visible = (
                    line.startswith("[INFO]")
                    or line.startswith("[ERROR]")
                    or should_store(formatted, filter_text)
                )

                if visible:
                    append_line(formatted)

                if should_store(formatted, filter_text):
                    with open(state["log_file"], "a", encoding="utf-8") as f:
                        f.write(formatted + "\n")

                if line.startswith("MATCH ->"):
                    record = parse_match_line(line)
                    if record and should_store(line, filter_text):
                        record["timestamp"] = time.strftime("%H:%M:%S")
                        latest_records.append(record)

    except Exception as e:
        append_line(f"[ERROR] No se pudo abrir/leer {port}: {e}")
    finally:
        state["running"] = False
        append_line("[INFO] Lectura serial finalizada")


@app.route("/")
def index():
    return render_template(
        "index.html",
        ports=list_serial_ports(),
        baudrate=state["baudrate"],
        filter_text=state["filter_text"],
    )


@app.route("/start", methods=["POST"])
def start():
    global serial_thread

    if state["running"]:
        return jsonify({"ok": False, "msg": "Ya esta corriendo"}), 400

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

    latest_lines.clear()
    latest_records.clear()
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
    serial_stop.set()
    append_line("[INFO] Deteniendo captura...")
    return jsonify({"ok": True, "msg": "Captura detenida"})


@app.route("/clear", methods=["POST"])
def clear():
    latest_lines.clear()
    latest_records.clear()
    append_line("[INFO] Consola limpiada")
    return jsonify({"ok": True})


@app.route("/lines")
def lines():
    return jsonify({"text": "\n".join(latest_lines)})


@app.route("/records")
def records():
    return jsonify({"records": list(latest_records)})


@app.route("/ports")
def ports():
    return jsonify({"ports": list_serial_ports()})


if __name__ == "__main__":
    with open(state["log_file"], "a", encoding="utf-8") as f:
        pass

    app.run(debug=True, host="127.0.0.1", port=5000, threaded=True)