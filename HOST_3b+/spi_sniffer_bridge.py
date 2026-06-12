import os
import shutil
import subprocess
import threading
import time
from collections import Counter, deque

from flask import Flask, Response, jsonify, request

from sniffer_spi_protocol import (
    CMD_GET_FILTER,
    CMD_GET_LATEST_META,
    CMD_GET_LATEST_SLOT,
    CMD_GET_STATS,
    CMD_POP_EVENT,
    CMD_RESET_STATS,
    CMD_SET_FILTER,
    FILTER_MODE_PASS_ALL,
    FILTER_MODE_WHITELIST,
    RESP_EVENT,
    RESP_FILTER,
    RESP_LATEST_META,
    RESP_LATEST_SLOT,
    RESP_STATS,
    RESP_ACK,
    SNIFFER_SPI_PACKET_SIZE,
    SNIFFER_SPI_TRANSPORT_IDLE,
    SNIFFER_SPI_TRANSPORT_RESET,
    STATUS_BAD_CRC,
    STATUS_BAD_MAGIC,
    STATUS_EMPTY,
    STATUS_OK,
    ProtocolError,
    build_packet,
    pack_filter,
    parse_packet,
    unpack_event,
    unpack_filter,
    unpack_latest_meta,
    unpack_latest_slot,
    unpack_stats,
)

try:
    import spidev
except ImportError:  # pragma: no cover - esperado fuera de Raspberry Pi OS
    spidev = None


def env_flag(name: str, default: bool) -> bool:
    value = os.getenv(name)
    if value is None:
        return default
    return value.strip().lower() not in ("0", "false", "no", "off")


def parse_packet_with_resync(raw: bytes) -> dict:
    try:
        return parse_packet(raw[:SNIFFER_SPI_PACKET_SIZE])
    except ProtocolError:
        pass

    magic = bytes((0xA4, 0x29))
    start = raw.find(magic, 1)
    while start >= 0:
        end = start + SNIFFER_SPI_PACKET_SIZE
        if end <= len(raw):
            try:
                return parse_packet(raw[start:end])
            except ProtocolError:
                pass
        start = raw.find(magic, start + 1)

    raise ProtocolError("magic SPI invalido")


SPI_MANUAL_CS = env_flag("ARINC_SPI_MANUAL_CS", False)
SPI_BUS = int(os.getenv("ARINC_SPI_BUS", "0"))
SPI_DEVICE = int(os.getenv("ARINC_SPI_DEVICE", "0"))
# Perfil recomendado para uso continuo:
# - suficientemente rapido para que el dashboard se vea fluido
# - bastante mas estable en pruebas largas que el perfil de estres maximo
SPI_MAX_SPEED_HZ = int(os.getenv("ARINC_SPI_HZ", "800000"))
SPI_CS_GPIO = int(os.getenv("ARINC_SPI_CS_GPIO", "8"))
SPI_CS_SETUP_US = int(os.getenv("ARINC_SPI_CS_SETUP_US", "150"))
SPI_CS_HOLD_US = int(os.getenv("ARINC_SPI_CS_HOLD_US", "150"))
SPI_BYTE_DELAY_US = int(os.getenv("ARINC_SPI_BYTE_DELAY_US", "25"))
SPI_TRANSFER_MODE = os.getenv("ARINC_SPI_TRANSFER_MODE", "byte").strip().lower()
if SPI_TRANSFER_MODE not in ("byte", "frame", "pio-frame", "auto"):
    SPI_TRANSFER_MODE = "byte"
