import argparse
import json
import time
from dataclasses import dataclass
from pathlib import Path
from typing import Any
from urllib import error as urllib_error
from urllib import request as urllib_request

from sniffer_spi_protocol import SNIFFER_SPI_FILTER_CAPACITY

BRIDGE_URL_DEFAULT = "http://127.0.0.1:5100"


PRESETS: dict[str, dict[str, Any]] = {
    "ack_temp": {
        "pass_all": False,
        "entries": [
            {"label": 0xA5, "sdi": 0},
            {"label": 0xAC, "sdi": 3},
        ],
    },
    "core4": {
        "pass_all": False,
        "entries": [
            {"label": 0xA5, "sdi": 0},
            {"label": 0xB1, "sdi": 1},
            {"label": 0xC2, "sdi": 2},
            {"label": 0xAC, "sdi": 3},
        ],
    },
    "known10": {
        "pass_all": False,
        "entries": [
            {"label": 0x11, "sdi": 0xFF},
            {"label": 0x24, "sdi": 0xFF},
            {"label": 0x39, "sdi": 0xFF},
            {"label": 0x4E, "sdi": 0xFF},
            {"label": 0x57, "sdi": 0xFF},
            {"label": 0x6A, "sdi": 0xFF},
            {"label": 0xA5, "sdi": 0},
            {"label": 0xB1, "sdi": 1},
            {"label": 0xC2, "sdi": 2},
            {"label": 0xAC, "sdi": 3},
        ],
    },
    "known11": {
        "pass_all": False,
        "entries": [
            {"label": 0x11, "sdi": 0xFF},
            {"label": 0x24, "sdi": 0xFF},
            {"label": 0x39, "sdi": 0xFF},
            {"label": 0x4E, "sdi": 0xFF},
            {"label": 0x57, "sdi": 0xFF},
            {"label": 0x6A, "sdi": 0xFF},
            {"label": 0x7D, "sdi": 0xFF},
            {"label": 0xA5, "sdi": 0},
            {"label": 0xB1, "sdi": 1},
            {"label": 0xC2, "sdi": 2},
            {"label": 0xAC, "sdi": 3},
        ],
    },
    "pass_all": {
        "pass_all": True,
        "entries": [],
    },
}


@dataclass
class StageSpec:
    name: str
    duration_sec: int


def bridge_request_json(base_url: str, path: str, method: str = "GET", payload: dict | None = None) -> dict:
    data = None
    headers = {}
    if payload is not None:
        data = json.dumps(payload).encode("utf-8")
        headers["Content-Type"] = "application/json"

    req = urllib_request.Request(
        f"{base_url.rstrip('/')}{path}",
        data=data,
        headers=headers,
        method=method,
    )
    with urllib_request.urlopen(req, timeout=5.0) as response:
        return json.loads(response.read().decode("utf-8"))


def parse_stage_specs(raw: str, default_duration: int) -> list[StageSpec]:
    stages: list[StageSpec] = []
    for token in raw.split(","):
        token = token.strip()
        if not token:
            continue

        if "@" in token:
            name, duration_text = token.split("@", 1)
            duration_sec = int(duration_text)
        else:
            name = token
            duration_sec = default_duration

        name = name.strip()
        if name not in PRESETS:
            raise ValueError(f"preset desconocido: {name}")
        stages.append(StageSpec(name=name, duration_sec=duration_sec))

    if not stages:
        raise ValueError("la lista de etapas no puede quedar vacia")
    return stages


def format_entries(entries: list[dict]) -> str:
    if not entries:
        return "(vacio)"
    parts = []
    for entry in entries:
        sdi = entry["sdi"]
        sdi_text = "ANY" if sdi == 0xFF else str(sdi)
        parts.append(f"0x{entry['label']:02X}/{sdi_text}")
    return ", ".join(parts)


def delta(end: dict, start: dict, key: str) -> int:
    return int(end.get(key, 0)) - int(start.get(key, 0))


