import time
import threading
from collections import Counter, deque

from flask import Flask, jsonify, render_template, request
import serial
import serial.tools.list_ports

app = Flask(__name__)

latest_lines = deque(maxlen=400)
latest_frames = deque(maxlen=200)
latest_messages = deque(maxlen=120)
latest_warnings = deque(maxlen=120)
pending_messages = {}

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


def parse_structured_line(line: str):
    if not line.startswith("RX_"):
        return None

    parts = [part.strip() for part in line.split("|") if part.strip()]
    if not parts:
        return None

    fields = {}
    tags = []

    for part in parts[1:]:
        if "=" in part:
            key, value = part.split("=", 1)
            fields[key] = value
        else:
            tags.append(part)

    return {
        "event": parts[0],
        "tags": tags,
        "fields": fields,
        "raw": line,
        "timestamp": now_text(),
    }


def get_field(fields: dict, key: str, default=""):
    return fields.get(key, default)

def append_frame_record(event_data: dict):
    fields = event_data["fields"]
    record = {
        "timestamp": event_data["timestamp"],
        "event": event_data["event"],
        "type": get_field(fields, "TYPE"),
        "type_name": get_field(fields, "TYPE_NAME", "-"),
        "word": get_field(fields, "WORD", "-"),
        "parity": get_field(fields, "PARITY", "-"),
        "seq": get_field(fields, "SEQ", "-"),
        "frame": get_field(fields, "FRAME", "-"),
        "errors": get_field(fields, "ERRORS", ""),
        "raw": event_data["raw"],
    }

    latest_frames.append(record)


def append_warning_record(event_data: dict):
    fields = event_data["fields"]
    tags = event_data["tags"]
    warning_name = get_field(fields, "WARNING", tags[0] if tags else "UNKNOWN")

    record = {
        "timestamp": event_data["timestamp"],
        "warning": warning_name,
        "msg_id": get_field(fields, "MSG_ID", ""),
        "expected": get_field(fields, "EXPECTED", ""),
        "received": get_field(fields, "RECEIVED", ""),
        "dir": get_field(fields, "DIR", ""),
        "rt": get_field(fields, "RT", ""),
        "sa": get_field(fields, "SA", ""),
        "raw": event_data["raw"],
    }

    latest_warnings.append(record)

    msg_id = get_field(fields, "MSG_ID")
    if msg_id and msg_id in pending_messages:
        pending_messages[msg_id]["warnings"].append(warning_name)


def handle_message_start(event_data: dict):
    fields = event_data["fields"]
    msg_id = get_field(fields, "MSG_ID")
    if not msg_id:
        return

    pending_messages[msg_id] = {
        "msg_id": msg_id,
        "timestamp_start": event_data["timestamp"],
        "dir": get_field(fields, "DIR"),
        "rt": get_field(fields, "RT"),
        "sa": get_field(fields, "SA"),
        "broadcast": get_field(fields, "BROADCAST", "0"),
        "mode_code": get_field(fields, "MODE_CODE", "0"),
        "mode_value": get_field(fields, "MODE_VALUE", ""),
        "data_expected": get_field(fields, "DATA_EXPECTED", "0"),
        "data_seen_live": "0",
        "status_seen_live": "0",
        "warnings": [],
        "status": {},
    }


def handle_message_data(event_data: dict):
    fields = event_data["fields"]
    msg_id = get_field(fields, "MSG_ID")
    if not msg_id or msg_id not in pending_messages:
        return

    pending_messages[msg_id]["data_seen_live"] = get_field(fields, "INDEX", "0")


def handle_message_status(event_data: dict):
    fields = event_data["fields"]
    msg_id = get_field(fields, "MSG_ID")
    if not msg_id or msg_id not in pending_messages:
        return

    pending_messages[msg_id]["status_seen_live"] = "1"
    pending_messages[msg_id]["status"] = {
        "me": get_field(fields, "ME", "0"),
        "sr": get_field(fields, "SR", "0"),
        "busy": get_field(fields, "BUSY", "0"),
        "tf": get_field(fields, "TF", "0"),
        "bcr": get_field(fields, "BCR", "0"),
    }