SPI_RECOVERY_GAP_SEC = float(os.getenv("ARINC_SPI_RECOVERY_GAP_SEC", "0.012"))
BRIDGE_HOST = os.getenv("ARINC_BRIDGE_HOST", "127.0.0.1").strip() or "127.0.0.1"
BRIDGE_PORT = int(os.getenv("ARINC_BRIDGE_PORT", "5100"))
POLL_INTERVAL_SEC = float(os.getenv("ARINC_SPI_POLL_SEC", "0.006"))
STATS_REFRESH_SEC = float(os.getenv("ARINC_SPI_STATS_SEC", "0.06"))
FILTER_REFRESH_SEC = float(os.getenv("ARINC_SPI_FILTER_SEC", "2.00"))
MAX_EVENTS_PER_CYCLE = int(os.getenv("ARINC_SPI_DRAIN_PER_LOOP", "32"))
MAX_RECORDS = int(os.getenv("ARINC_SPI_MAX_RECORDS", "500"))
SPI_RESPONSE_DELAY_SEC = float(os.getenv("ARINC_SPI_RESPONSE_DELAY_SEC", "0.00005"))
SPI_FRAME_RESPONSE_DELAY_SEC = float(os.getenv("ARINC_SPI_FRAME_RESPONSE_DELAY_SEC", "0.002"))
SPI_RESPONSE_RETRIES = int(os.getenv("ARINC_SPI_RESPONSE_RETRIES", "4"))
SPI_RESET_BURST = int(os.getenv("ARINC_SPI_RESET_BURST", "3"))
SPI_RESPONSE_WINDOW_BYTES = int(os.getenv("ARINC_SPI_RESPONSE_WINDOW_BYTES", "48"))

ACK_LABEL = 0xAC
ACK_SDI = 0x03

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
    "port": f"spi{SPI_BUS}.{SPI_DEVICE}" + (f"+gpio{SPI_CS_GPIO}" if SPI_MANUAL_CS else ""),
    "spi_requested_hz": SPI_MAX_SPEED_HZ,
    "spi_manual_cs": SPI_MANUAL_CS,
    "spi_cs_gpio": SPI_CS_GPIO if SPI_MANUAL_CS else None,
    "spi_cs_setup_us": SPI_CS_SETUP_US if SPI_MANUAL_CS else 0,
    "spi_cs_hold_us": SPI_CS_HOLD_US if SPI_MANUAL_CS else 0,
    "spi_transfer_mode": "frame" if SPI_TRANSFER_MODE == "auto" else SPI_TRANSFER_MODE,
    "spi_requested_transfer_mode": SPI_TRANSFER_MODE,
    "last_rx_time": None,
    "total_events": 0,
    "ack_events": 0,
    "spi_errors": 0,
    "spi_startup_errors": 0,
    "spi_error_armed": False,
    "last_error": "",
    "spi_drop_events": 0,
    "slot_evictions": 0,
    "snapshot_revision": 0,
    "last_update_counter": 0,
    "slot_count": 0,
    "fwd_startup_resync_events": 0,
    "fwd_operational_resync_events": 0,
    "latest_slots": [],
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


class ManualCsController:
    def __init__(self, gpio: int):
        self.gpio = gpio
        self.backend = self._detect_backend()
        self._run_set("op", "dh")

    @staticmethod
    def _detect_backend() -> str:
        for candidate in ("pinctrl", "raspi-gpio"):
            if shutil.which(candidate):
                return candidate
        raise RuntimeError("No se encontro pinctrl ni raspi-gpio para manejar CS manual en la Raspberry Pi.")

    def _run_set(self, mode: str, level: str) -> None:
        subprocess.run([self.backend, "set", str(self.gpio), mode, level], check=True)

    def low(self) -> None:
        self._run_set("op", "dl")

    def high(self) -> None:
        self._run_set("op", "dh")


