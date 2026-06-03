#!/usr/bin/env python3
"""Monitorea GPIO25 de la Raspberry Pi para la prueba bruta GP20<->GPIO25."""

from __future__ import annotations

import argparse
import shutil
import subprocess
import sys
import time
from dataclasses import dataclass


@dataclass
class Backend:
    name: str
    executable: str


def detect_backend() -> Backend:
    for candidate in ("pinctrl", "raspi-gpio"):
        path = shutil.which(candidate)
        if path:
            return Backend(name=candidate, executable=path)
    raise RuntimeError("No se encontro pinctrl ni raspi-gpio en esta Raspberry Pi.")


def run_checked(command: list[str]) -> str:
    completed = subprocess.run(command, capture_output=True, text=True, check=True)
    return completed.stdout.strip()


def configure_input(backend: Backend, pin: int) -> None:
    if backend.name == "pinctrl":
        subprocess.run([backend.executable, "set", str(pin), "ip", "pd"], check=True)
    else:
        subprocess.run([backend.executable, "set", str(pin), "ip", "pd"], check=True)


def parse_level(output: str) -> int:
    text = output.lower()

    if "level=1" in text or " hi " in f" {text} " or text.endswith(" hi"):
        return 1
    if "level=0" in text or " lo " in f" {text} " or text.endswith(" lo"):
        return 0

    raise RuntimeError(f"No pude interpretar el nivel GPIO desde: {output}")


def read_level(backend: Backend, pin: int) -> int:
    output = run_checked([backend.executable, "get", str(pin)])
    return parse_level(output)


def main() -> int:
    parser = argparse.ArgumentParser(description="Monitorea GPIO25 para validar el enlace GP20<->GPIO25.")
    parser.add_argument("--pin", type=int, default=25, help="GPIO BCM a leer en la Pi (default: 25).")
    parser.add_argument("--seconds", type=float, default=8.0, help="Duracion total del monitoreo.")
    parser.add_argument("--poll-ms", type=float, default=50.0, help="Periodo de sondeo en milisegundos.")
    args = parser.parse_args()

    backend = detect_backend()
    configure_input(backend, args.pin)

    poll_s = max(args.poll_ms / 1000.0, 0.01)
    started = time.monotonic()
    deadline = started + max(args.seconds, poll_s)
    last_level = read_level(backend, args.pin)
    transitions = 0
    high_samples = 0
    total_samples = 0

    print("=== GPIO PROBE ===")
    print(f"Backend : {backend.name}")
    print(f"GPIO BCM: {args.pin}")
    print(f"Duracion: {args.seconds:.1f} s")
    print(f"Sondeo  : {args.poll_ms:.0f} ms")
    print(f"Nivel inicial: {last_level}")

    while time.monotonic() < deadline:
        level = read_level(backend, args.pin)
        total_samples += 1
        if level:
            high_samples += 1

        if level != last_level:
            transitions += 1
            elapsed = time.monotonic() - started
            print(f"[{elapsed:6.2f}s] cambio -> {level}")
            last_level = level

        time.sleep(poll_s)

    print("")
    print(f"Transiciones detectadas: {transitions}")
    print(f"Muestras en alto      : {high_samples}/{total_samples}")

    if transitions == 0 and high_samples == 0:
        print("Resultado: linea clavada en 0. El cable GP20<->GPIO25 o la señal de la Pico no se estan viendo.")
    elif transitions >= 6:
        print("Resultado: heartbeat visible. La conectividad GP20<->GPIO25 parece sana.")
    elif transitions > 0:
        print("Resultado: hubo cambios de nivel, pero no muchos. Puede haber pulso SPI aislado o muestreo insuficiente.")
    else:
        print("Resultado: nivel alto sin transiciones claras. Revisar pull-down o estado fijo del pin.")

    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except KeyboardInterrupt:
        print("\nInterrumpido por usuario.")
        raise SystemExit(130)
    except Exception as exc:  # pragma: no cover - diagnostico de campo
        print(f"ERROR: {exc}", file=sys.stderr)
        raise SystemExit(1)
