import os
import time
import threading
from collections import deque

from flask import Flask, Response, render_template, request, jsonify
import serial
import serial.tools.list_ports

app = Flask(__name__)

# Buffer en memoria para mostrar ultimas lineas
latest_lines = deque(maxlen=200)

# Estado global simple
state = {
    "running": False,
    "port": None,
    "baudrate": 115200,
    "filter_text": "",
    "log_file": "data_log.txt",
}

serial_thread = None
serial_stop = threading.Event()


def list_serial_ports():
    """Devuelve una lista de puertos serie disponibles."""
    return [p.device for p in serial.tools.list_ports.comports()]


def append_line(line: str):
    """Agrega linea al buffer en memoria."""
    latest_lines.append(line)


def should_store(line: str, filter_text: str) -> bool:
    if not filter_text:
        return True
    return filter_text.lower() in line.lower()


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

                timestamp = time.strftime("%H:%M:%S")
                formatted = f"[{timestamp}] {line}"

                if line.startswith("[INFO]") or line.startswith("[ERROR]") or should_store(formatted, filter_text):
                    append_line(formatted)

                if should_store(formatted, filter_text):
                    with open(state["log_file"], "a", encoding="utf-8") as f:
                        f.write(formatted + "\n")

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
        running=state["running"],
        selected_port=state["port"],
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
    if state["running"]:
        serial_stop.set()
        time.sleep(0.3)
    return jsonify({"ok": True, "msg": "Captura detenida"})

@app.route("/lines")
def lines():
    return jsonify({"text": "\n".join(latest_lines)})

#@app.route("/stream")
#def stream():
    def generate():
        last_payload = ""
        while True:
            payload = "\n".join(latest_lines)

            if payload != last_payload:
                sse_message = ""
                for line in payload.splitlines():
                    sse_message += f"data: {line}\n"
                sse_message += "\n"

                yield sse_message
                last_payload = payload

            time.sleep(0.2)

    return Response(generate(), mimetype="text/event-stream")


@app.route("/clear", methods=["POST"])
def clear():
    latest_lines.clear()
    append_line("[INFO] Consola limpiada")
    return jsonify({"ok": True})


@app.route("/ports")
def ports():
    return jsonify({"ports": list_serial_ports()})


if __name__ == "__main__":
    if not os.path.exists(state["log_file"]):
        with open(state["log_file"], "w", encoding="utf-8") as f:
            f.write("=== Captura serial ===\n")

    app.run(debug=True, host="127.0.0.1", port=5000)