class SpiSnifferClient:
    def __init__(self):
        self.spi = None
        self.sequence = 1
        self.cs = None
        self.active_transfer_mode = "frame" if SPI_TRANSFER_MODE == "auto" else SPI_TRANSFER_MODE

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
        if SPI_MANUAL_CS:
            spi.no_cs = True
            self.cs = ManualCsController(SPI_CS_GPIO)
        self.spi = spi

    def close(self):
        if self.spi is not None:
            self.spi.close()
            self.spi = None
        self.cs = None

    def _next_sequence(self):
        sequence = self.sequence & 0xFFFF
        self.sequence = (self.sequence + 1) & 0xFFFF
        return sequence

    def _xfer_byte(self, value: int) -> int:
        if self.cs is None:
            response = self.spi.xfer2([value & 0xFF])[0]
        else:
            self.cs.low()
            if SPI_CS_SETUP_US > 0:
                time.sleep(SPI_CS_SETUP_US / 1_000_000.0)
            try:
                response = self.spi.xfer2([value & 0xFF])[0]
            finally:
                if SPI_CS_HOLD_US > 0:
                    time.sleep(SPI_CS_HOLD_US / 1_000_000.0)
                self.cs.high()

        if SPI_BYTE_DELAY_US > 0:
            time.sleep(SPI_BYTE_DELAY_US / 1_000_000.0)
        return response

    def _xfer_frame(self, frame) -> bytes:
        values = [byte & 0xFF for byte in frame]
        if self.cs is None:
            response = bytes(self.spi.xfer2(values))
        else:
            self.cs.low()
            if SPI_CS_SETUP_US > 0:
                time.sleep(SPI_CS_SETUP_US / 1_000_000.0)
            try:
                response = bytes(self.spi.xfer2(values))
            finally:
                if SPI_CS_HOLD_US > 0:
                    time.sleep(SPI_CS_HOLD_US / 1_000_000.0)
                self.cs.high()

        if SPI_BYTE_DELAY_US > 0:
            time.sleep(SPI_BYTE_DELAY_US / 1_000_000.0)
        return response

    def _transport_reset(self) -> int:
        return self._xfer_byte(SNIFFER_SPI_TRANSPORT_RESET)

    def _recovery_pause(self) -> None:
        if SPI_RECOVERY_GAP_SEC > 0.0:
            time.sleep(SPI_RECOVERY_GAP_SEC)

    def _transport_send_request(self, packet: bytes) -> None:
        for byte in packet:
            self._xfer_byte(byte)

    def _transport_read_response(self) -> bytes:
        return bytes(
            self._xfer_byte(SNIFFER_SPI_TRANSPORT_IDLE)
            for _ in range(max(SPI_RESPONSE_WINDOW_BYTES, SNIFFER_SPI_PACKET_SIZE))
        )

    def _transport_exchange_frame(self, request_packet: bytes) -> bytes:
        for _reset_index in range(max(SPI_RESET_BURST, 1)):
            self._transport_reset()
        self._xfer_frame(request_packet)

        response_delay = max(SPI_RESPONSE_DELAY_SEC, SPI_FRAME_RESPONSE_DELAY_SEC)
        if response_delay > 0.0:
            time.sleep(response_delay)

        return self._xfer_frame(
            bytes([SNIFFER_SPI_TRANSPORT_IDLE] * max(SPI_RESPONSE_WINDOW_BYTES, SNIFFER_SPI_PACKET_SIZE))
        )

    def _transport_exchange_pio_frame(self, request_packet: bytes) -> bytes:
        self._xfer_frame(request_packet)

        response_delay = max(SPI_RESPONSE_DELAY_SEC, SPI_FRAME_RESPONSE_DELAY_SEC)
        if response_delay > 0.0:
            time.sleep(response_delay)

        return self._xfer_frame(
            bytes([SNIFFER_SPI_TRANSPORT_IDLE] * SNIFFER_SPI_PACKET_SIZE)
        )

    def _transport_exchange_bytewise(self, request_packet: bytes) -> bytes:
        for _reset_index in range(max(SPI_RESET_BURST, 1)):
            self._transport_reset()
        self._transport_send_request(request_packet)

        if SPI_RESPONSE_DELAY_SEC > 0.0:
            time.sleep(SPI_RESPONSE_DELAY_SEC)

        return self._transport_read_response()

    def _exchange_once(self, request_packet: bytes, transfer_mode: str) -> dict:
        if transfer_mode == "frame":
            response_raw = self._transport_exchange_frame(request_packet)
        elif transfer_mode == "pio-frame":
            response_raw = self._transport_exchange_pio_frame(request_packet)
        else:
            response_raw = self._transport_exchange_bytewise(request_packet)

        response = parse_packet_with_resync(response_raw)
        if response["command"] == RESP_ACK and response["status"] in (STATUS_BAD_MAGIC, STATUS_BAD_CRC):
            raise ProtocolError(
                f"sniffer rechazo request SPI: status=0x{response['status']:02X}"
            )
        return response

    def _candidate_modes(self) -> list[str]:
        if SPI_TRANSFER_MODE == "auto":
            if self.active_transfer_mode == "byte":
                return ["byte"]
            return ["frame", "byte"]
        return [SPI_TRANSFER_MODE]

    def exchange(self, command: int, payload: bytes = b"") -> dict:
        if self.spi is None:
            self.open()

        request_packet = build_packet(command, payload, sequence=self._next_sequence())
        last_exc = None

        for _ in range(max(SPI_RESPONSE_RETRIES, 1)):
            for transfer_mode in self._candidate_modes():
                if last_exc is not None:
                    self._recovery_pause()
                try:
                    response = self._exchange_once(request_packet, transfer_mode)
                    self.active_transfer_mode = transfer_mode
                    with data_lock:
                        bridge_state["spi_transfer_mode"] = transfer_mode
                    return response
                except ProtocolError as exc:
                    last_exc = exc

        raise last_exc or ProtocolError("no se pudo sincronizar la respuesta SPI del sniffer")

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

    def get_latest_meta(self):
        response = self.exchange(CMD_GET_LATEST_META)
        if response["command"] != RESP_LATEST_META or response["status"] != STATUS_OK:
            raise ProtocolError("GET_LATEST_META devolvio una respuesta invalida")
        return unpack_latest_meta(response["payload"])

    def get_latest_slot(self, slot_index: int):
        response = self.exchange(CMD_GET_LATEST_SLOT, bytes((slot_index & 0xFF,)))
        if response["command"] != RESP_LATEST_SLOT or response["status"] != STATUS_OK:
            raise ProtocolError("GET_LATEST_SLOT devolvio una respuesta invalida")
        return unpack_latest_slot(response["payload"])

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


