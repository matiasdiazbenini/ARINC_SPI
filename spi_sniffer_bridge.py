import os
import threading
import time
from collections import Counter, deque

from flask import Flask, Response, jsonify, request

from sniffer_spi_protocol import (
    CMD_GET_FILTER,
    CMD_GET_STATS,
    CMD_POP_EVENT,
    CMD_RESET_STATS,
    CMD_SET_FILTER,
    FILTER_MODE_PASS_ALL,
    FILTER_MODE_WHITELIST,
    RESP_EVENT,
    RESP_FILTER,
    RESP_STATS,
    STATUS_EMPTY,
    STATUS_OK,
    ProtocolError,
    build_packet,
    pack_filter,
    parse_packet,
    unpack_event,
    unpack_filter,
    unpack_stats,
)

try:
    import spidev
except ImportError:  # pragma: no cover - esperado fuera de Raspberry Pi OS
    spidev = None


SPI_BUS = int(os.getenv("ARINC_SPI_BUS", "0"))
SPI_DEVICE = int(os.getenv("ARINC_SPI_DEVICE", "0"))
SPI_MAX_SPEED_HZ = int(os.getenv("ARINC_SPI_HZ", "200000"))
BRIDGE_PORT = int(os.getenv("ARINC_BRIDGE_PORT", "5100"))
POLL_INTERVAL_SEC = float(os.getenv("ARINC_SPI_POLL_SEC", "0.05"))
STATS_REFRESH_SEC = float(os.getenv("ARINC_SPI_STATS_SEC", "0.50"))
FILTER_REFRESH_SEC = float(os.getenv("ARINC_SPI_FILTER_SEC", "2.00"))
MAX_EVENTS_PER_CYCLE = int(os.getenv("ARINC_SPI_DRAIN_PER_LOOP", "32"))
MAX_RECORDS = int(os.getenv("ARINC_SPI_MAX_RECORDS", "12000"))

ACK_LABEL = 0xAC

app = Flask(__name__)
records = deque(maxlen=MAX_RECORDS)
data_lock = threading.Lock()
bridge_lock = threading.Lock()
bridge_stop = threading.Event()
bridge_thread = None

bridge_state = {
    "running": False,
    "connected": False,
    "source_mode": "spi_bridge",
    "port": f"spi{SPI_BUS}.{SPI_DEVICE}",
    "last_rx_time": None,
    "total_events": 0,
    "ack_events": 0,
    "spi_errors": 0,
    "last_error": "",
    "spi_drop_events": 0,
    "sniffer_stats": {
        "received_words": 0,
        "accepted_words": 0,
        "filtered_words": 0,
        "parity_errors": 0,
        "overflow_events": 0,
        "fwd_resync_events": 0,
        "rev_resync_events": 0,
    },
    "filter_config": {
        "mode": FILTER_MODE_WHITELIST,
        "count": 0,
        "entries": [],
    },
}


LABEL_NAMES = {
    0xA5: "TEMPERATURA",
    0xB1: "VELOCIDAD",
    0xC2: "ALTITUD",
    ACK_LABEL: "ACK_BATCH",
}

LABEL_UNITS = {
    0xA5: "C",
    0xB1: "kt",
    0xC2: "ft",
    ACK_LABEL: "batch",
}

SSM_TEXT = {
    3: "NORMAL",
    1: "NCD",
    2: "FUNCTIONAL_TEST",
    0: "FAILURE",
}

CHANNEL_TEXT = {
    0: "FWD",
    1: "REV",
}


