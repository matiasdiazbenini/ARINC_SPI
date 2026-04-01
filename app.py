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
    """Decide si una linea se guarda en archivo."""
    if not filter_text:
        return True
    return filter_text.lower() in line.lower()


def serial_worker(port: str, baudrate: int, filter_text: str):
    """
    Hilo que lee el puerto serie y:
    - publica lineas en memoria
    - guarda coincidencias en archivo
    """
    try:
        with serial.Serial(port, baudrate=baudrate, timeout=1) as ser:
            append_line(f"[INFO] Conectado a {port} @ {baudrate}")

            while not serial_stop.is_set():
                raw = ser.readline()
                if not raw:
                    continue

                try:
                    line = raw.decode("utf-8", errors="replace").strip()
                except Exception:
                    line = str(raw)

                if not line:
                    continue

                timestamp = time.strftime("%H:%M:%S")
                formatted = f"[{timestamp}] {line}"
                append_line(formatted)

                if should_store(formatted, filter_text):
                    with open(state["log_file"], "a", encoding="utf-8") as f:
                        f.write(formatted + "\n")

    except Exception as e:
        append_line(f"[ERROR] {e}")
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

    # Reinicia estado
    serial_stop.clear()
    state["running"] = True
    state["port"] = port
    state["baudrate"] = baudrate
    state["filter_text"] = filter_text

    # Limpia buffer visual
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


@app.route("/stream")
def stream():
    """
    Endpoint SSE (Server-Sent Events).
    El navegador se queda escuchando y va recibiendo nuevas lineas.
    """
    def generate():
        last_len = 0
        while True:
            current = list(latest_lines)
            if len(current) != last_len:
                payload = "\n".join(current)
                yield f"data: {payload}\n\n"
                last_len = len(current)
            time.sleep(0.5)

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