def is_ack_slot(label: int, sdi: int) -> bool:
    return label == ACK_LABEL and sdi == ACK_SDI


def slot_name(label: int, sdi: int) -> str:
    if is_ack_slot(label, sdi):
        return "ACK_BATCH"
    if label == ACK_LABEL:
        return f"LABEL_0x{label:02X}"
    return label_name(label)


def slot_unit(label: int, sdi: int) -> str:
    if is_ack_slot(label, sdi):
        return "batch"
    if label == ACK_LABEL:
        return "raw"
    return label_unit(label)


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


def record_from_slot(slot: dict) -> dict:
    label = slot["label"]
    sdi = slot["sdi"]
    value = slot["scaled_tenths"] / 10.0
    return {
        "label": f"0x{label:02X}",
        "name": slot_name(label, sdi),
        "raw": str(slot["raw_value"]),
        "value": f"{value:.1f}",
        "unit": slot_unit(label, sdi),
        "sdi": str(sdi),
        "ssm": str(slot["ssm"]),
        "ssm_txt": ssm_text(slot["ssm"]),
        "parity": "OK" if slot["parity_ok"] else "ERROR",
        "timestamp": current_timestamp(),
        "channel": CHANNEL_TEXT.get(slot["channel"], "UNK"),
        "update_counter": slot["update_counter"],
        "hit_count": slot["hit_count"],
    }


def update_snapshot_state(meta_payload: dict, slots_payload: list[dict]):
    with data_lock:
        previous = {
            (slot["channel"], slot["label"], slot["sdi"]): slot["update_counter"]
            for slot in bridge_state["latest_slots"]
        }

        bridge_state["snapshot_revision"] = meta_payload["snapshot_revision"]
        bridge_state["slot_evictions"] = meta_payload["slot_evictions"]
        bridge_state["last_update_counter"] = meta_payload["last_update_counter"]
        bridge_state["slot_count"] = meta_payload["slot_count"]
        bridge_state["fwd_startup_resync_events"] = meta_payload["fwd_startup_resync_events"]
        bridge_state["fwd_operational_resync_events"] = meta_payload["fwd_operational_resync_events"]
        bridge_state["latest_slots"] = slots_payload
        bridge_state["connected"] = True
        bridge_state["last_error"] = ""

        changed = 0
        ack_hits = 0
        for slot in slots_payload:
            if not slot.get("valid"):
                continue
            key = (slot["channel"], slot["label"], slot["sdi"])
            if previous.get(key) != slot["update_counter"]:
                records.append(record_from_slot(slot))
                changed += 1
            if is_ack_slot(slot["label"], slot["sdi"]):
                ack_hits += slot["hit_count"]

        if changed > 0 or meta_payload["snapshot_revision"] > 0:
            bridge_state["last_rx_time"] = current_timestamp()
        bridge_state["ack_events"] = ack_hits