class SpiSnifferClient:
    def __init__(self):
        self.spi = None
        self.sequence = 1

    def open(self):
        if spidev is None:
            raise RuntimeError("python3-spidev no esta instalado en la Raspberry Pi 3B+")

        if self.spi is not None:
            return

        spi = spidev.SpiDev()
        spi.open(SPI_BUS, SPI_DEVICE)
        spi.max_speed_hz = SPI_MAX_SPEED_HZ
        spi.mode = 0
        spi.bits_per_word = 8
        self.spi = spi

    def close(self):
        if self.spi is not None:
            self.spi.close()
            self.spi = None

    def _next_sequence(self):
        sequence = self.sequence & 0xFFFF
        self.sequence = (self.sequence + 1) & 0xFFFF
        return sequence

    def exchange(self, command: int, payload: bytes = b"") -> dict:
        if self.spi is None:
            self.open()

        request_packet = build_packet(command, payload, sequence=self._next_sequence())
        trailing_nop = build_packet(0x00, b"", sequence=self._next_sequence())

        self.spi.xfer2(list(request_packet))
        response_raw = bytes(self.spi.xfer2(list(trailing_nop)))
        return parse_packet(response_raw)

    def pop_event(self):
        response = self.exchange(CMD_POP_EVENT)
        if response["command"] != RESP_EVENT:
            raise ProtocolError(f"respuesta inesperada a POP_EVENT: 0x{response['command']:02X}")
        if response["status"] == STATUS_EMPTY:
            return None
        if response["status"] != STATUS_OK:
            raise ProtocolError(f"status inesperado en POP_EVENT: 0x{response['status']:02X}")
        return unpack_event(response["payload"])

    def get_stats(self):
        response = self.exchange(CMD_GET_STATS)
        if response["command"] != RESP_STATS or response["status"] != STATUS_OK:
            raise ProtocolError("GET_STATS devolvio una respuesta invalida")
        return unpack_stats(response["payload"])

    def get_filter(self):
        response = self.exchange(CMD_GET_FILTER)
        if response["command"] != RESP_FILTER or response["status"] != STATUS_OK:
            raise ProtocolError("GET_FILTER devolvio una respuesta invalida")
        return unpack_filter(response["payload"])

    def set_filter(self, *, pass_all: bool, entries: list[tuple[int, int]]):
        mode = FILTER_MODE_PASS_ALL if pass_all else FILTER_MODE_WHITELIST
        response = self.exchange(CMD_SET_FILTER, pack_filter(mode, entries))
        if response["command"] != RESP_FILTER or response["status"] != STATUS_OK:
            raise ProtocolError("SET_FILTER devolvio una respuesta invalida")
        return unpack_filter(response["payload"])

    def reset_stats(self):
        response = self.exchange(CMD_RESET_STATS)
        if response["command"] != RESP_STATS or response["status"] != STATUS_OK:
            raise ProtocolError("RESET_STATS devolvio una respuesta invalida")
        return unpack_stats(response["payload"])


client = SpiSnifferClient()


def label_name(label: int) -> str:
    return LABEL_NAMES.get(label, f"LABEL_0x{label:02X}")


def label_unit(label: int) -> str:
    return LABEL_UNITS.get(label, "raw")


def ssm_text(ssm: int) -> str:
    return SSM_TEXT.get(ssm, "DESCONOCIDO")


def format_filter_entries(entries: list[dict]) -> list[str]:
    formatted = []
    for entry in entries:
        sdi = entry["sdi"]
        sdi_text = "ANY" if sdi == 0xFF else str(sdi)
        formatted.append(f"0x{entry['label']:02X}/{sdi_text}")
    return formatted


def current_timestamp():
    return time.strftime("%H:%M:%S")


def record_from_event(event: dict) -> dict:
    label = event["label"]
    value = event["scaled_tenths"] / 10.0
    return {
        "label": f"0x{label:02X}",
        "name": label_name(label),
        "raw": str(event["raw_value"]),
        "value": f"{value:.1f}",
        "unit": label_unit(label),
        "sdi": str(event["sdi"]),
        "ssm": str(event["ssm"]),
        "ssm_txt": ssm_text(event["ssm"]),
        "parity": "OK" if event["parity_ok"] else "ERROR",
        "timestamp": current_timestamp(),
        "channel": CHANNEL_TEXT.get(event["channel"], "UNK"),
        "event_counter": event["event_counter"],
    }


def ingest_event(event: dict):
    record = record_from_event(event)

    with data_lock:
        records.append(record)
        bridge_state["last_rx_time"] = record["timestamp"]
        bridge_state["total_events"] += 1
        bridge_state["spi_drop_events"] = event["drop_counter"]
        if event["label"] == ACK_LABEL:
            bridge_state["ack_events"] += 1


def update_filter_state(filter_payload: dict):
    with data_lock:
        bridge_state["filter_config"] = filter_payload


def update_sniffer_stats(stats_payload: dict):
    with data_lock:
        bridge_state["sniffer_stats"] = stats_payload