def summarize_stage(stage: StageSpec,
                    stage_filter: dict,
                    start_stats: dict,
                    end_stats: dict,
                    samples: list[dict],
                    sample_sec: float) -> dict:
    connected_samples = sum(1 for sample in samples if sample.get("connected"))
    disconnected_samples = len(samples) - connected_samples
    throughput = delta(end_stats, start_stats, "accepted_words") / max(stage.duration_sec, 1)

    summary = {
        "stage": stage.name,
        "duration_sec": stage.duration_sec,
        "filter_mode": "PASS_ALL" if stage_filter["pass_all"] else "WHITELIST",
        "filter_entries": stage_filter["entries"],
        "filter_text": format_entries(stage_filter["entries"]),
        "accepted_delta": delta(end_stats, start_stats, "accepted_words"),
        "ack_delta": delta(end_stats, start_stats, "ack_events"),
        "parity_error_delta": delta(end_stats, start_stats, "parity_errors_sniffer"),
        "spi_errors_delta": delta(end_stats, start_stats, "spi_errors"),
        "spi_drop_delta": delta(end_stats, start_stats, "spi_drop_events"),
        "overflow_delta": delta(end_stats, start_stats, "overflow_events"),
        "fwd_resync_delta": delta(end_stats, start_stats, "fwd_resync_events"),
        "rev_resync_delta": delta(end_stats, start_stats, "rev_resync_events"),
        "slot_evictions_delta": delta(end_stats, start_stats, "slot_evictions"),
        "snapshot_revision_delta": delta(end_stats, start_stats, "snapshot_revision"),
        "throughput_words_per_sec": round(throughput, 2),
        "connected_samples": connected_samples,
        "disconnected_samples": disconnected_samples,
        "estimated_disconnected_sec": round(disconnected_samples * sample_sec, 2),
        "end_connected": bool(end_stats.get("connected", False)),
        "end_last_error": end_stats.get("last_error", ""),
        "end_slot_count": int(end_stats.get("slot_count", 0)),
        "end_latest_by_name": end_stats.get("latest_by_name", {}),
    }

    if disconnected_samples == 0 and summary["spi_errors_delta"] == 0 and summary["slot_evictions_delta"] == 0:
        summary["verdict"] = "PASS"
    elif disconnected_samples <= 2 and summary["slot_evictions_delta"] == 0:
        summary["verdict"] = "WARN"
    else:
        summary["verdict"] = "FAIL"

    return summary


def print_stage_summary(summary: dict) -> None:
    print(f"\n=== ETAPA {summary['stage']} ===")
    print(f"Filtro       : {summary['filter_mode']} | {summary['filter_text']}")
    print(f"Duracion     : {summary['duration_sec']} s")
    print(f"Veredicto    : {summary['verdict']}")
    if summary["verdict"] == "SKIP":
        print(f"Motivo       : {summary['reason']}")
        return
    print(f"Aceptadas    : +{summary['accepted_delta']}")
    print(f"ACK          : +{summary['ack_delta']}")
    print(f"Throughput   : {summary['throughput_words_per_sec']} words/s")
    print(f"SPI errors   : +{summary['spi_errors_delta']}")
    print(f"SPI drops    : +{summary['spi_drop_delta']}")
    print(f"Overflow     : +{summary['overflow_delta']}")
    print(f"Resync FWD   : +{summary['fwd_resync_delta']}")
    print(f"Resync REV   : +{summary['rev_resync_delta']}")
    print(f"Evictions    : +{summary['slot_evictions_delta']}")
    print(
        f"Conexion     : {summary['connected_samples']}/{summary['connected_samples'] + summary['disconnected_samples']} "
        f"muestras conectadas | {summary['estimated_disconnected_sec']} s desconectado aprox"
    )
    if summary["end_last_error"]:
        print(f"Ultimo error : {summary['end_last_error']}")


