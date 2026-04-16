import time
import threading
from collections import deque

from flask import Flask, jsonify, render_template, request
import serial
import serial.tools.list_ports

app = Flask(__name__)

latest_lines = deque(maxlen=400)
latest_frames = deque(maxlen=200)

data_lock = threading.Lock()
serial_thread = None
serial_stop = threading.Event()

state = {
    "running": False,
    "port": None,
    "baudrate": 115200,
    "filter_text": "",
    "log_file": "data_log.txt",
}


def now_text() -> str:
    return time.strftime("%H:%M:%S")


def append_line(text: str):
    with data_lock:
        latest_lines.append(f"[{now_text()}] {text}")


def list_serial_ports():
    return [p.device for p in serial.tools.list_ports.comports()]


def should_store(text: str, filter_text: str) -> bool:
    if not filter_text:
        return True

    terms = [term.strip().lower() for term in filter_text.split(",") if term.strip()]
    return any(term in text.lower() for term in terms)


def parse_sniffer_line(line: str):
    # Formato esperado del sniffer:
    # RX_OK|TYPE=CMD|WORD=0xA5A5|PARITY=OK
    # RX_ERR|TYPE=UNKNOWN|WORD=0x1234|PARITY=ERR
    parts = [part.strip() for part in line.split("|") if part.strip()]
    if len(parts) < 4:
        return None

    event = parts[0]
    if event not in {"RX_OK", "RX_ERR"}:
        return None

    fields = {}
    for part in parts[1:]:
        if "=" not in part:
            continue
        key, value = part.split("=", 1)
        fields[key.strip()] = value.strip()

    word = fields.get("WORD")
    parity = fields.get("PARITY")
    word_type = fields.get("TYPE")

    if not word or not parity or not word_type:
        return None

    word_type = word_type.upper()
    parity = parity.upper()
    if word_type not in {"CMD", "STS", "DATA", "UNKNOWN"}:
        word_type = "UNKNOWN"

    return {
        "timestamp": now_text(),
        "event": event,
        "type": word_type,
        "word": word,
        "parity": parity,
        "raw": line,
    }


def process_serial_line(line: str):
    frame = parse_sniffer_line(line)
    if not frame:
        return

    with data_lock:
        latest_frames.append(frame)


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

                process_serial_line(line)

                visible = (
                    line.startswith("[INFO]")
                    or line.startswith("[ERROR]")
                    or should_store(line, filter_text)
                )
                if visible:
                    append_line(line)

                if should_store(line, filter_text):
                    with open(state["log_file"], "a", encoding="utf-8") as f:
                        f.write(line + "\n")

    except Exception as exc:
        append_line(f"[ERROR] No se pudo abrir/leer {port}: {exc}")
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

    with data_lock:
        latest_lines.clear()
        latest_frames.clear()

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
    with data_lock:
        latest_lines.clear()
        latest_frames.clear()

    append_line("[INFO] Consola limpiada")
    return jsonify({"ok": True})


@app.route("/lines")
def lines():
    with data_lock:
        text = "\n".join(latest_lines)
    return jsonify({"text": text})


@app.route("/frames")
def frames():
    with data_lock:
        data = list(latest_frames)
    return jsonify({"frames": data})


@app.route("/stats")
def stats():
    with data_lock:
        frames_data = list(latest_frames)

    valid_frames = sum(1 for frame in frames_data if frame["event"] == "RX_OK")
    invalid_frames = sum(1 for frame in frames_data if frame["event"] == "RX_ERR")
    by_type = {
        "CMD": 0,
        "STS": 0,
        "DATA": 0,
        "UNKNOWN": 0,
    }

    for frame in frames_data:
        frame_type = frame.get("type", "UNKNOWN")
        if frame_type in by_type:
            by_type[frame_type] += 1
        else:
            by_type["UNKNOWN"] += 1

    return jsonify({
        "valid_frames": valid_frames,
        "invalid_frames": invalid_frames,
        "by_type": by_type,
    })


@app.route("/ports")
def ports():
    return jsonify({"ports": list_serial_ports()})


if __name__ == "__main__":
    with open(state["log_file"], "a", encoding="utf-8"):
        pass

    app.run(debug=True, host="127.0.0.1", port=5000, threaded=True)