def mark_bridge_error(exc: Exception):
    with data_lock:
        bridge_state["connected"] = False
        bridge_state["running"] = False
        bridge_state["spi_errors"] += 1
        bridge_state["last_error"] = str(exc)


def bridge_worker():
    last_stats_refresh = 0.0
    last_filter_refresh = 0.0

    while not bridge_stop.is_set():
        try:
            with bridge_lock:
                client.open()
            with data_lock:
                bridge_state["running"] = True
                bridge_state["connected"] = True
                bridge_state["last_error"] = ""

            drained = 0
            for _ in range(MAX_EVENTS_PER_CYCLE):
                with bridge_lock:
                    event = client.pop_event()
                if event is None:
                    break
                ingest_event(event)
                drained += 1

            now = time.time()
            if now - last_stats_refresh >= STATS_REFRESH_SEC:
                with bridge_lock:
                    update_sniffer_stats(client.get_stats())
                last_stats_refresh = now

            if now - last_filter_refresh >= FILTER_REFRESH_SEC:
                with bridge_lock:
                    update_filter_state(client.get_filter())
                last_filter_refresh = now

            if drained == 0:
                time.sleep(POLL_INTERVAL_SEC)
        except Exception as exc:  # pragma: no cover - depende del hardware SPI
            mark_bridge_error(exc)
            with bridge_lock:
                client.close()
            time.sleep(1.0)


def start_bridge_thread():
    global bridge_thread
    if bridge_thread is None:
        bridge_stop.clear()
        bridge_thread = threading.Thread(target=bridge_worker, daemon=True)
        bridge_thread.start()


def compute_dashboard_stats() -> dict:
    with data_lock:
        records_data = list(records)
        sniffer_stats = dict(bridge_state["sniffer_stats"])
        filter_config = dict(bridge_state["filter_config"])
        running = bridge_state["running"]
        port = bridge_state["port"]
        last_rx_time = bridge_state["last_rx_time"]
        total_events = bridge_state["total_events"]
        ack_events = bridge_state["ack_events"]
        spi_errors = bridge_state["spi_errors"]
        last_error = bridge_state["last_error"]
        spi_drop_events = bridge_state["spi_drop_events"]

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

    return {
        "total": len(records_data),
        "max_records": MAX_RECORDS,
        "max_console_lines": 0,
        "serial_lines": total_events,
        "serial_records": total_events,
        "last_rx_time": last_rx_time,
        "parity_ok": parity_ok,
        "parity_error": parity_error,
        "by_label": dict(by_label),
        "by_ssm": dict(by_ssm),
        "latest_by_name": latest_by_name,
        "running": running,
        "port": port,
        "source_mode": "spi_bridge",
        "ack_events": ack_events,
        "spi_errors": spi_errors,
        "last_error": last_error,
        "spi_drop_events": spi_drop_events,
        "received_words": sniffer_stats.get("received_words", 0),
        "accepted_words": sniffer_stats.get("accepted_words", 0),
        "filtered_words": sniffer_stats.get("filtered_words", 0),
        "parity_errors_sniffer": sniffer_stats.get("parity_errors", 0),
        "overflow_events": sniffer_stats.get("overflow_events", 0),
        "fwd_resync_events": sniffer_stats.get("fwd_resync_events", 0),
        "rev_resync_events": sniffer_stats.get("rev_resync_events", 0),
        "filter_mode": filter_config.get("mode", FILTER_MODE_WHITELIST),
        "filter_entries": format_filter_entries(filter_config.get("entries", [])),
    }


def prometheus_escape(value: str) -> str:
    return str(value).replace("\\", "\\\\").replace('"', '\\"').replace("\n", " ")


def parse_filter_request(payload: dict) -> tuple[bool, list[tuple[int, int]]]:
    pass_all = bool(payload.get("pass_all", False))
    entries = []

    for entry in payload.get("entries", []):
        label = entry.get("label")
        sdi = entry.get("sdi", 0xFF)

        if isinstance(label, str):
            label = int(label, 16) if label.lower().startswith("0x") else int(label)
        if isinstance(sdi, str):
            sdi = 0xFF if sdi.upper() == "ANY" else int(sdi)

        entries.append((int(label), int(sdi)))

    return pass_all, entries