def main() -> None:
    parser = argparse.ArgumentParser(
        description="Runner por etapas para medir capacidad del bridge SPI + sniffer."
    )
    parser.add_argument(
        "--bridge-url",
        default=BRIDGE_URL_DEFAULT,
        help="URL base del bridge SPI, por defecto http://127.0.0.1:5100",
    )
    parser.add_argument(
        "--stages",
        default="core4@180,known10@180,pass_all@60",
        help="Etapas CSV. Usa nombre@segundos. Presets: ack_temp, core4, known10, known11, pass_all",
    )
    parser.add_argument(
        "--duration-sec",
        type=int,
        default=180,
        help="Duracion default por etapa cuando no se usa @segundos.",
    )
    parser.add_argument(
        "--sample-sec",
        type=float,
        default=2.0,
        help="Periodo de muestreo de /stats durante cada etapa.",
    )
    parser.add_argument(
        "--reset-before-stage",
        action="store_true",
        help="Resetea stats antes de cada etapa para aislar mejor las mediciones.",
    )
    parser.add_argument(
        "--settle-sec",
        type=float,
        default=3.0,
        help="Espera despues de aplicar filtro y/o reset antes de muestrear.",
    )
    parser.add_argument(
        "--output-json",
        default="spi_capacity_report.json",
        help="Archivo JSON de salida para guardar el reporte.",
    )
    args = parser.parse_args()

    stages = parse_stage_specs(args.stages, args.duration_sec)

    report = {
        "bridge_url": args.bridge_url,
        "sample_sec": args.sample_sec,
        "reset_before_stage": args.reset_before_stage,
        "settle_sec": args.settle_sec,
        "started_at": time.strftime("%Y-%m-%d %H:%M:%S"),
        "stages": [],
    }

    print("=== SPI CAPACITY RUNNER ===")
    print(f"Bridge URL   : {args.bridge_url}")
    print(f"Stages       : {', '.join(f'{stage.name}@{stage.duration_sec}s' for stage in stages)}")
    print(f"Sample sec   : {args.sample_sec}")
    print(f"Reset/etapa  : {'si' if args.reset_before_stage else 'no'}")
    print(f"Settle sec   : {args.settle_sec}")

    try:
        initial_stats = bridge_request_json(args.bridge_url, "/stats")
    except urllib_error.URLError as exc:
        raise SystemExit(f"No se pudo contactar al bridge SPI: {exc}") from exc

    print(f"Estado inicial: connected={initial_stats.get('connected')} running={initial_stats.get('running')}")

    for stage in stages:
        preset = PRESETS[stage.name]
        if not preset["pass_all"] and len(preset["entries"]) > SNIFFER_SPI_FILTER_CAPACITY:
            summary = {
                "stage": stage.name,
                "duration_sec": stage.duration_sec,
                "filter_mode": "WHITELIST",
                "filter_entries": preset["entries"],
                "filter_text": format_entries(preset["entries"]),
                "verdict": "SKIP",
                "reason": (
                    f"requiere {len(preset['entries'])} entradas pero el filtro del sniffer "
                    f"soporta {SNIFFER_SPI_FILTER_CAPACITY}"
                ),
            }
            report["stages"].append(summary)
            print_stage_summary(summary)
            continue

        print(f"\nAplicando etapa {stage.name}...")
        try:
            bridge_request_json(
                args.bridge_url,
                "/filter",
                method="POST",
                payload={
                    "pass_all": preset["pass_all"],
                    "entries": preset["entries"],
                },
            )
        except urllib_error.HTTPError as exc:
            body = exc.read().decode("utf-8", errors="replace")
            summary = {
                "stage": stage.name,
                "duration_sec": stage.duration_sec,
                "filter_mode": "PASS_ALL" if preset["pass_all"] else "WHITELIST",
                "filter_entries": preset["entries"],
                "filter_text": format_entries(preset["entries"]),
                "verdict": "FAIL",
                "reason": f"HTTP {exc.code}: {body}",
            }
            report["stages"].append(summary)
            print_stage_summary(summary)
            continue

        if args.reset_before_stage:
            bridge_request_json(args.bridge_url, "/control/reset", method="POST", payload={})

        if args.settle_sec > 0:
            time.sleep(args.settle_sec)

        start_stats = bridge_request_json(args.bridge_url, "/stats")
        samples = [start_stats]
        deadline = time.time() + stage.duration_sec
        while time.time() < deadline:
            time.sleep(args.sample_sec)
            samples.append(bridge_request_json(args.bridge_url, "/stats"))

        end_stats = samples[-1]
        summary = summarize_stage(
            stage=stage,
            stage_filter=preset,
            start_stats=start_stats,
            end_stats=end_stats,
            samples=samples,
            sample_sec=args.sample_sec,
        )
        report["stages"].append(summary)
        print_stage_summary(summary)

    report["finished_at"] = time.strftime("%Y-%m-%d %H:%M:%S")
    output_path = Path(args.output_json)
    output_path.write_text(json.dumps(report, indent=2, ensure_ascii=False), encoding="utf-8")
    print(f"\nReporte guardado en {output_path}")


if __name__ == "__main__":
    main()
