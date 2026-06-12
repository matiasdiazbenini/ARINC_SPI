import argparse
import json
import socket
import time
from pathlib import Path

from sniffer_spi_protocol import ProtocolError
from spi_link_diagnostic import (
    fetch_stats_once,
    parse_csv_ints,
    run_ping_series,
)


DEFAULT_HZ_LIST = (
    "100000,200000,400000,800000,1000000,2000000,"
    "4000000,8000000,16000000,32000000,50000000"
)


def bridge_is_listening(host: str, port: int) -> bool:
    try:
        with socket.create_connection((host, port), timeout=0.3):
            return True
    except OSError:
        return False


def print_failures(failures: dict[str, int]) -> None:
    for reason, count in failures.items():
        print(f"    fallo x{count}: {reason}")


def classify_result(success: int, attempts: int, stats_ok: bool) -> str:
    if success == attempts and stats_ok:
        return "PASS"
    if success > 0:
        return "PARTIAL"
    return "FAIL"


def main() -> None:
    parser = argparse.ArgumentParser(
        description=(
            "Barrido escalonado de frecuencia para el enlace PIO-frame entre "
            "ARINC-SNIFFER y Raspberry Pi 3B+."
        )
    )
    parser.add_argument("--bus", type=int, default=0, help="Bus SPI de Linux.")
    parser.add_argument("--device", type=int, default=0, help="Device SPI de Linux.")
    parser.add_argument(
        "--hz-list",
        default=DEFAULT_HZ_LIST,
        help="Frecuencias solicitadas en Hz, separadas por coma.",
    )
    parser.add_argument(
        "--attempts",
        type=int,
        default=24,
        help="PING medidos por frecuencia. Por defecto 24.",
    )
    parser.add_argument(
        "--warmup-attempts",
        type=int,
        default=3,
        help="PING de calentamiento no incluidos en el veredicto.",
    )
    parser.add_argument(
        "--retries",
        type=int,
        default=1,
        help=(
            "Intentos internos por PING. Mantener en 1 para no ocultar errores "
            "transitorios durante la caracterizacion."
        ),
    )
    parser.add_argument(
        "--delay-ms",
        type=int,
        default=2,
        help="Espera entre request y ventana de respuesta.",
    )
    parser.add_argument(
        "--recovery-gap-ms",
        type=int,
        default=12,
        help="Pausa de recuperacion despues de una respuesta invalida.",
    )
    parser.add_argument(
        "--cs-gpio",
        type=int,
        default=8,
        help="GPIO BCM usado como CS manual.",
    )
    parser.add_argument(
        "--cs-setup-us",
        type=int,
        default=100,
        help="Tiempo entre CS bajo y primer clock.",
    )
    parser.add_argument(
        "--cs-hold-us",
        type=int,
        default=100,
        help="Tiempo entre ultimo clock y CS alto.",
    )
    cs_group = parser.add_mutually_exclusive_group()
    cs_group.add_argument(
        "--manual-cs",
        dest="manual_cs",
        action="store_true",
        help="Usa GPIO manual para CS. Es el modo recomendado.",
    )
    cs_group.add_argument(
        "--hardware-cs",
        dest="manual_cs",
        action="store_false",
        help="Usa CE hardware de spidev.",
    )
    parser.set_defaults(manual_cs=True)
    parser.add_argument(
        "--between-hz-ms",
        type=int,
        default=250,
        help="Pausa entre frecuencias.",
    )
    parser.add_argument(
        "--continue-after-fail",
        action="store_true",
        help=(
            "Continua despues de PARTIAL/FAIL. No recomendado: una perdida de "
            "flancos puede dejar el PIO desalineado para los puntos siguientes."
        ),
    )
    parser.add_argument(
        "--bridge-host",
        default="127.0.0.1",
        help="Host usado para comprobar que el bridge esta detenido.",
    )
    parser.add_argument(
        "--bridge-port",
        type=int,
        default=5100,
        help="Puerto usado para comprobar que el bridge esta detenido.",
    )
    parser.add_argument(
        "--allow-bridge-running",
        action="store_true",
        help="Permite continuar aunque el puerto del bridge responda. No recomendado.",
    )
    parser.add_argument(
        "--output-json",
        default="",
        help="Ruta del reporte. Si se omite, usa un nombre con fecha y hora.",
    )
    args = parser.parse_args()

    if args.attempts <= 0:
        raise SystemExit("--attempts debe ser mayor que cero")
    if args.retries <= 0:
        raise SystemExit("--retries debe ser mayor que cero")
    if args.delay_ms < 0:
        raise SystemExit("--delay-ms no puede ser negativo")

    hz_values = sorted(set(parse_csv_ints(args.hz_list)))
    if any(hz <= 0 for hz in hz_values):
        raise SystemExit("Todas las frecuencias deben ser mayores que cero")

    if (
        not args.allow_bridge_running
        and bridge_is_listening(args.bridge_host, args.bridge_port)
    ):
        raise SystemExit(
            f"El bridge sigue escuchando en {args.bridge_host}:{args.bridge_port}. "
            "Detenelo antes del barrido para evitar dos procesos usando SPI."
        )

    timestamp = time.strftime("%Y%m%d_%H%M%S")
    output_path = Path(
        args.output_json or f"spi_frequency_sweep_{timestamp}.json"
    )
    manual_cs_gpio = args.cs_gpio if args.manual_cs else None

    report = {
        "started_at": time.strftime("%Y-%m-%d %H:%M:%S"),
        "bus": args.bus,
        "device": args.device,
        "transfer_mode": "pio-frame",
        "requested_hz": hz_values,
        "attempts": args.attempts,
        "warmup_attempts": args.warmup_attempts,
        "retries": args.retries,
        "delay_ms": args.delay_ms,
        "recovery_gap_ms": args.recovery_gap_ms,
        "manual_cs": args.manual_cs,
        "cs_gpio": args.cs_gpio if args.manual_cs else None,
        "cs_setup_us": args.cs_setup_us,
        "cs_hold_us": args.cs_hold_us,
        "continue_after_fail": args.continue_after_fail,
        "results": [],
        "note": (
            "Las frecuencias son valores solicitados a spidev. Para medir el clock "
            "fisico efectivo se necesita osciloscopio o analizador logico."
        ),
    }

    print("=== SPI PIO-FRAME FREQUENCY SWEEP ===")
    print(f"Bus/device  : spidev{args.bus}.{args.device}")
    print(f"Frecuencias : {hz_values}")
    print(f"PING medidos: {args.attempts} por frecuencia")
    print(f"Reintentos  : {args.retries} por PING")
    print(f"Delay resp. : {args.delay_ms} ms")
    if args.manual_cs:
        print(
            f"CS manual   : GPIO{args.cs_gpio} | "
            f"setup={args.cs_setup_us}us hold={args.cs_hold_us}us"
        )
    else:
        print("CS          : CE hardware")
    print()

    for hz in hz_values:
        print(f"[{hz:>8} Hz] iniciando...")

        warmup = None
        if args.warmup_attempts > 0:
            warmup = run_ping_series(
                bus=args.bus,
                device=args.device,
                hz=hz,
                delay_ms=args.delay_ms,
                attempts=args.warmup_attempts,
                retries=args.retries,
                manual_cs_gpio=manual_cs_gpio,
                cs_setup_us=args.cs_setup_us,
                cs_hold_us=args.cs_hold_us,
                byte_delay_us=0,
                transfer_mode="pio-frame",
                recovery_gap_ms=args.recovery_gap_ms,
            )

        measured = run_ping_series(
            bus=args.bus,
            device=args.device,
            hz=hz,
            delay_ms=args.delay_ms,
            attempts=args.attempts,
            retries=args.retries,
            manual_cs_gpio=manual_cs_gpio,
            cs_setup_us=args.cs_setup_us,
            cs_hold_us=args.cs_hold_us,
            byte_delay_us=0,
            transfer_mode="pio-frame",
            recovery_gap_ms=args.recovery_gap_ms,
        )

        stats_ok = False
        stats = None
        stats_error = ""
        if measured["success"] > 0:
            try:
                stats, _last_raw = fetch_stats_once(
                    bus=args.bus,
                    device=args.device,
                    hz=hz,
                    delay_ms=args.delay_ms,
                    retries=args.retries,
                    manual_cs_gpio=manual_cs_gpio,
                    cs_setup_us=args.cs_setup_us,
                    cs_hold_us=args.cs_hold_us,
                    byte_delay_us=0,
                    transfer_mode="pio-frame",
                    recovery_gap_ms=args.recovery_gap_ms,
                )
                stats_ok = True
            except (ProtocolError, OSError, RuntimeError) as exc:
                stats_error = str(exc)

        verdict = classify_result(
            measured["success"],
            measured["attempts"],
            stats_ok,
        )
        result = {
            "requested_hz": hz,
            "verdict": verdict,
            "warmup_success": warmup["success"] if warmup is not None else None,
            "warmup_attempts": (
                warmup["attempts"] if warmup is not None else 0
            ),
            "success": measured["success"],
            "attempts": measured["attempts"],
            "failures": measured["failures"],
            "stats_ok": stats_ok,
            "stats_error": stats_error,
            "stats": stats,
        }
        report["results"].append(result)

        print(
            f"    {verdict}: PING={measured['success']}/{measured['attempts']} "
            f"| GET_STATS={'OK' if stats_ok else 'FAIL'}"
        )
        if measured["failures"]:
            print_failures(measured["failures"])
        if stats_error:
            print(f"    GET_STATS: {stats_error}")

        if verdict != "PASS" and not args.continue_after_fail:
            report["stopped_after_hz"] = hz
            print(
                "    Barrido detenido para no contaminar frecuencias posteriores. "
                "Reiniciar la sniffer antes de otra prueba."
            )
            break

        if args.between_hz_ms > 0:
            time.sleep(args.between_hz_ms / 1000.0)

    perfect_hz = [
        result["requested_hz"]
        for result in report["results"]
        if result["verdict"] == "PASS"
    ]
    contiguous_ceiling = None
    for result in report["results"]:
        if result["verdict"] != "PASS":
            break
        contiguous_ceiling = result["requested_hz"]

    report["finished_at"] = time.strftime("%Y-%m-%d %H:%M:%S")
    report["highest_pass_hz"] = max(perfect_hz) if perfect_hz else None
    report["contiguous_pass_ceiling_hz"] = contiguous_ceiling
    output_path.write_text(
        json.dumps(report, indent=2, ensure_ascii=False),
        encoding="utf-8",
    )

    print()
    print("=== RESULTADO ===")
    print(f"Mayor frecuencia PASS     : {report['highest_pass_hz']}")
    print(f"Techo PASS sin interrupcion: {contiguous_ceiling}")
    print(f"Reporte JSON              : {output_path}")
    print(
        "Nota: PASS significa PING perfecto sin reintentos ocultos y GET_STATS valido."
    )

    if not perfect_hz:
        raise SystemExit(2)


if __name__ == "__main__":
    main()