def update_filter_state(filter_payload: dict):
    with data_lock:
        bridge_state["filter_config"] = filter_payload


def update_sniffer_stats(stats_payload: dict):
    with data_lock:
        bridge_state["sniffer_stats"] = stats_payload


def reset_bridge_session_state(stats_payload: dict):
    with data_lock:
        records.clear()
        bridge_state["sniffer_stats"] = stats_payload
        bridge_state["total_events"] = 0
        bridge_state["ack_events"] = 0
        bridge_state["spi_errors"] = 0
        bridge_state["spi_startup_errors"] = 0
        bridge_state["spi_error_armed"] = True
        bridge_state["last_error"] = ""
        bridge_state["spi_drop_events"] = 0
        bridge_state["last_rx_time"] = None
        bridge_state["latest_slots"] = []
        bridge_state["snapshot_revision"] = 0
        bridge_state["last_update_counter"] = 0
        bridge_state["slot_count"] = 0
        bridge_state["slot_evictions"] = 0
        bridge_state["fwd_startup_resync_events"] = 0
        bridge_state["fwd_operational_resync_events"] = 0


def mark_bridge_ok():
    with data_lock:
        bridge_state["running"] = True
        bridge_state["connected"] = True
        bridge_state["spi_error_armed"] = True
        bridge_state["last_error"] = ""


def mark_bridge_running():
    with data_lock:
        bridge_state["running"] = True


def mark_bridge_error(exc: Exception):
    with data_lock:
        bridge_state["connected"] = False
        bridge_state["running"] = True
        if bridge_state["spi_error_armed"]:
            bridge_state["spi_errors"] += 1
        else:
            bridge_state["spi_startup_errors"] += 1
        bridge_state["last_error"] = str(exc)


