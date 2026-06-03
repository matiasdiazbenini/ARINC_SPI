#!/usr/bin/env python3
"""Prueba simple de continuidad GPIO entre Raspberry Pi 3B+ y Pico sniffer."""

from __future__ import annotations

import argparse
import shutil
import subprocess
import sys
import time
from dataclasses import dataclass


@dataclass(frozen=True)
class PiOutToPicoIn:
    pi_gpio: int
    pico_pin_name: str
    description: str
    pulses: int


PI_OUTPUTS = (
    PiOutToPicoIn(8, "GP17<=CSn", "Pi GPIO8 (pin 24 / CE0) -> Pico GP17", 2),
    PiOutToPicoIn(10, "GP16<=MOSI", "Pi GPIO10 (pin 19 / MOSI) -> Pico GP16", 3),
    PiOutToPicoIn(11, "GP18<=SCK", "Pi GPIO11 (pin 23 / SCLK) -> Pico GP18", 4),
)

PI_INPUTS = {
    9: "Pi GPIO9  (pin 21 / MISO)  <= Pico GP19",
    25: "Pi GPIO25 (pin 22 / DRDY) <= Pico GP20",
}


class GpioBackend:
    def __init__(self) -> None:
        self.executable = self._detect_backend()

    @staticmethod
    def _detect_backend() -> str:
        for candidate in ("pinctrl", "raspi-gpio"):
            path = shutil.which(candidate)
            if path:
                return path
        raise RuntimeError("No se encontro pinctrl ni raspi-gpio en esta Raspberry Pi.")

    def set_output(self, gpio: int, high: bool) -> None:
        level = "dh" if high else "dl"
        subprocess.run([self.executable, "set", str(gpio), "op", level], check=True)

    def set_input_pulldown(self, gpio: int) -> None:
        subprocess.run([self.executable, "set", str(gpio), "ip", "pd"], check=True)

    def read(self, gpio: int) -> int:
        output = subprocess.run(
            [self.executable, "get", str(gpio)],
            capture_output=True,
            text=True,
            check=True,
        ).stdout.lower()

        if "level=1" in output or " hi" in output:
            return 1
        if "level=0" in output or " lo" in output:
            return 0
        raise RuntimeError(f"No pude interpretar el nivel GPIO desde: {output.strip()}")


def drive_output(backend: GpioBackend, spec: PiOutToPicoIn, pulse_ms: int) -> None:
    print(f"\nFASE SALIDA: {spec.description}")
    print(f"Esperado en Tera Term de la Pico: {spec.pico_pin_name} con {spec.pulses} pulsos.")
    backend.set_output(spec.pi_gpio, False)
    time.sleep(0.2)

    for pulse in range(1, spec.pulses + 1):
        print(f"  pulso {pulse}/{spec.pulses} -> GPIO{spec.pi_gpio}=1")
        backend.set_output(spec.pi_gpio, True)
        time.sleep(pulse_ms / 1000.0)
        print(f"  pulso {pulse}/{spec.pulses} -> GPIO{spec.pi_gpio}=0")
        backend.set_output(spec.pi_gpio, False)
        time.sleep(pulse_ms / 1000.0)


def observe_input(backend: GpioBackend, gpio: int, seconds: float, poll_ms: int) -> tuple[int, int]:
    backend.set_input_pulldown(gpio)
    last_level = backend.read(gpio)
    transitions = 0
    high_samples = 0
    total_samples = 0
    deadline = time.monotonic() + seconds

    while time.monotonic() < deadline:
        level = backend.read(gpio)
        total_samples += 1
        if level:
            high_samples += 1
        if level != last_level:
            transitions += 1
            last_level = level
        time.sleep(max(poll_ms / 1000.0, 0.01))

    return transitions, high_samples


def main() -> int:
    parser = argparse.ArgumentParser(
        description="Prueba de continuidad GPIO entre la Raspberry Pi 3B+ y la Pico sniffer."
    )
    parser.add_argument("--pulse-ms", type=int, default=250, help="Duracion de cada pulso de salida en ms.")
    parser.add_argument("--observe-sec", type=float, default=6.0, help="Tiempo de observacion para GPIO9/GPIO25.")
    parser.add_argument("--poll-ms", type=int, default=50, help="Periodo de sondeo para GPIO9/GPIO25.")
    args = parser.parse_args()

    backend = GpioBackend()

    print("=== GPIO SPI CONTINUITY TEST ===")
    print("1. Flashear la Pico con ARINC-SNIFFER_GPIO_TEST.uf2")
    print("2. Abrir Tera Term sobre la Pico y dejar visible su salida USB")
    print("3. Mantener solo estas conexiones:")
    print("   Pi GPIO8  <-> Pico GP17")
    print("   Pi GPIO10 <-> Pico GP16")
    print("   Pi GPIO11 <-> Pico GP18")
    print("   Pi GPIO9  <-> Pico GP19")
    print("   Pi GPIO25 <-> Pico GP20")
    print("   GND comun")

    for spec in PI_OUTPUTS:
        drive_output(backend, spec, pulse_ms=args.pulse_ms)

    print("\nFASE ENTRADAS: observando si la Pi ve actividad desde la Pico")
    for gpio, description in PI_INPUTS.items():
        transitions, high_samples = observe_input(
            backend,
            gpio=gpio,
            seconds=args.observe_sec,
            poll_ms=args.poll_ms,
        )
        print(f"  {description}")
        print(f"    transiciones={transitions} | muestras_en_alto={high_samples}")

    print("\nInterpretacion:")
    print("- Si la Pico muestra cambios en GP16/GP17/GP18 al pulsar GPIO8/10/11, esas lineas tienen continuidad.")
    print("- Si la Pi detecta transiciones en GPIO9/GPIO25, las lineas GP19/GP20 tambien tienen continuidad.")
    print("- Si alguna linea no cambia, el problema ya no es SPI: es pinout, cable o contacto.")
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except KeyboardInterrupt:
        print("\nInterrumpido por usuario.")
        raise SystemExit(130)
    except Exception as exc:
        print(f"ERROR: {exc}", file=sys.stderr)
        raise SystemExit(1)
