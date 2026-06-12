#!/usr/bin/env python3
"""Prueba minima de frame SPI entre Raspberry Pi 3B+ y Pico."""

import argparse
import shutil
import subprocess
import time

try:
    import spidev
except ImportError as exc:
    raise SystemExit(
        "python3-spidev no esta disponible en esta Raspberry Pi. "
        "Instalalo con: sudo apt install -y python3-spidev"
    ) from exc


FRAME_SIZE = 32


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
        raise RuntimeError("No se encontro pinctrl ni raspi-gpio para manejar CS manual.")

    def _run_set(self, mode: str, level: str) -> None:
        subprocess.run([self.backend, "set", str(self.gpio), mode, level], check=True)

    def low(self) -> None:
        self._run_set("op", "dl")

    def high(self) -> None:
        self._run_set("op", "dh")


def build_tx_frame() -> bytes:
    payload = bytearray(FRAME_SIZE)
    payload[0] = 0xA4
    payload[1] = 0x29
    payload[2] = 0x50  # P
    payload[3] = 0x49  # I
    payload[4] = 0x33  # 3
    payload[5] = 0x42  # B
    payload[6] = 0x2B  # +
    payload[7] = 0x2B  # +
    for i in range(8, FRAME_SIZE):
        payload[i] = (0x80 + i) & 0xFF
    return bytes(payload)


def classify_response(raw: bytes) -> str:
    if all(byte == 0x00 for byte in raw):
        return "todo_cero"
    if raw[:8] == bytes((0xA4, 0x29, 0x53, 0x50, 0x49, 0x4D, 0x49, 0x4E)):
        return "firma_SPI_MIN_OK"
    if raw[:2] == bytes((0xA4, 0x29)):
        return "magic_ok_firma_distinta"
    return f"cabecera={raw[:8].hex()}"


def transfer_frame(spi,
                   tx_frame: bytes,
                   cs: ManualCsController | None,
                   cs_setup_us: int,
                   cs_hold_us: int) -> bytes:
    if cs is None:
        return bytes(spi.xfer2(list(tx_frame)))

    cs.low()
    if cs_setup_us > 0:
        time.sleep(cs_setup_us / 1_000_000.0)
    try:
        return bytes(spi.xfer2(list(tx_frame)))
    finally:
        if cs_hold_us > 0:
            time.sleep(cs_hold_us / 1_000_000.0)
        cs.high()


def main():
    parser = argparse.ArgumentParser(description="Prueba minima SPI master para Pico slave.")
    parser.add_argument("--bus", type=int, default=0)
    parser.add_argument("--device", type=int, default=0)
    parser.add_argument("--hz", type=int, default=10000)
    parser.add_argument("--mode", type=int, default=0)
    parser.add_argument("--attempts", type=int, default=10)
    parser.add_argument("--sleep-ms", type=int, default=300)
    parser.add_argument("--manual-cs", action="store_true")
    parser.add_argument("--cs-gpio", type=int, default=8)
    parser.add_argument("--cs-setup-us", type=int, default=50)
    parser.add_argument("--cs-hold-us", type=int, default=50)
    args = parser.parse_args()

    spi = spidev.SpiDev()
    spi.open(args.bus, args.device)
    spi.max_speed_hz = args.hz
    spi.mode = args.mode
    spi.bits_per_word = 8
    if args.manual_cs:
        spi.no_cs = True

    tx_frame = build_tx_frame()
    cs = ManualCsController(args.cs_gpio) if args.manual_cs else None

    print("=== SPI SLAVE MIN TEST ===")
    print(f"spidev{args.bus}.{args.device} | hz={args.hz} | mode={args.mode} | attempts={args.attempts}")
    if args.manual_cs:
        print(f"CS manual GPIO{args.cs_gpio} | setup={args.cs_setup_us}us | hold={args.cs_hold_us}us")
    else:
        print("CS hardware CE")
    print(f"TX first8: {tx_frame[:8].hex(' ')}")
    print("")

    try:
        for attempt in range(1, args.attempts + 1):
            raw = transfer_frame(spi, tx_frame, cs, args.cs_setup_us, args.cs_hold_us)
            verdict = classify_response(raw)
            print(f"[{attempt:02d}] RX first8={raw[:8].hex(' ')} | {verdict}")
            time.sleep(max(args.sleep_ms, 0) / 1000.0)
    finally:
        spi.close()


if __name__ == "__main__":
    main()