def bridge_worker():
    last_stats_refresh = 0.0
    last_filter_refresh = 0.0
    last_meta_refresh = 0.0
    known_revision = -1

    mark_bridge_running()
    while not bridge_stop.is_set():
        try:
            with bridge_lock:
                client.open()

            now = time.time()
            if now - last_stats_refresh >= STATS_REFRESH_SEC:
                with bridge_lock:
                    stats_payload = client.get_stats()
                update_sniffer_stats(stats_payload)
                mark_bridge_ok()
                last_stats_refresh = now

            if now - last_meta_refresh >= POLL_INTERVAL_SEC:
                with bridge_lock:
                    meta_payload = client.get_latest_meta()
                mark_bridge_ok()
                if meta_payload["snapshot_revision"] != known_revision:
                    slots_payload = []
                    for slot_index in range(meta_payload["slot_count"]):
                        with bridge_lock:
                            slot_payload = client.get_latest_slot(slot_index)
                        slots_payload.append(slot_payload)
                    update_snapshot_state(meta_payload, slots_payload)
                    known_revision = meta_payload["snapshot_revision"]
                else:
                    with data_lock:
                        bridge_state["snapshot_revision"] = meta_payload["snapshot_revision"]
                        bridge_state["slot_evictions"] = meta_payload["slot_evictions"]
                        bridge_state["last_update_counter"] = meta_payload["last_update_counter"]
                        bridge_state["slot_count"] = meta_payload["slot_count"]
                        bridge_state["fwd_startup_resync_events"] = meta_payload["fwd_startup_resync_events"]
                        bridge_state["fwd_operational_resync_events"] = meta_payload["fwd_operational_resync_events"]
                        bridge_state["connected"] = True
                last_meta_refresh = now

            if now - last_filter_refresh >= FILTER_REFRESH_SEC:
                with bridge_lock:
                    filter_payload = client.get_filter()
                update_filter_state(filter_payload)
                mark_bridge_ok()
                last_filter_refresh = now

            time.sleep(POLL_INTERVAL_SEC)
        except Exception as exc:  # pragma: no cover - depende del hardware SPI
            mark_bridge_error(exc)
            with bridge_lock:
                client.close()
            time.sleep(1.0)

    with data_lock:
        bridge_state["running"] = False


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
        connected = bridge_state["connected"]
        latest_slots = list(bridge_state["latest_slots"])
        running = bridge_state["running"]
        port = bridge_state["port"]
        spi_requested_hz = bridge_state["spi_requested_hz"]
        spi_transfer_mode = bridge_state["spi_transfer_mode"]
        spi_manual_cs = bridge_state["spi_manual_cs"]
        spi_cs_setup_us = bridge_state["spi_cs_setup_us"]
        spi_cs_hold_us = bridge_state["spi_cs_hold_us"]
        last_rx_time = bridge_state["last_rx_time"]
        ack_events = bridge_state["ack_events"]
        spi_errors = bridge_state["spi_errors"]
        spi_startup_errors = bridge_state["spi_startup_errors"]
        spi_error_armed = bridge_state["spi_error_armed"]
        last_error = bridge_state["last_error"]
        spi_drop_events = bridge_state["spi_drop_events"]
        slot_evictions = bridge_state["slot_evictions"]
        snapshot_revision = bridge_state["snapshot_revision"]
        last_update_counter = bridge_state["last_update_counter"]
        slot_count = bridge_state["slot_count"]
        fwd_startup_resync_events = bridge_state["fwd_startup_resync_events"]
        fwd_operational_resync_events = bridge_state["fwd_operational_resync_events"]

    by_label = Counter()
    by_ssm = Counter()
    parity_ok = sniffer_stats.get("accepted_words", 0)
    parity_error = sniffer_stats.get("parity_errors", 0)
    latest_by_name = {}

    for slot in latest_slots:
        if not slot.get("valid"):
            continue

        record = record_from_slot(slot)
        label_key = f"{record['label']} ({record['name']})"
        by_label[label_key] += int(slot.get("hit_count", 0))
        by_ssm[record["ssm_txt"]] += 1
        latest_by_name[record["name"]] = {
            "timestamp": last_rx_time or record["timestamp"],
            "label": record["label"],
            "value": record["value"],
            "unit": record["unit"],
            "ssm_txt": record["ssm_txt"],
            "parity": record["parity"],
        }

    return {
        "total": sniffer_stats.get("accepted_words", 0),
        "max_records": MAX_RECORDS,
        "max_console_lines": 0,
        "serial_lines": sniffer_stats.get("accepted_words", 0),
        "serial_records": len(records_data),
        "last_rx_time": last_rx_time,
        "parity_ok": parity_ok,
        "parity_error": parity_error,
        "by_label": dict(by_label),
        "by_ssm": dict(by_ssm),
        "latest_by_name": latest_by_name,
        "running": running,
        "connected": connected,
        "port": port,
        "spi_requested_hz": spi_requested_hz,
        "spi_transfer_mode": spi_transfer_mode,
        "spi_manual_cs": spi_manual_cs,
        "spi_cs_setup_us": spi_cs_setup_us,
        "spi_cs_hold_us": spi_cs_hold_us,
        "source_mode": "spi_bridge",
        "ack_events": ack_events,
        "spi_errors": spi_errors,
        "spi_startup_errors": spi_startup_errors,
        "spi_error_armed": spi_error_armed,
        "last_error": last_error,
        "spi_drop_events": spi_drop_events,
        "slot_evictions": slot_evictions,
        "snapshot_revision": snapshot_revision,
        "last_update_counter": last_update_counter,
        "slot_count": slot_count,
        "received_words": sniffer_stats.get("received_words", 0),
        "accepted_words": sniffer_stats.get("accepted_words", 0),
        "filtered_words": sniffer_stats.get("filtered_words", 0),
        "parity_errors_sniffer": sniffer_stats.get("parity_errors", 0),
        "overflow_events": sniffer_stats.get("overflow_events", 0),
        "fwd_resync_events": sniffer_stats.get("fwd_resync_events", 0),
        "fwd_startup_resync_events": fwd_startup_resync_events,
        "fwd_operational_resync_events": fwd_operational_resync_events,
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

    try:
        payload = request.get_json(force=True, silent=False)
        pass_all, entries = parse_filter_request(payload)
        with bridge_lock:
            filter_payload = client.set_filter(pass_all=pass_all, entries=entries)
        update_filter_state(filter_payload)
        with data_lock:
            records.clear()
            bridge_state["ack_events"] = 0
            bridge_state["latest_slots"] = []
            bridge_state["snapshot_revision"] = 0
            bridge_state["last_update_counter"] = 0
            bridge_state["slot_count"] = 0
            bridge_state["slot_evictions"] = 0
            bridge_state["last_rx_time"] = None
        return jsonify(filter_payload)
    except Exception as exc:  # pragma: no cover - depende del estado del hardware SPI
        mark_bridge_error(exc)
        return jsonify({"ok": False, "msg": str(exc)}), 502


@app.route("/control/reset", methods=["POST"])
def reset_endpoint():
    try:
        with bridge_lock:
            stats_payload = client.reset_stats()
        reset_bridge_session_state(stats_payload)
        return jsonify({"ok": True, "stats": stats_payload})
    except Exception as exc:  # pragma: no cover - depende del estado del hardware SPI
        mark_bridge_error(exc)
        return jsonify({"ok": False, "msg": str(exc)}), 502


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
        "# HELP arinc_fwd_resync_startup_events_total Resincronizaciones FWD anteriores a la primera palabra valida.",
        "# TYPE arinc_fwd_resync_startup_events_total gauge",
        f"arinc_fwd_resync_startup_events_total {snapshot['fwd_startup_resync_events']}",
        "# HELP arinc_fwd_resync_operational_events_total Resincronizaciones FWD posteriores a la primera palabra valida.",
        "# TYPE arinc_fwd_resync_operational_events_total gauge",
        f"arinc_fwd_resync_operational_events_total {snapshot['fwd_operational_resync_events']}",
        "# HELP arinc_spi_drop_events_total Eventos SPI descartados por cola llena en el sniffer.",
        "# TYPE arinc_spi_drop_events_total gauge",
        f"arinc_spi_drop_events_total {snapshot['spi_drop_events']}",
        "# HELP arinc_spi_errors_total Errores SPI operativos despues de la primera comunicacion valida.",
        "# TYPE arinc_spi_errors_total gauge",
        f"arinc_spi_errors_total {snapshot['spi_errors']}",
        "# HELP arinc_spi_startup_errors_total Errores SPI de arranque antes de la primera comunicacion valida.",
        "# TYPE arinc_spi_startup_errors_total gauge",
        f"arinc_spi_startup_errors_total {snapshot['spi_startup_errors']}",
        "# HELP arinc_spi_error_armed Estado del contador operativo de errores SPI.",
        "# TYPE arinc_spi_error_armed gauge",
        f"arinc_spi_error_armed {1 if snapshot['spi_error_armed'] else 0}",
        "# HELP arinc_slot_evictions_total Reemplazos de slots del snapshot del sniffer.",
        "# TYPE arinc_slot_evictions_total gauge",
        f"arinc_slot_evictions_total {snapshot['slot_evictions']}",
        "# HELP arinc_snapshot_revision Ultima revision de snapshot observada en el sniffer.",
        "# TYPE arinc_snapshot_revision gauge",
        f"arinc_snapshot_revision {snapshot['snapshot_revision']}",
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
    app.run(debug=False, host=BRIDGE_HOST, port=BRIDGE_PORT, threaded=True, use_reloader=False)