@app.route("/")
def index():
    return jsonify({
        "service": "spi_sniffer_bridge",
        "mode": "spi_bridge",
        "spi_port": bridge_state["port"],
        "stats_url": "/stats",
        "metrics_url": "/metrics",
    })


@app.route("/health")
def health():
    with data_lock:
        return jsonify(dict(bridge_state))


@app.route("/records")
def records_endpoint():
    with data_lock:
        return jsonify({"records": list(records)})


@app.route("/stats")
def stats_endpoint():
    return jsonify(compute_dashboard_stats())


@app.route("/filter", methods=["GET", "POST"])
def filter_endpoint():
    if request.method == "GET":
        with data_lock:
            return jsonify(dict(bridge_state["filter_config"]))

    payload = request.get_json(force=True, silent=False)
    pass_all, entries = parse_filter_request(payload)
    with bridge_lock:
        filter_payload = client.set_filter(pass_all=pass_all, entries=entries)
    update_filter_state(filter_payload)
    return jsonify(filter_payload)


@app.route("/control/reset", methods=["POST"])
def reset_endpoint():
    with bridge_lock:
        stats_payload = client.reset_stats()
    update_sniffer_stats(stats_payload)
    with data_lock:
        records.clear()
        bridge_state["total_events"] = 0
        bridge_state["ack_events"] = 0
        bridge_state["spi_drop_events"] = 0
        bridge_state["last_rx_time"] = None
    return jsonify({"ok": True, "stats": stats_payload})


@app.route("/metrics")
def metrics():
    snapshot = compute_dashboard_stats()
    latest_by_name = snapshot["latest_by_name"]

    lines = [
        "# HELP arinc_records_total Registros ARINC filtrados en memoria del bridge SPI.",
        "# TYPE arinc_records_total gauge",
        f"arinc_records_total {snapshot['total']}",
        "# HELP arinc_ack_events_total ACK observados por el bridge SPI.",
        "# TYPE arinc_ack_events_total gauge",
        f"arinc_ack_events_total {snapshot['ack_events']}",
        "# HELP arinc_filtered_words_total Palabras descartadas por el filtro del sniffer.",
        "# TYPE arinc_filtered_words_total gauge",
        f"arinc_filtered_words_total {snapshot['filtered_words']}",
        "# HELP arinc_overflow_events_total Overflows de la cola de captura del sniffer.",
        "# TYPE arinc_overflow_events_total gauge",
        f"arinc_overflow_events_total {snapshot['overflow_events']}",
        "# HELP arinc_resync_events_total Resincronizaciones detectadas por canal.",
        "# TYPE arinc_resync_events_total gauge",
        f'arinc_resync_events_total{{channel="FWD"}} {snapshot["fwd_resync_events"]}',
        f'arinc_resync_events_total{{channel="REV"}} {snapshot["rev_resync_events"]}',
        "# HELP arinc_spi_drop_events_total Eventos SPI descartados por cola llena en el sniffer.",
        "# TYPE arinc_spi_drop_events_total gauge",
        f"arinc_spi_drop_events_total {snapshot['spi_drop_events']}",
        "# HELP arinc_parity_ok_total Registros con paridad OK.",
        "# TYPE arinc_parity_ok_total gauge",
        f"arinc_parity_ok_total {snapshot['parity_ok']}",
        "# HELP arinc_parity_error_total Registros con paridad ERROR.",
        "# TYPE arinc_parity_error_total gauge",
        f"arinc_parity_error_total {snapshot['parity_error']}",
        "# HELP arinc_label_records_total Registros por label dentro de la ventana activa.",
        "# TYPE arinc_label_records_total gauge",
    ]

    for label, count in snapshot["by_label"].items():
        lines.append(f'arinc_label_records_total{{label="{prometheus_escape(label)}"}} {count}')

    lines.extend([
        "# HELP arinc_latest_value Ultimo valor decodificado por variable.",
        "# TYPE arinc_latest_value gauge",
    ])

    for name, record in latest_by_name.items():
        try:
            value = float(record["value"])
        except (KeyError, ValueError):
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


if __name__ == "__main__":
    start_bridge_thread()
    app.run(debug=False, host="0.0.0.0", port=BRIDGE_PORT, threaded=True, use_reloader=False)