def handle_message_end(event_data: dict):
    fields = event_data["fields"]
    msg_id = get_field(fields, "MSG_ID")
    if not msg_id:
        return

    pending = pending_messages.pop(msg_id, {
        "msg_id": msg_id,
        "timestamp_start": event_data["timestamp"],
        "dir": get_field(fields, "DIR"),
        "rt": get_field(fields, "RT"),
        "sa": get_field(fields, "SA"),
        "broadcast": get_field(fields, "BROADCAST", "0"),
        "mode_code": get_field(fields, "MODE_CODE", "0"),
        "mode_value": "",
        "data_expected": get_field(fields, "DATA_EXPECTED", "0"),
        "data_seen_live": get_field(fields, "DATA_SEEN", "0"),
        "status_seen_live": get_field(fields, "STATUS_SEEN", "0"),
        "warnings": [],
        "status": {},
    })

    message_record = {
        "msg_id": msg_id,
        "timestamp_start": pending["timestamp_start"],
        "timestamp_end": event_data["timestamp"],
        "dir": get_field(fields, "DIR", pending["dir"]),
        "rt": get_field(fields, "RT", pending["rt"]),
        "sa": get_field(fields, "SA", pending["sa"]),
        "broadcast": get_field(fields, "BROADCAST", pending["broadcast"]),
        "mode_code": get_field(fields, "MODE_CODE", pending["mode_code"]),
        "mode_value": pending.get("mode_value", ""),
        "data_seen": get_field(fields, "DATA_SEEN", pending["data_seen_live"]),
        "data_expected": get_field(fields, "DATA_EXPECTED", pending["data_expected"]),
        "status_seen": get_field(fields, "STATUS_SEEN", pending["status_seen_live"]),
        "result": get_field(fields, "RESULT", "UNKNOWN"),
        "warnings": pending["warnings"],
        "status": pending["status"],
    }

    latest_messages.append(message_record)


def process_serial_event(line: str):
    event_data = parse_structured_line(line)
    if not event_data:
        return

    with data_lock:
        event = event_data["event"]

        if event in {"RX_OK", "RX_ERR"}:
            append_frame_record(event_data)
        elif event == "RX_WARN":
            append_warning_record(event_data)
        elif event == "RX_MSG_START":
            handle_message_start(event_data)
        elif event == "RX_DATA":
            handle_message_data(event_data)
        elif event == "RX_STATUS":
            handle_message_status(event_data)
        elif event == "RX_MSG_END":
            handle_message_end(event_data)


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

                process_serial_event(line)

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
        latest_messages.clear()
        latest_warnings.clear()
        pending_messages.clear()

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
        latest_messages.clear()
        latest_warnings.clear()
        pending_messages.clear()

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


@app.route("/messages")
def messages():
    with data_lock:
        completed = list(latest_messages)
        pending = list(pending_messages.values())
    return jsonify({"messages": completed, "pending": pending})


@app.route("/warnings")
def warnings():
    with data_lock:
        data = list(latest_warnings)
    return jsonify({"warnings": data})


@app.route("/stats")
def stats():
    with data_lock:
        frames = list(latest_frames)
        messages = list(latest_messages)
        warnings_data = list(latest_warnings)
        pending_count = len(pending_messages)

    valid_frames = sum(1 for frame in frames if frame["event"] == "RX_OK")
    invalid_frames = sum(1 for frame in frames if frame["event"] == "RX_ERR")

    by_type = Counter()
    by_dir = Counter()
    by_result = Counter()

    for frame in frames:
        if frame["event"] == "RX_OK":
            by_type[frame["type_name"]] += 1

    for message in messages:
        by_dir[message["dir"]] += 1
        by_result[message["result"]] += 1

    latest_status = {}
    for message in reversed(messages):
        if message["status"]:
            latest_status = {
                "msg_id": message["msg_id"],
                "rt": message["rt"],
                "dir": message["dir"],
                "busy": message["status"].get("busy", "0"),
                "sr": message["status"].get("sr", "0"),
                "tf": message["status"].get("tf", "0"),
                "bcr": message["status"].get("bcr", "0"),
                "timestamp": message["timestamp_end"],
            }
            break

    return jsonify({
        "valid_frames": valid_frames,
        "invalid_frames": invalid_frames,
        "warnings": len(warnings_data),
        "completed_messages": len(messages),
        "pending_messages": pending_count,
        "by_type": dict(by_type),
        "by_dir": dict(by_dir),
        "by_result": dict(by_result),
        "latest_status": latest_status,
    })


@app.route("/ports")
def ports():
    return jsonify({"ports": list_serial_ports()})


if __name__ == "__main__":
    with open(state["log_file"], "a", encoding="utf-8"):
        pass

    app.run(debug=True, host="127.0.0.1", port=5000, threaded=True)
